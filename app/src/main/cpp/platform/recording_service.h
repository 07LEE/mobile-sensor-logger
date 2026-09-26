#ifndef SENSOR_LOGGER_PLATFORM_RECORDING_SERVICE_H
#define SENSOR_LOGGER_PLATFORM_RECORDING_SERVICE_H

struct android_app;

namespace sensor_logger {

// Starts and stops RecordingService, the foreground service that keeps a
// recording alive in the background.
void StartRecordingService(android_app* app);
void StopRecordingService(android_app* app);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PLATFORM_RECORDING_SERVICE_H
