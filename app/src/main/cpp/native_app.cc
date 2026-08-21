#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <cstdio>
#include <string>
#include <vector>

#include "camera_image.h"
#include "camera_source.h"
#include "imu_source.h"
#include "preview_renderer.h"
#include "session_recorder.h"

namespace {

using sensor_logger::CameraImageView;
using sensor_logger::CameraSource;
using sensor_logger::FrameData;
using sensor_logger::ImuSample;
using sensor_logger::ImuSource;
using sensor_logger::PendingFrame;
using sensor_logger::PreviewRenderer;
using sensor_logger::SessionRecorder;

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
  bool capturing = false;
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

  if (!state->camera.Start()) {
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

// The readout drawn over the preview.
//
// These are the numbers that decide whether a capture is worth keeping, and
// without them on screen they exist only in logcat, which cannot be read while
// walking around with the phone — the only time they could change anything.
std::vector<std::string> StatusLines(const AppState& state) {
  const SessionRecorder& recorder = state.recorder;
  char buffer[64];
  std::vector<std::string> lines;

  if (recorder.is_recording()) {
    std::snprintf(buffer, sizeof(buffer), "RECORDING  %lld KEPT / %lld SEEN",
                  (long long)recorder.written_frames(),
                  (long long)recorder.considered_frames());
  } else {
    std::snprintf(buffer, sizeof(buffer), "IDLE - WAITING FOR THE CAMERA");
  }
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "CAPTURE %dX%d",
                state.camera.capture_width(), state.camera.capture_height());
  lines.emplace_back(buffer);

  // Movement since the last kept frame. Nothing else on the device says whether
  // the capture is covering new ground, now that there is no pose to ask.
  std::snprintf(buffer, sizeof(buffer), "SHIFT %.0F%%  DIFF %.0F%%",
                recorder.last_shift() * 100.0f,
                recorder.last_residual() * 100.0f);
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

  state.permission_granted = HasCameraPermission(app);
  if (!state.permission_granted) {
    RequestCameraPermission(app);
  }

  FrameData frame;
  CameraImageView preview_image;
  std::vector<ImuSample> imu_samples;

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

    if (state.camera.AcquireFrame(&frame)) {
      // Recording starts on its own with the first frame. There is no UI to
      // start it from, and GameActivity is not delivering touch events to the
      // native buffer, so waiting for a tap would mean capturing nothing.
      if (!state.recorder.is_recording()) {
        if (state.recorder.Start(SessionRoot(app), frame.timestamp_ns,
                                 state.camera.sensor_orientation())) {
          __android_log_print(ANDROID_LOG_INFO, kTag, "recording to %s",
                              state.recorder.session_path().c_str());
        } else {
          LogError("could not start recording");
        }
      }

      state.recorder.Record(frame);
    }

    if (state.display == EGL_NO_DISPLAY) continue;

    if (state.camera.AcquirePreviewFrame(&preview_image)) {
      state.preview.UploadCamera(preview_image);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    state.preview.DrawCamera(state.camera.sensor_orientation());
    state.preview.DrawStatus(StatusLines(state),
                             state.recorder.is_recording());

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
