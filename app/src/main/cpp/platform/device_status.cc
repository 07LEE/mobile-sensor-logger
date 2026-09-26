#include "device_status.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace sensor_logger {

ThermalSample ReadThermalSample(android_app* app) {
  ThermalSample out;
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  jobject activity = app->activity->javaGameActivity;
  jclass activity_class = env->GetObjectClass(activity);
  jclass context_class = env->FindClass("android/content/Context");

  // Battery temperature: the sticky ACTION_BATTERY_CHANGED broadcast,
  // fetched by registering a null receiver, which returns the last broadcast
  // immediately instead of waiting for the next one.
  jclass intent_filter_class = env->FindClass("android/content/IntentFilter");
  jmethodID intent_filter_init =
      env->GetMethodID(intent_filter_class, "<init>", "(Ljava/lang/String;)V");
  jstring battery_action =
      env->NewStringUTF("android.intent.action.BATTERY_CHANGED");
  jobject filter =
      env->NewObject(intent_filter_class, intent_filter_init, battery_action);

  jmethodID register_receiver = env->GetMethodID(
      activity_class, "registerReceiver",
      "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)"
      "Landroid/content/Intent;");
  jobject battery_intent =
      env->CallObjectMethod(activity, register_receiver, nullptr, filter);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    battery_intent = nullptr;
  }

  if (battery_intent != nullptr) {
    jclass battery_manager_class = env->FindClass("android/os/BatteryManager");
    jfieldID extra_temp_field = env->GetStaticFieldID(
        battery_manager_class, "EXTRA_TEMPERATURE", "Ljava/lang/String;");
    jstring extra_temp_key = static_cast<jstring>(
        env->GetStaticObjectField(battery_manager_class, extra_temp_field));
    jfieldID extra_level_field = env->GetStaticFieldID(
        battery_manager_class, "EXTRA_LEVEL", "Ljava/lang/String;");
    jstring extra_level_key = static_cast<jstring>(
        env->GetStaticObjectField(battery_manager_class, extra_level_field));
    jfieldID extra_scale_field = env->GetStaticFieldID(
        battery_manager_class, "EXTRA_SCALE", "Ljava/lang/String;");
    jstring extra_scale_key = static_cast<jstring>(
        env->GetStaticObjectField(battery_manager_class, extra_scale_field));

    jclass intent_class = env->GetObjectClass(battery_intent);
    jmethodID get_int_extra = env->GetMethodID(
        intent_class, "getIntExtra", "(Ljava/lang/String;I)I");
    const jint temp_tenths =
        env->CallIntMethod(battery_intent, get_int_extra, extra_temp_key, -1);
    if (temp_tenths >= 0) out.battery_temp_c = temp_tenths / 10.0f;

    // level/scale rather than a direct percentage — Android's own battery
    // icon does this arithmetic itself, there is no EXTRA_PERCENT.
    const jint level =
        env->CallIntMethod(battery_intent, get_int_extra, extra_level_key, -1);
    const jint scale =
        env->CallIntMethod(battery_intent, get_int_extra, extra_scale_key, -1);
    if (level >= 0 && scale > 0) {
      out.battery_percent = level * 100 / scale;
    }

    env->DeleteLocalRef(extra_temp_key);
    env->DeleteLocalRef(extra_level_key);
    env->DeleteLocalRef(extra_scale_key);
    env->DeleteLocalRef(battery_manager_class);
    env->DeleteLocalRef(intent_class);
    env->DeleteLocalRef(battery_intent);
  }
  env->DeleteLocalRef(filter);
  env->DeleteLocalRef(battery_action);
  env->DeleteLocalRef(intent_filter_class);

  // Thermal status: PowerManager.getCurrentThermalStatus(), API 29+ only.
  jclass build_version_class = env->FindClass("android/os/Build$VERSION");
  jfieldID sdk_int_field =
      env->GetStaticFieldID(build_version_class, "SDK_INT", "I");
  const jint sdk_int = env->GetStaticIntField(build_version_class, sdk_int_field);
  env->DeleteLocalRef(build_version_class);

  if (sdk_int >= 29) {
    jfieldID power_service_field = env->GetStaticFieldID(
        context_class, "POWER_SERVICE", "Ljava/lang/String;");
    jstring power_service = static_cast<jstring>(
        env->GetStaticObjectField(context_class, power_service_field));

    jmethodID get_system_service = env->GetMethodID(
        activity_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject power_manager =
        env->CallObjectMethod(activity, get_system_service, power_service);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      power_manager = nullptr;
    }

    if (power_manager != nullptr) {
      jclass power_manager_class = env->GetObjectClass(power_manager);
      jmethodID get_thermal_status = env->GetMethodID(
          power_manager_class, "getCurrentThermalStatus", "()I");
      if (get_thermal_status != nullptr) {
        out.thermal_status =
            env->CallIntMethod(power_manager, get_thermal_status);
      } else {
        env->ExceptionClear();
      }
      env->DeleteLocalRef(power_manager_class);
      env->DeleteLocalRef(power_manager);
    }
    env->DeleteLocalRef(power_service);
  }

  env->DeleteLocalRef(context_class);
  env->DeleteLocalRef(activity_class);
  return out;
}

}  // namespace sensor_logger
