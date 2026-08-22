#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <sys/statvfs.h>

#include <cstdio>
#include <string>
#include <vector>

#include "camera_image.h"
#include "camera_source.h"
#include "capture_config.h"
#include "imu_source.h"
#include "input.h"
#include "preview_renderer.h"
#include "session_recorder.h"

namespace {

using sensor_logger::CameraImageView;
using sensor_logger::CameraSource;
using sensor_logger::CaptureResult;
using sensor_logger::CaptureConfig;
using sensor_logger::Retention;
using sensor_logger::FrameData;
using sensor_logger::ImuSample;
using sensor_logger::Action;
using sensor_logger::ImuSource;
using sensor_logger::Input;
using sensor_logger::PendingFrame;
using sensor_logger::PreviewRenderer;
using sensor_logger::SessionRecorder;
using sensor_logger::SessionsOverlay;

constexpr char kTag[] = "sensor_logger";

void LogInfo(const char* what) {
  __android_log_print(ANDROID_LOG_INFO, kTag, "%s", what);
}

void LogError(const char* what) {
  __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", what);
}

// Holds everything that depends on the window, so it can be torn down and
// rebuilt when the app goes to background and comes back.
struct AppState {
  android_app* app = nullptr;

  EGLDisplay display = EGL_NO_DISPLAY;
  EGLSurface surface = EGL_NO_SURFACE;
  EGLContext context = EGL_NO_CONTEXT;
  int width = 0;
  int height = 0;

  CameraSource camera;
  ImuSource imu;
  PreviewRenderer preview;
  SessionRecorder recorder;
  Input input;
  CaptureConfig config;
  bool capturing = false;

  // Free space, refreshed on a timer rather than per frame: statvfs on the
  // shared storage is a round trip through the filesystem daemon, and the
  // number moves slowly enough that a couple of seconds stale is honest.
  int64_t free_bytes = 0;
  int free_space_countdown = 0;
  int64_t last_timestamp_ns = 0;

  // The camera's own account of the last frame it finished, kept whether or not
  // anything is being recorded. Watching exposure and focus settle is how the
  // decision to start is made, and they only settle while nothing is locked.
  sensor_logger::CaptureResult last_result;

  // Why the last session ended, when it was not asked to. Shown until the next
  // one starts, since the reason is worth more at the moment of finding out
  // than in a log read afterwards.
  const char* stop_reason = nullptr;

  // The picture is not drawn at all unless asked for.
  //
  // The numbers are what the screen is for during a capture; the picture is for
  // aiming, which is a thing done between captures more than during one. Not
  // drawing it also skips uploading a frame as two textures every frame, which
  // is what was capping the frame rate, and leaves nearly the whole OLED panel
  // switched off.
  bool preview_visible = false;
  bool sessions_overlay_visible = false;
  int pending_delete_index = -1;
  int sessions_page = 0;
  bool permission_granted = false;

  // The sessions overlay's contents. Fetched once when the overlay opens
  // (each session's size costs a recursive stat of its whole directory tree,
  // too expensive to redo every frame) and updated in place on delete, rather
  // than re-read. Delete taps also resolve against this same list rather than
  // a fresh directory read: readdir order is not guaranteed stable between
  // calls, so a second read could put a different session at the tapped index
  // and delete the wrong one.
  std::vector<sensor_logger::SessionItem> cached_sessions;
};

// Runtime camera permission, requested through JNI because there is no native
// entry point for it. Returns true once the permission is already held.
bool HasCameraPermission(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass context_class = env->GetObjectClass(activity);
  jmethodID check_permission = env->GetMethodID(
      context_class, "checkSelfPermission", "(Ljava/lang/String;)I");

  jstring permission = env->NewStringUTF("android.permission.CAMERA");
  const jint result = env->CallIntMethod(activity, check_permission, permission);

  env->DeleteLocalRef(permission);
  env->DeleteLocalRef(context_class);

  // PackageManager.PERMISSION_GRANTED
  return result == 0;
}

void RequestCameraPermission(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID request = env->GetMethodID(activity_class, "requestPermissions",
                                       "([Ljava/lang/String;I)V");

  jobjectArray permissions = env->NewObjectArray(
      1, env->FindClass("java/lang/String"),
      env->NewStringUTF("android.permission.CAMERA"));

  env->CallVoidMethod(activity, request, permissions, 0);

  env->DeleteLocalRef(permissions);
  env->DeleteLocalRef(activity_class);
}

std::string FilesRoot(android_app* app) {
  return app->activity->externalDataPath != nullptr
             ? app->activity->externalDataPath
             : app->activity->internalDataPath;
}

std::string SessionRoot(android_app* app) {
  return FilesRoot(app) + "/sessions";
}

bool InitDisplay(AppState* state) {
  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (state->display == EGL_NO_DISPLAY) return false;
  eglInitialize(state->display, nullptr, nullptr);

  const EGLint config_attribs[] = {EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES3_BIT,
                                   EGL_SURFACE_TYPE,
                                   EGL_WINDOW_BIT,
                                   EGL_BLUE_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_NONE};

  EGLConfig config;
  EGLint num_configs = 0;
  eglChooseConfig(state->display, config_attribs, &config, 1, &num_configs);
  if (num_configs < 1) return false;

  state->surface =
      eglCreateWindowSurface(state->display, config, state->app->window, nullptr);
  if (state->surface == EGL_NO_SURFACE) return false;

  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  state->context =
      eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attribs);
  if (state->context == EGL_NO_CONTEXT) return false;

  if (eglMakeCurrent(state->display, state->surface, state->surface,
                     state->context) == EGL_FALSE) {
    return false;
  }

  eglQuerySurface(state->display, state->surface, EGL_WIDTH, &state->width);
  eglQuerySurface(state->display, state->surface, EGL_HEIGHT, &state->height);

  if (!state->preview.Init()) {
    LogError("could not build the preview shaders");
    return false;
  }
  state->preview.SetViewport(state->width, state->height);

  return true;
}

void TerminateDisplay(AppState* state) {
  // Before the context goes away, since the objects belong to it.
  state->preview.Destroy();

  if (state->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (state->context != EGL_NO_CONTEXT) {
      eglDestroyContext(state->display, state->context);
    }
    if (state->surface != EGL_NO_SURFACE) {
      eglDestroySurface(state->display, state->surface);
    }
    eglTerminate(state->display);
  }

  state->display = EGL_NO_DISPLAY;
  state->context = EGL_NO_CONTEXT;
  state->surface = EGL_NO_SURFACE;
}

void StartCapture(AppState* state) {
  if (state->capturing) return;
  if (!state->permission_granted) return;

  if (!state->camera.Start(state->config)) {
    LogError("could not start the camera");
    return;
  }

  state->capturing = true;

  // Inertial capture starts with the camera and stops with it. Both run for the
  // whole session, and the samples covering a gap in the images are exactly the
  // ones that could bridge it later.
  //
  // Not started earlier: the sensor queue shares the looper, and until there is
  // a camera to wait on, a stream of sensor events would hold the main loop in
  // its drain loop, where the camera permission is never re-checked.
  state->imu.Start(state->app->looper, "com.sensor.logger");
}

void StopCapture(AppState* state) {
  if (!state->capturing) return;

  state->recorder.Stop();
  state->imu.Stop();
  state->camera.Stop();
  state->capturing = false;
}

int64_t FreeBytes(const std::string& path) {
  struct statvfs stats{};
  if (statvfs(path.c_str(), &stats) != 0) return 0;
  return static_cast<int64_t>(stats.f_bavail) * stats.f_frsize;
}

// Bytes as something readable at arm's length. Two significant figures is as
// much as anyone acts on.
std::string Bytes(int64_t bytes) {
  char buffer[32];
  if (bytes >= 1000LL * 1000 * 1000) {
    std::snprintf(buffer, sizeof(buffer), "%.1FGB",
                  static_cast<double>(bytes) / (1000.0 * 1000 * 1000));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%lldMB",
                  static_cast<long long>(bytes / (1000 * 1000)));
  }
  return buffer;
}

// Below this, recording stops on its own.
//
// Not zero, and not small. A session that runs the disk to the last byte cannot
// write its own manifest — the settings, the counts, the calibration all go
// with it — so a capture that filled a phone ends up unusable anyway. This is
// enough room for a hundred frames at full resolution, which is plenty for
// everything that has to be written after the last one.
constexpr int64_t kMinimumFreeBytes = 2LL * 1000 * 1000 * 1000;

void ToggleRecording(AppState* state) {
  if (state->recorder.is_recording()) {
    state->recorder.Stop();
    state->camera.UnlockExposureAndFocus();
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "stopped: %lld written, %lld considered, %lld dropped",
                        (long long)state->recorder.written_frames(),
                        (long long)state->recorder.considered_frames(),
                        (long long)state->recorder.dropped_frames());
    return;
  }

  if (state->last_timestamp_ns == 0) {
    LogError("no frame yet; not starting");
    return;
  }

  if (state->free_bytes > 0 && state->free_bytes < kMinimumFreeBytes) {
    state->stop_reason = "NOT ENOUGH SPACE TO START";
    LogError("not enough free space to start recording");
    return;
  }

  // Locked before the first frame is kept, not at startup: by now the camera
  // has been metering this room for as long as it took to point the phone at
  // it, so what it settled on is what gets held.
  state->camera.LockExposureAndFocus(state->last_result,
                                     state->config.max_exposure_ns,
                                     state->config.mains_hz);

  if (state->recorder.Start(SessionRoot(state->app), state->last_timestamp_ns,
                            state->camera.info(), state->config)) {
    state->stop_reason = nullptr;
    __android_log_print(ANDROID_LOG_INFO, kTag, "recording to %s",
                        state->recorder.session_path().c_str());
  } else {
    LogError("could not start recording");
  }
}

// Cycles through the rear cameras.
//
// Refused while recording. Changing lens changes the intrinsics, so a session
// cannot continue across one, and ending a capture because a key was brushed
// against a coat is a trip wasted. Stopping first makes it deliberate: down to
// stop, up to change, down to start again.
void NextLens(AppState* state) {
  if (state->camera.rear_camera_count() < 2) return;

  if (state->recorder.is_recording()) {
    LogInfo("not changing lens while recording; stop first");
    return;
  }

  const std::string current = state->camera.info().id;
  state->camera.Stop();

  state->config.lens = sensor_logger::Lens::kExplicit;
  state->config.lens_id = state->camera.NextRearCameraId(current);

  if (!state->camera.Start(state->config)) {
    LogError("could not switch lens");
    return;
  }
  state->last_timestamp_ns = 0;
}

// Switches between keeping only the sharpest frame of each stretch and
// keeping every frame.
//
// Refused while recording for the same reason as the lens: SessionRecorder
// only reads `retention` when a session starts, so flipping it mid-session
// would silently keep doing whatever the session started with, and a button
// that looks like it did something but did not is worse than one that
// visibly refuses.
void ToggleRetention(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing retention while recording; stop first");
    return;
  }

  state->config.retention = state->config.retention == Retention::kAll
                                ? Retention::kSharpest
                                : Retention::kAll;
}

// Shutter speed the way it is written on a camera, since 41621860 nanoseconds
// is not a number anyone reads.
std::string Shutter(int64_t exposure_ns) {
  char buffer[24];
  if (exposure_ns <= 0) {
    std::snprintf(buffer, sizeof(buffer), "-");
  } else if (exposure_ns < 500000000) {
    std::snprintf(buffer, sizeof(buffer), "1/%lld",
                  (long long)(1000000000LL / exposure_ns));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1FS",
                  static_cast<double>(exposure_ns) / 1e9);
  }
  return buffer;
}

// The readout drawn over the preview.
//
// These are the numbers that decide whether a capture is worth keeping, and
// without them on screen they exist only in logcat, which cannot be read while
// walking around with the phone — the only time they could change anything.
std::vector<std::string> StatusLines(const AppState& state) {
  const SessionRecorder& recorder = state.recorder;
  char buffer[64];
  std::vector<std::string> lines;

  // Written to fit the narrower of the two layouts, so one set serves both: the
  // compact one spreads them across the screen and the expanded one shrinks
  // them out of the way of the picture.
  const int64_t elapsed_s = recorder.elapsed_ns() / 1000000000;

  // How long the free space lasts at the rate this capture is actually filling
  // it. The rate follows the resolution, how much of the scene is moving and
  // how many frames survive selection, so a figure worked out beforehand would
  // be wrong for the capture in hand.
  long long minutes = -1;
  if (elapsed_s > 2 && recorder.written_bytes() > 0) {
    const double per_second =
        static_cast<double>(recorder.written_bytes()) / elapsed_s;
    minutes = static_cast<long long>(state.free_bytes / per_second / 60.0);
  }

  if (recorder.is_recording()) {
    if (minutes >= 0) {
      std::snprintf(buffer, sizeof(buffer), "REC %lld:%02lld   %lld MIN LEFT",
                    (long long)(elapsed_s / 60), (long long)(elapsed_s % 60),
                    minutes);
    } else {
      std::snprintf(buffer, sizeof(buffer), "REC %lld:%02lld",
                    (long long)(elapsed_s / 60), (long long)(elapsed_s % 60));
    }
  } else if (state.stop_reason != nullptr) {
    std::snprintf(buffer, sizeof(buffer), "%s", state.stop_reason);
  } else {
    std::snprintf(buffer, sizeof(buffer), "IDLE - VOL DOWN");
  }
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "%lld KEPT / %lld SEEN",
                (long long)recorder.written_frames(),
                (long long)recorder.considered_frames());
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "%s USED  %s FREE",
                Bytes(recorder.written_bytes()).c_str(),
                Bytes(state.free_bytes).c_str());
  lines.emplace_back(buffer);

  // Each number against the threshold that would end the stretch, so the
  // readout says how close the next frame is rather than just where things
  // stand.
  std::snprintf(buffer, sizeof(buffer), "SHIFT %.0F/%.0F  DIFF %.0F/%.0F",
                recorder.last_shift() * 100.0f,
                state.config.min_shift * 100.0f,
                recorder.last_residual() * 100.0f,
                state.config.min_residual * 100.0f);
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "%dX%d %s %s",
                state.camera.capture_width(), state.camera.capture_height(),
                state.camera.LensName(),
                state.config.retention == Retention::kAll ? "ALL" : "SHARP");
  lines.emplace_back(buffer);

  // What the camera is doing with the picture, locked or not. Watching these
  // settle is how the moment to start recording is chosen, so they are shown
  // whether or not anything is being recorded.
  const CaptureResult& result = state.last_result;
  char focus[16];
  if (result.focus_distance > 0.0f) {
    std::snprintf(focus, sizeof(focus), "%.2FM", 1.0f / result.focus_distance);
  } else {
    std::snprintf(focus, sizeof(focus), "INF");
  }

  std::snprintf(buffer, sizeof(buffer), "%s ISO%d %s%s",
                Shutter(result.exposure_ns).c_str(), result.sensitivity, focus,
                state.camera.is_locked() ? " LOCKED" : "");
  lines.emplace_back(buffer);

  // Anything but zero in the first two means the capture is outrunning the
  // disk, which nothing else on screen would show.
  std::snprintf(buffer, sizeof(buffer), "DROP %lld NOIMG %lld IMU %lldK",
                (long long)recorder.dropped_frames(),
                (long long)recorder.frames_without_image(),
                (long long)(recorder.imu_samples() / 1000));
  lines.emplace_back(buffer);

  return lines;
}

void HandleCommand(android_app* app, int32_t cmd) {
  auto* state = static_cast<AppState*>(app->userData);

  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (app->window != nullptr && InitDisplay(state)) {
        StartCapture(state);
      }
      break;

    case APP_CMD_TERM_WINDOW:
      TerminateDisplay(state);
      break;

    case APP_CMD_PAUSE:
      // Stopping the session here rather than on destroy: one left open across
      // a backgrounding would be truncated with no manifest. The camera has to
      // go too — Android takes it away regardless.
      StopCapture(state);
      break;

    case APP_CMD_RESUME:
      if (state->display != EGL_NO_DISPLAY) StartCapture(state);
      break;

    default:
      break;
  }
}

}  // namespace

extern "C" void android_main(android_app* app) {
  AppState state;
  state.app = app;
  app->userData = &state;
  app->onAppCmd = HandleCommand;

  state.config.Load(FilesRoot(app));
  state.input.Attach(app);

  state.permission_granted = HasCameraPermission(app);
  if (!state.permission_granted) {
    RequestCameraPermission(app);
  }

  FrameData frame;
  CameraImageView preview_image;
  std::vector<ImuSample> imu_samples;
  std::vector<sensor_logger::CaptureResult> capture_results;

  while (true) {
    int events = 0;
    android_poll_source* source = nullptr;

    // The timeout is zero once the camera is running, so the loop paces itself
    // on frames arriving rather than on the event queue, and 50ms before that
    // so it is not spinning while waiting for a window.
    int ident = 0;
    while ((ident = ALooper_pollOnce(state.capturing ? 0 : 50, nullptr, &events,
                                     reinterpret_cast<void**>(&source))) >= 0) {
      if (source != nullptr) source->process(app, source);

      // The sensor queue has no poll source; it reports itself by identifier,
      // and pollOnce keeps returning that identifier until something reads the
      // events. Draining has to happen here rather than beside the frame step
      // below, which this loop would otherwise never reach: four hundred
      // samples a second means there is always another event pending.
      if (ident == ImuSource::kLooperIdent) {
        state.imu.Drain(&imu_samples);
        state.recorder.RecordImu(imu_samples);
      }

      if (app->destroyRequested != 0) {
        StopCapture(&state);
        TerminateDisplay(&state);
        return;
      }
    }

    if (!state.permission_granted) {
      state.permission_granted = HasCameraPermission(app);
      if (state.permission_granted && state.display != EGL_NO_DISPLAY) {
        StartCapture(&state);
      }
      continue;
    }

    if (!state.capturing) continue;

    state.camera.DrainResults(&capture_results);
    state.recorder.RecordCaptureResults(capture_results);
    if (!capture_results.empty()) state.last_result = capture_results.back();

    if (state.camera.AcquireFrame(&frame)) {
      state.last_timestamp_ns = frame.timestamp_ns;
      if (state.recorder.is_recording()) state.recorder.Record(frame);
    }

    const sensor_logger::InputEvents input = state.input.Poll(app);
    switch (input.action) {
      case Action::kToggleRecording:
        ToggleRecording(&state);
        break;
      case Action::kTogglePreview:
        state.preview_visible = !state.preview_visible;
        break;
      case Action::kNone:
        break;
    }

    if (state.recorder.is_recording()) {
      state.sessions_overlay_visible = false;
      state.pending_delete_index = -1;
    }

    if (input.touched) {
      if (state.sessions_overlay_visible) {
        if (state.preview.CloseOverlayContains(input.x, input.y)) {
          state.sessions_overlay_visible = false;
          state.pending_delete_index = -1;
        } else if (state.preview.PrevPageOverlayContains(input.x, input.y)) {
          state.sessions_page -= 1;
          state.pending_delete_index = -1;
        } else if (state.preview.NextPageOverlayContains(input.x, input.y)) {
          state.sessions_page += 1;
          state.pending_delete_index = -1;
        } else {
          int touched_idx = state.preview.ItemDeleteOverlayTouched(input.x, input.y);
          if (touched_idx >= 0) {
            if (state.pending_delete_index == touched_idx) {
              if (touched_idx < static_cast<int>(state.cached_sessions.size())) {
                const std::string& path =
                    state.cached_sessions[static_cast<size_t>(touched_idx)].full_path;
                if (SessionRecorder::DeleteSessionPath(path)) {
                  state.cached_sessions.erase(
                      state.cached_sessions.begin() + touched_idx);
                } else {
                  __android_log_print(ANDROID_LOG_WARN, kTag,
                                      "failed to delete session: %s",
                                      path.c_str());
                }
              }
              state.pending_delete_index = -1;
            } else {
              state.pending_delete_index = touched_idx;
            }
          } else {
            state.pending_delete_index = -1;
          }
        }
      } else {
        if (state.preview.LensButtonContains(input.x, input.y)) {
          NextLens(&state);
        } else if (state.preview.SessionsButtonContains(input.x, input.y)) {
          if (!state.recorder.is_recording()) {
            state.sessions_overlay_visible = !state.sessions_overlay_visible;
            state.pending_delete_index = -1;
            state.sessions_page = 0;
            if (state.sessions_overlay_visible) {
              state.cached_sessions =
                  SessionRecorder::GetSessions(SessionRoot(state.app));
            }
          }
        } else if (state.preview.RetentionButtonContains(input.x, input.y)) {
          ToggleRetention(&state);
        }
      }
    }

    // Every couple of seconds at camera rate.
    if (--state.free_space_countdown <= 0) {
      // The files directory rather than the sessions directory: the latter is
      // not created until the first session starts, and statvfs on a path that
      // does not exist reports no space at all.
      state.free_bytes = FreeBytes(FilesRoot(app));
      state.free_space_countdown = 60;

      if (state.recorder.is_recording() &&
          state.free_bytes < kMinimumFreeBytes) {
        state.recorder.Stop();
        state.stop_reason = "STOPPED - DISK FULL";
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "stopped: %lld bytes free, %lld written",
                            (long long)state.free_bytes,
                            (long long)state.recorder.written_frames());
      }
    }

    if (state.display == EGL_NO_DISPLAY) continue;

    if (state.preview_visible) {
      if (state.camera.AcquirePreviewFrame(&preview_image)) {
        state.preview.UploadCamera(preview_image);
      }
    } else {
      state.camera.DrainPreview();
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // With the picture up it fills the screen and the numbers shrink to a strip
    // over it. Without, they are the whole screen and can be twice the size.
    // Drawn only where there is something to switch to. A control that cannot
    // do anything is worse than no control: it invites a press and then does
    // nothing visible, which reads as the app being broken.
    const bool lens_choice = state.camera.rear_camera_count() > 1;

    char lens_label[32];
    std::snprintf(lens_label, sizeof(lens_label), "%s %.1FMM - TAP",
                  state.camera.LensName(), state.camera.info().focal_length_mm);

    const char* retention_label = state.config.retention == Retention::kAll
                                      ? "RETENTION ALL - TAP"
                                      : "RETENTION SHARP - TAP";

    constexpr float kGap = 0.02f;
    constexpr float kBtnHeight = 0.045f;

    if (state.preview_visible) {
      state.preview.DrawCamera(state.camera.sensor_orientation(), 0.58f, 0.05f);
      float bottom = state.preview.DrawStatus(
          StatusLines(state), state.recorder.is_recording(), 0.65f, 40);
      float btn_pos = bottom + kGap;
      if (lens_choice) {
        state.preview.DrawLensButton(lens_label, btn_pos,
                                     !state.recorder.is_recording());
        btn_pos += kBtnHeight + kGap;
      }
      state.preview.DrawSessionsButton("SESSIONS - TAP", btn_pos,
                                       !state.recorder.is_recording());
      btn_pos += kBtnHeight + kGap;
      state.preview.DrawRetentionButton(retention_label, btn_pos,
                                        !state.recorder.is_recording());
    } else {

      constexpr int kColumns = 32;
      const std::vector<std::string> lines = StatusLines(state);
      const float block =
          state.preview.StatusHeightFraction(kColumns, (int)lines.size()) +
          (lens_choice ? kGap + kBtnHeight : 0.0f) + 2 * (kGap + kBtnHeight);
      const float top = (1.0f - block) * 0.5f;

      float bottom = state.preview.DrawStatus(
          lines, state.recorder.is_recording(), top, kColumns);
      if (lens_choice) {
        state.preview.DrawLensButton(lens_label, bottom + kGap,
                                     !state.recorder.is_recording());
        bottom += kBtnHeight + kGap;
      }
      state.preview.DrawSessionsButton("SESSIONS - TAP", bottom + kGap,
                                       !state.recorder.is_recording());
      bottom += kBtnHeight + kGap;
      state.preview.DrawRetentionButton(retention_label, bottom + kGap,
                                        !state.recorder.is_recording());
    }

    if (state.sessions_overlay_visible) {
      state.preview.DrawSessionsOverlay(state.cached_sessions, state.pending_delete_index,
                                        state.sessions_page);
    }

    eglSwapBuffers(state.display, state.surface);

    static int heartbeat = 0;
    if (++heartbeat % 90 == 0) {
      __android_log_print(ANDROID_LOG_INFO, kTag,
                          "recording=%d written=%lld dropped=%lld",
                          (int)state.recorder.is_recording(),
                          (long long)state.recorder.written_frames(),
                          (long long)state.recorder.dropped_frames());
    }
  }
}
