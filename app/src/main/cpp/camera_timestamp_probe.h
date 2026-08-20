#ifndef SENSOR_LOGGER_CAMERA_TIMESTAMP_PROBE_H
#define SENSOR_LOGGER_CAMERA_TIMESTAMP_PROBE_H

namespace sensor_logger {

// Logs each camera's ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE.
//
// Recorded because it decides whether a capture built on Camera2 plus the IMU
// is possible at all on a given device: only REALTIME puts camera frames and
// inertial samples on one clock. It is per-device and undocumented by vendors,
// so the only way to know is to ask the device.
void LogCameraTimestampSource();

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_CAMERA_TIMESTAMP_PROBE_H
