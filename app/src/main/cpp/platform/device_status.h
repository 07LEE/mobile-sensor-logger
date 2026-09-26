#ifndef SENSOR_LOGGER_PLATFORM_DEVICE_STATUS_H
#define SENSOR_LOGGER_PLATFORM_DEVICE_STATUS_H

#include <cstdint>

struct android_app;

namespace sensor_logger {

// Device thermal and battery state (see ADR 9 for the thermal half). All
// three readings are Java-only; there is no NDK equivalent for PowerManager
// or BatteryManager. Read fresh each call rather than cached, since the point
// is watching them change — through a recording for the first two, and
// continuously for battery_percent, which is shown on the HUD whether or not
// anything is recording.
struct ThermalSample {
  int32_t thermal_status = -1;      // PowerManager's enum; -1 below API 29.
  float battery_temp_c = -1000.0f;  // Sentinel: unavailable/unreadable.
  int32_t battery_percent = -1;     // Sentinel: unavailable/unreadable.
};

ThermalSample ReadThermalSample(android_app* app);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PLATFORM_DEVICE_STATUS_H
