#include "keyframe_selector.h"

namespace sensor_logger {

void KeyframeSelector::Observe(int64_t timestamp_ns, float shift, float residual,
                               float target_shift, float target_residual) {
  if (!has_last_keyframe_) {
    last_keyframe_ns_ = timestamp_ns;
    has_last_keyframe_ = true;
  }

  const bool crossing_now =
      shift >= target_shift || residual >= target_residual;
  const bool approaching =
      shift >= target_shift * kApproachRatio ||
      residual >= target_residual * kApproachRatio;

  if (!crossed_ && crossing_now) {
    crossed_ = true;
    crossed_at_ns_ = timestamp_ns;
  }

  in_window_ = crossed_ ? (timestamp_ns - crossed_at_ns_ <= kPostCrossingWindowNs)
                        : approaching;

  if (crossed_ && timestamp_ns - crossed_at_ns_ > kPostCrossingWindowNs) {
    confirm_reason_ = ConfirmReason::kWindow;
  } else if (!crossed_ &&
            timestamp_ns - last_keyframe_ns_ >= kMaxStationaryIntervalNs) {
    confirm_reason_ = ConfirmReason::kStationary;
  } else {
    confirm_reason_ = ConfirmReason::kNone;
  }
}

void KeyframeSelector::Confirmed(int64_t timestamp_ns) {
  last_keyframe_ns_ = timestamp_ns;
  has_last_keyframe_ = true;
  crossed_ = false;
  crossed_at_ns_ = 0;
  in_window_ = false;
  confirm_reason_ = ConfirmReason::kNone;
}

void KeyframeSelector::Reset() {
  has_last_keyframe_ = false;
  last_keyframe_ns_ = 0;
  crossed_ = false;
  crossed_at_ns_ = 0;
  in_window_ = false;
  confirm_reason_ = ConfirmReason::kNone;
}

}  // namespace sensor_logger
