#include "fps_selection.h"

#include <algorithm>

namespace sensor_logger {

FpsSelection SelectFps(int32_t requested_fps,
                       const std::vector<FpsRange>& available_ranges,
                       int64_t min_frame_duration_ns) {
  FpsSelection result;
  result.requested_fps = std::max(requested_fps, 0);
  result.min_frame_duration_ns = std::max<int64_t>(min_frame_duration_ns, 0);
  if (result.min_frame_duration_ns > 0) {
    result.max_output_fps =
        static_cast<int32_t>(1'000'000'000LL / result.min_frame_duration_ns);
  }
  if (result.requested_fps == 0) return result;

  for (const FpsRange& range : available_ranges) {
    if (range.min == result.requested_fps &&
        range.max == result.requested_fps) {
      result.fixed_range_available = true;
      break;
    }
  }

  const bool output_fast_enough =
      result.min_frame_duration_ns > 0 &&
      result.min_frame_duration_ns * result.requested_fps <= 1'000'000'000LL;
  result.request_supported =
      result.fixed_range_available && output_fast_enough;
  result.applied_fps = result.request_supported ? result.requested_fps : 0;
  return result;
}

}  // namespace sensor_logger
