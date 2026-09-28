#ifndef SENSOR_LOGGER_CAMERA_FPS_SELECTION_H
#define SENSOR_LOGGER_CAMERA_FPS_SELECTION_H

#include <cstdint>
#include <vector>

namespace sensor_logger {

struct FpsRange {
  int32_t min = 0;
  int32_t max = 0;
};

struct FpsSelection {
  int32_t requested_fps = 0;
  int32_t applied_fps = 0;
  int32_t max_output_fps = 0;
  int64_t min_frame_duration_ns = 0;
  bool request_supported = true;
  bool fixed_range_available = false;
};

// A fixed rate is safe to request only when Camera2 advertises that exact AE
// range and the selected output size can physically produce frames that fast.
// Zero means auto. Unsupported requests explicitly fall back to auto.
FpsSelection SelectFps(int32_t requested_fps,
                       const std::vector<FpsRange>& available_ranges,
                       int64_t min_frame_duration_ns);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_CAMERA_FPS_SELECTION_H
