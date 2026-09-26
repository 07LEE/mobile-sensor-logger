#include "recording_service.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "log.h"

namespace sensor_logger {
namespace {

jclass LoadRecordingServiceClass(JNIEnv* env, jobject activity) {
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID get_class_loader = env->GetMethodID(
      activity_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject class_loader = env->CallObjectMethod(activity, get_class_loader);
  jclass class_loader_class = env->GetObjectClass(class_loader);
  jmethodID load_class = env->GetMethodID(
      class_loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

  jstring class_name = env->NewStringUTF("com.sensor.logger.RecordingService");
  jclass service_class = static_cast<jclass>(
      env->CallObjectMethod(class_loader, load_class, class_name));

  env->DeleteLocalRef(class_name);
  env->DeleteLocalRef(class_loader_class);
  env->DeleteLocalRef(class_loader);
  env->DeleteLocalRef(activity_class);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return nullptr;
  }
  return service_class;
}

// Cached as a global ref after the first successful resolution. A jclass
// local ref from LoadRecordingServiceClass is only valid for the JNIEnv call
// that produced it, but Start/StopRecordingService look this up on every
// single recording start and stop — a full getClassLoader()->loadClass()
// reflection round trip each time for something that never changes at
// runtime.
jclass CachedRecordingServiceClass(JNIEnv* env, jobject activity) {
  static jclass cached = nullptr;
  if (cached != nullptr) return cached;

  jclass local = LoadRecordingServiceClass(env, activity);
  if (local == nullptr) return nullptr;

  cached = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  return cached;
}

// Builds `new Intent(activity, service_class)`. Caller deletes the local ref.
jobject BuildServiceIntent(JNIEnv* env, jobject activity, jclass service_class) {
  jclass intent_class = env->FindClass("android/content/Intent");
  jmethodID intent_init = env->GetMethodID(
      intent_class, "<init>", "(Landroid/content/Context;Ljava/lang/Class;)V");
  jobject intent = env->NewObject(intent_class, intent_init, activity, service_class);
  env->DeleteLocalRef(intent_class);
  return intent;
}

}  // namespace

void StartRecordingService(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass activity_class = env->GetObjectClass(activity);
  jclass service_class = CachedRecordingServiceClass(env, activity);

  if (service_class != nullptr) {
    jobject intent = BuildServiceIntent(env, activity, service_class);

    jmethodID start_service = env->GetMethodID(
        activity_class, "startForegroundService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;");
    if (start_service == nullptr) {
      env->ExceptionClear();
      start_service = env->GetMethodID(
          activity_class, "startService",
          "(Landroid/content/Intent;)Landroid/content/ComponentName;");
    }

    if (start_service != nullptr) {
      env->CallObjectMethod(activity, start_service, intent);
      if (env->ExceptionCheck()) {
        // E.g. ForegroundServiceStartNotAllowedException on Android 12+ when
        // the process is in a restricted background state. Clear it before
        // any further JNI call on this thread: leaving one pending is
        // undefined behavior per the JNI spec, not just for this call.
        env->ExceptionClear();
        LogError("startForegroundService threw");
      }
    } else {
      env->ExceptionClear();
    }

    env->DeleteLocalRef(intent);
  }

  env->DeleteLocalRef(activity_class);
}

void StopRecordingService(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass activity_class = env->GetObjectClass(activity);
  jclass service_class = CachedRecordingServiceClass(env, activity);

  if (service_class != nullptr) {
    jobject intent = BuildServiceIntent(env, activity, service_class);

    jfieldID action_stop_field = env->GetStaticFieldID(
        service_class, "ACTION_STOP", "Ljava/lang/String;");
    if (action_stop_field != nullptr) {
      jstring action_stop = static_cast<jstring>(
          env->GetStaticObjectField(service_class, action_stop_field));
      jclass intent_class = env->GetObjectClass(intent);
      jmethodID set_action = env->GetMethodID(
          intent_class, "setAction",
          "(Ljava/lang/String;)Landroid/content/Intent;");
      env->CallObjectMethod(intent, set_action, action_stop);
      env->DeleteLocalRef(action_stop);
      env->DeleteLocalRef(intent_class);
    } else {
      // Field lookup failed — e.g. RecordingService was resolved but no
      // longer defines ACTION_STOP. Clear the pending exception before any
      // further JNI call: leaving one set is undefined behavior per the JNI
      // spec, not just for this call but for whatever runs next.
      env->ExceptionClear();
    }

    jmethodID start_service = env->GetMethodID(
        activity_class, "startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;");
    if (start_service != nullptr) {
      env->CallObjectMethod(activity, start_service, intent);
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
        LogError("startService (stop) threw");
      }
    }

    env->DeleteLocalRef(intent);
  }

  env->DeleteLocalRef(activity_class);
}

}  // namespace sensor_logger
