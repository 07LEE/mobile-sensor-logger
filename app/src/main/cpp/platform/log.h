#ifndef SENSOR_LOGGER_PLATFORM_LOG_H
#define SENSOR_LOGGER_PLATFORM_LOG_H

#include <android/log.h>

namespace sensor_logger {

inline void LogInfo(const char* what) {
  __android_log_print(ANDROID_LOG_INFO, "sensor_logger", "%s", what);
}

inline void LogError(const char* what) {
  __android_log_print(ANDROID_LOG_ERROR, "sensor_logger", "%s", what);
}

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PLATFORM_LOG_H
