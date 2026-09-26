#include "permissions.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "log.h"

namespace sensor_logger {

// Runtime permission check, through JNI because there is no native entry
// point for it.
bool HasPermission(android_app* app, const char* permission) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass context_class = env->GetObjectClass(activity);
  jmethodID check_permission = env->GetMethodID(
      context_class, "checkSelfPermission", "(Ljava/lang/String;)I");

  jstring permission_str = env->NewStringUTF(permission);
  const jint result = env->CallIntMethod(activity, check_permission, permission_str);

  env->DeleteLocalRef(permission_str);
  env->DeleteLocalRef(context_class);

  // PackageManager.PERMISSION_GRANTED
  return result == 0;
}

bool HasCameraPermission(android_app* app) {
  return HasPermission(app, "android.permission.CAMERA");
}

// POST_NOTIFICATIONS and the location permissions are requested alongside
// CAMERA (see RequestCameraPermission below) but nothing else ever checks
// whether they actually landed. A denial otherwise shows up only as the
// background-recording notification silently never appearing, or every
// session's location fields silently staying null — logged once, when camera
// permission first comes back granted, so a denial is at least diagnosable
// from logcat instead of indistinguishable from "no fix yet".
void LogOptionalPermissions(android_app* app) {
  if (!HasPermission(app, "android.permission.POST_NOTIFICATIONS")) {
    LogInfo("POST_NOTIFICATIONS denied; the background-recording notification will not show");
  }
  if (!HasPermission(app, "android.permission.ACCESS_FINE_LOCATION") &&
      !HasPermission(app, "android.permission.ACCESS_COARSE_LOCATION")) {
    LogInfo("location permissions denied; sessions will have no start/end location");
  }
}

void RequestCameraPermission(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID request = env->GetMethodID(activity_class, "requestPermissions",
                                       "([Ljava/lang/String;I)V");

  jclass string_class = env->FindClass("java/lang/String");
  jobjectArray permissions = env->NewObjectArray(4, string_class, nullptr);
  jstring cam_perm = env->NewStringUTF("android.permission.CAMERA");
  jstring notif_perm = env->NewStringUTF("android.permission.POST_NOTIFICATIONS");
  jstring fine_loc = env->NewStringUTF("android.permission.ACCESS_FINE_LOCATION");
  jstring coarse_loc = env->NewStringUTF("android.permission.ACCESS_COARSE_LOCATION");

  env->SetObjectArrayElement(permissions, 0, cam_perm);
  env->SetObjectArrayElement(permissions, 1, notif_perm);
  env->SetObjectArrayElement(permissions, 2, fine_loc);
  env->SetObjectArrayElement(permissions, 3, coarse_loc);

  env->CallVoidMethod(activity, request, permissions, 0);

  env->DeleteLocalRef(cam_perm);
  env->DeleteLocalRef(notif_perm);
  env->DeleteLocalRef(fine_loc);
  env->DeleteLocalRef(coarse_loc);
  env->DeleteLocalRef(permissions);
  env->DeleteLocalRef(string_class);
  env->DeleteLocalRef(activity_class);
}

}  // namespace sensor_logger
