#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ar_session.h"
#include "camera_timestamp_probe.h"
#include "imu_source.h"
#include "preview_renderer.h"
#include "session_recorder.h"

namespace {

using sensor_logger::ArSession;
using sensor_logger::FrameData;
using sensor_logger::ImuSample;
using sensor_logger::ImuSource;
using sensor_logger::PendingFrame;
using sensor_logger::PreviewRenderer;
using sensor_logger::SessionRecorder;

constexpr char kTag[] = "sensor_logger";
constexpr GLenum kTextureExternalOes = 0x8D65;

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
  GLuint camera_texture = 0;
  int width = 0;
  int height = 0;

  ArSession ar_session;
  ImuSource imu;
  PreviewRenderer preview;
  SessionRecorder recorder;
  bool ar_started = false;
  bool permission_granted = false;
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

std::string SessionRoot(android_app* app) {
  const char* base = app->activity->externalDataPath != nullptr
                         ? app->activity->externalDataPath
                         : app->activity->internalDataPath;
  return std::string(base) + "/sessions";
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
                                   EGL_DEPTH_SIZE,
                                   16,
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

  // ARCore writes the camera image into an external OES texture.
  glGenTextures(1, &state->camera_texture);
  glBindTexture(kTextureExternalOes, state->camera_texture);
  glTexParameteri(kTextureExternalOes, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(kTextureExternalOes, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  if (!state->preview.Init()) {
    LogError("could not build the preview shaders");
    return false;
  }
  state->preview.SetViewport(state->width, state->height);

  return true;
}

void TerminateDisplay(AppState* state) {
  state->ar_session.Pause();
  state->ar_started = false;

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
  state->camera_texture = 0;
}

void StartAr(AppState* state) {
  if (state->ar_started) return;

  JNIEnv* env = nullptr;
  state->app->activity->vm->AttachCurrentThread(&env, nullptr);

  if (!state->ar_session.IsValid()) {
    if (!state->ar_session.Create(env, state->app->activity->javaGameActivity)) {
      LogError("could not create ARCore session");
      return;
    }
  }

  state->ar_session.SetCameraTexture(state->camera_texture);
  state->ar_session.SetDisplayGeometry(0, state->width, state->height);

  if (!state->ar_session.Resume()) {
    LogError("could not resume ARCore session");
    return;
  }

  state->ar_started = true;
  LogInfo("ARCore session running");

  // Inertial capture starts with the AR session, not with the app. Both stay
  // running for the whole session, independent of whether ARCore is tracking:
  // the samples covering a gap are exactly the ones that could bridge it later.
  //
  // Starting earlier would stall the main loop. The sensor queue shares the
  // looper, and until the AR session exists that loop waits on the looper with
  // a timeout; a stream of sensor events keeps it there, and the camera
  // permission is only re-checked once it comes back out.
  state->imu.Start(state->app->looper, "com.sensor.logger");
}

void ToggleRecording(AppState* state, const FrameData& frame) {
  if (state->recorder.is_recording()) {
    state->recorder.Stop();
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "stopped: %lld written from %lld considered, %lld untracked, "
        "%lld without image, %lld dropped, %lld imu samples",
        static_cast<long long>(state->recorder.written_frames()),
        static_cast<long long>(state->recorder.considered_frames()),
        static_cast<long long>(state->recorder.untracked_frames()),
        static_cast<long long>(state->recorder.frames_without_image()),
        static_cast<long long>(state->recorder.dropped_frames()),
        static_cast<long long>(state->recorder.imu_samples()));
    return;
  }

  if (state->recorder.Start(SessionRoot(state->app), frame.timestamp_ns)) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "recording to %s",
                        state->recorder.session_path().c_str());
  } else {
    LogError("could not start recording");
  }
}

void HandleCommand(android_app* app, int32_t cmd) {
  auto* state = static_cast<AppState*>(app->userData);

  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (app->window != nullptr && InitDisplay(state)) {
        if (state->permission_granted) StartAr(state);
      }
      break;

    case APP_CMD_TERM_WINDOW:
      TerminateDisplay(state);
      break;

    case APP_CMD_PAUSE:
      // Stopping here rather than on destroy: a session left open across a
      // backgrounding would be truncated with no manifest.
      state->recorder.Stop();
      state->ar_session.Pause();
      state->ar_started = false;
      break;

    case APP_CMD_RESUME:
      if (state->display != EGL_NO_DISPLAY && state->permission_granted) {
        StartAr(state);
      }
      break;

    default:
      break;
  }
}

// The readout drawn over the preview.
//
// These are the numbers that decide whether a capture is worth keeping, and
// until now they existed only in logcat — which is not readable while walking
// around with the phone, which is the only time they could change anything.
std::vector<std::string> StatusLines(const AppState& state,
                                     const FrameData& frame) {
  const SessionRecorder& recorder = state.recorder;
  char buffer[64];
  std::vector<std::string> lines;

  if (recorder.is_recording()) {
    std::snprintf(buffer, sizeof(buffer), "RECORDING  %lld KEPT / %lld SEEN",
                  (long long)recorder.written_frames(),
                  (long long)recorder.considered_frames());
  } else {
    std::snprintf(buffer, sizeof(buffer), "IDLE - WAITING FOR TRACKING");
  }
  lines.emplace_back(buffer);

  if (frame.is_tracking) {
    lines.emplace_back("TRACKING");
  } else {
    std::snprintf(buffer, sizeof(buffer), "NO TRACKING: %s",
                  frame.tracking_failure != nullptr ? frame.tracking_failure
                                                    : "STARTING UP");
    lines.emplace_back(buffer);
  }

  // The step is what to act on while filming: it is how far to move for the
  // next viewpoint, and it follows how far away the scene is rather than being
  // a fixed number to memorise.
  //
  // Marked when nothing measured it. Captures have come back with the point
  // cloud empty for whole sessions, and a fallback distance printed as though
  // it had been measured is worse than no number: it looks like the step is
  // tracking the scene when it is really a constant.
  if (recorder.scene_distance_m() > 0.0f) {
    std::snprintf(buffer, sizeof(buffer), "SCENE %.1FM  STEP %.0FCM  PTS %d",
                  recorder.scene_distance_m(),
                  recorder.translation_threshold_m() * 100.0f,
                  (int)frame.point_cloud.size());
  } else {
    std::snprintf(buffer, sizeof(buffer), "SCENE ASSUMED  STEP %.0FCM  PTS 0",
                  recorder.translation_threshold_m() * 100.0f);
  }
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "IMU %lld SAMPLES",
                (long long)recorder.imu_samples());
  lines.emplace_back(buffer);

  // Anything but zero here means the capture is outrunning the disk, which
  // nothing else on screen would show.
  std::snprintf(buffer, sizeof(buffer), "DROPPED %lld  NO IMAGE %lld",
                (long long)recorder.dropped_frames(),
                (long long)recorder.frames_without_image());
  lines.emplace_back(buffer);

  return lines;
}

// Any tap toggles recording. A real UI comes later; this keeps the capture
// path exercisable without one.
bool ConsumeTap(android_app* app) {
  auto* input = android_app_swap_input_buffers(app);
  if (input == nullptr) return false;

  bool tapped = false;
  for (uint64_t i = 0; i < input->motionEventsCount; ++i) {
    const GameActivityMotionEvent& event = input->motionEvents[i];
    if ((event.action & AMOTION_EVENT_ACTION_MASK) ==
        AMOTION_EVENT_ACTION_DOWN) {
      tapped = true;
    }
  }
  android_app_clear_motion_events(input);
  return tapped;
}

}  // namespace

extern "C" void android_main(android_app* app) {
  AppState state;
  state.app = app;
  app->userData = &state;
  app->onAppCmd = HandleCommand;

  // Null filter means every motion event reaches the input buffer. The default
  // one admits only touchscreen sources, which is not what arrives here.
  android_app_set_motion_event_filter(app, nullptr);

  sensor_logger::LogCameraTimestampSource();

  state.permission_granted = HasCameraPermission(app);
  if (!state.permission_granted) {
    RequestCameraPermission(app);
  }

  FrameData frame;
  std::vector<ImuSample> imu_samples;

  while (true) {
    int events = 0;
    android_poll_source* source = nullptr;

    // The timeout is re-evaluated on every call, not hoisted: the window
    // arrives and AR starts inside this drain loop, and a value captured
    // beforehand would leave the loop waiting forever afterwards. Nothing would
    // wake it either, because GameActivity delivers input through its own
    // buffer rather than the looper.
    //
    // Once AR is running the timeout is zero, so the loop paces itself on
    // ArSession_update's blocking mode instead of the event queue.
    int ident = 0;
    while ((ident = ALooper_pollOnce(state.ar_started ? 0 : 50, nullptr, &events,
                                     reinterpret_cast<void**>(&source))) >= 0) {
      if (source != nullptr) source->process(app, source);

      // The sensor queue has no poll source; it reports itself by identifier,
      // and pollOnce keeps returning that identifier until something reads the
      // events. Draining has to happen here rather than beside the frame step
      // below, which this loop would otherwise never reach: four hundred
      // samples a second means there is always another event pending, so the
      // loop spins forever and no frame is ever captured.
      if (ident == ImuSource::kLooperIdent) {
        state.imu.Drain(&imu_samples);
        state.recorder.RecordImu(imu_samples);
      }

      if (app->destroyRequested != 0) {
        state.recorder.Stop();
        TerminateDisplay(&state);
        return;
      }
    }

    if (!state.permission_granted) {
      state.permission_granted = HasCameraPermission(app);
      if (state.permission_granted && state.display != EGL_NO_DISPLAY) {
        StartAr(&state);
      }
      continue;
    }

    if (!state.ar_started) continue;

    if (state.ar_session.Update(&frame)) {
      state.recorder.Record(frame);
    }

    // Recording starts on its own once ARCore is tracking. There is no UI yet
    // to start it from, and GameActivity is not delivering touch events to the
    // native buffer, so waiting for a tap would mean never capturing anything.
    if (frame.is_tracking && !state.recorder.is_recording()) {
      ToggleRecording(&state, frame);
    }

    static int heartbeat = 0;
    if (++heartbeat % 90 == 0) {
      __android_log_print(
          ANDROID_LOG_INFO, kTag,
          "tracking=%d recording=%d written=%lld dropped=%lld",
          (int)frame.is_tracking, (int)state.recorder.is_recording(),
          (long long)state.recorder.written_frames(),
          (long long)state.recorder.dropped_frames());
    }

    if (ConsumeTap(app)) ToggleRecording(&state, frame);

    state.preview.DrawCamera(state.camera_texture, frame.background_uvs.data());
    state.preview.DrawStatus(StatusLines(state, frame),
                             state.recorder.is_recording());

    eglSwapBuffers(state.display, state.surface);
  }
}
