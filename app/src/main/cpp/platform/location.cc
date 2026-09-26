#include "location.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace sensor_logger {

LocationData GetLocationData(android_app* app) {
  LocationData loc;
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass activity_class = env->GetObjectClass(activity);

  jstring service_name = env->NewStringUTF("location");
  jmethodID get_system_service = env->GetMethodID(
      activity_class, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  jobject loc_manager = env->CallObjectMethod(activity, get_system_service, service_name);
  env->DeleteLocalRef(service_name);

  if (loc_manager != nullptr) {
    jclass loc_manager_class = env->GetObjectClass(loc_manager);

    jstring gps_provider = env->NewStringUTF("gps");
    jstring network_provider = env->NewStringUTF("network");

    jmethodID get_last_known = env->GetMethodID(
        loc_manager_class, "getLastKnownLocation",
        "(Ljava/lang/String;)Landroid/location/Location;");

    jobject location = env->CallObjectMethod(loc_manager, get_last_known, gps_provider);
    if (location == nullptr) {
      env->ExceptionClear();
      location = env->CallObjectMethod(loc_manager, get_last_known, network_provider);
    }

    if (location != nullptr) {
      jclass loc_class = env->GetObjectClass(location);
      jmethodID get_lat = env->GetMethodID(loc_class, "getLatitude", "()D");
      jmethodID get_lon = env->GetMethodID(loc_class, "getLongitude", "()D");
      jmethodID get_alt = env->GetMethodID(loc_class, "getAltitude", "()D");
      jmethodID get_acc = env->GetMethodID(loc_class, "getAccuracy", "()F");

      loc.valid = true;
      loc.latitude = env->CallDoubleMethod(location, get_lat);
      loc.longitude = env->CallDoubleMethod(location, get_lon);
      loc.altitude_m = env->CallDoubleMethod(location, get_alt);
      loc.accuracy_m = env->CallFloatMethod(location, get_acc);

      env->DeleteLocalRef(loc_class);
      env->DeleteLocalRef(location);
    } else {
      env->ExceptionClear();
    }

    env->DeleteLocalRef(gps_provider);
    env->DeleteLocalRef(network_provider);
    env->DeleteLocalRef(loc_manager_class);
    env->DeleteLocalRef(loc_manager);
  } else {
    env->ExceptionClear();
  }

  env->DeleteLocalRef(activity_class);
  return loc;
}

}  // namespace sensor_logger
