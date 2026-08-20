#include "camera_timestamp_probe.h"

#include <android/log.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

}  // namespace

void LogCameraTimestampSource() {
  ACameraManager* manager = ACameraManager_create();
  if (manager == nullptr) return;

  ACameraIdList* ids = nullptr;
  if (ACameraManager_getCameraIdList(manager, &ids) != ACAMERA_OK ||
      ids == nullptr) {
    ACameraManager_delete(manager);
    return;
  }

  for (int i = 0; i < ids->numCameras; ++i) {
    ACameraMetadata* characteristics = nullptr;
    if (ACameraManager_getCameraCharacteristics(manager, ids->cameraIds[i],
                                                &characteristics) != ACAMERA_OK) {
      continue;
    }

    ACameraMetadata_const_entry facing{};
    ACameraMetadata_getConstEntry(characteristics, ACAMERA_LENS_FACING, &facing);

    ACameraMetadata_const_entry source{};
    const bool have_source =
        ACameraMetadata_getConstEntry(characteristics,
                                      ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE,
                                      &source) == ACAMERA_OK &&
        source.count > 0;

    // REALTIME means camera timestamps share a clock with the sensors, which is
    // what any camera-plus-IMU capture depends on. UNKNOWN means the camera
    // keeps its own, and nothing in software can align the two.
    const char* name = "unavailable";
    if (have_source) {
      name = source.data.u8[0] == ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME
                 ? "REALTIME"
                 : "UNKNOWN";
    }

    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "camera %s (facing=%d) timestamp source: %s",
                        ids->cameraIds[i],
                        facing.count > 0 ? facing.data.u8[0] : -1, name);

    ACameraMetadata_free(characteristics);
  }

  ACameraManager_deleteCameraIdList(ids);
  ACameraManager_delete(manager);
}

}  // namespace sensor_logger
