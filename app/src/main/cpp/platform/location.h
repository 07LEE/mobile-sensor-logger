#ifndef SENSOR_LOGGER_PLATFORM_LOCATION_H
#define SENSOR_LOGGER_PLATFORM_LOCATION_H

struct android_app;

namespace sensor_logger {

struct LocationData {
  bool valid = false;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_m = 0.0;
  float accuracy_m = 0.0f;
};

// The last known fix, from GPS and then the network provider. `valid` is false
// when there is none or the permission was denied.
LocationData GetLocationData(android_app* app);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PLATFORM_LOCATION_H
