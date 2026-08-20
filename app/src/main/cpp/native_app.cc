#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <memory>
#include <string>

#include "ar_session.h"
#include "session_recorder.h"

namespace {

using sensor_logger::ArSession;
using sensor_logger::FrameData;
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

  return true;
}

void TerminateDisplay(AppState* state) {
  state->ar_session.Pause();
  state->ar_started = false;

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
}

void ToggleRecording(AppState* state, const FrameData& frame) {
  if (state->recorder.is_recording()) {
    state->recorder.Stop();
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "stopped: %lld recorded, %lld too close, %lld untracked, "
        "%lld without image",
        static_cast<long long>(state->recorder.recorded_frames()),
        static_cast<long long>(state->recorder.skipped_frames()),
        static_cast<long long>(state->recorder.dropped_frames()),
        static_cast<long long>(state->recorder.frames_without_image()));
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

  state.permission_granted = HasCameraPermission(app);
  if (!state.permission_granted) {
    RequestCameraPermission(app);
  }

  FrameData frame;

  while (true) {
    int events = 0;
    android_poll_source* source = nullptr;

    // Zero timeout while running so the loop stays tied to the camera through
    // ArSession_update's blocking mode rather than to the event queue.
    const int timeout = state.ar_started ? 0 : -1;
    while (ALooper_pollOnce(timeout, nullptr, &events,
                            reinterpret_cast<void**>(&source)) >= 0) {
      if (source != nullptr) source->process(app, source);
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

    if (ConsumeTap(app)) ToggleRecording(&state, frame);

    eglSwapBuffers(state.display, state.surface);
  }
}
