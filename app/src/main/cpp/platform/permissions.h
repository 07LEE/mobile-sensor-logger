#ifndef SENSOR_LOGGER_PLATFORM_PERMISSIONS_H
#define SENSOR_LOGGER_PLATFORM_PERMISSIONS_H

struct android_app;

namespace sensor_logger {

// Runtime permission check, through JNI because there is no native entry
// point for it.
bool HasPermission(android_app* app, const char* permission);

bool HasCameraPermission(android_app* app);

// Logs a denial of the permissions requested alongside CAMERA, so it is
// diagnosable from logcat.
void LogOptionalPermissions(android_app* app);

// Asks for CAMERA together with POST_NOTIFICATIONS and the location
// permissions.
void RequestCameraPermission(android_app* app);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PLATFORM_PERMISSIONS_H
