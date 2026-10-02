#ifndef SENSOR_LOGGER_PIPELINE_FRAME_STALL_DETECTOR_H
#define SENSOR_LOGGER_PIPELINE_FRAME_STALL_DETECTOR_H

#include <cstdint>

namespace sensor_logger {

// Notices that camera frames stopped arriving while the app is in the
// background. The OS can throttle a backgrounded camera even with the
// foreground service running, and nothing else reports it: a frame that never
// arrived is not a dropped frame.
//
// A stall is a gap of more than two frame periods since the last frame, so the
// limit follows the configured frame rate instead of being a fixed number. It
// latches until Reset, since the warning matters when the user comes back, not
// at the instant it happened.
class FrameStallDetector {
 public:
  void Reset(int64_t now_ns) {
    last_frame_ns_ = now_ns;
    stalled_ = false;
  }

  void OnFrame(int64_t now_ns) { last_frame_ns_ = now_ns; }

  void Check(int64_t now_ns, int64_t frame_period_ns) {
    if (now_ns - last_frame_ns_ > 2 * frame_period_ns) stalled_ = true;
  }

  bool stalled() const { return stalled_; }

 private:
  int64_t last_frame_ns_ = 0;
  bool stalled_ = false;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PIPELINE_FRAME_STALL_DETECTOR_H
