#ifndef SENSOR_LOGGER_KEYFRAME_SELECTOR_H
#define SENSOR_LOGGER_KEYFRAME_SELECTOR_H

#include <cstdint>

namespace sensor_logger {

// Decides when a stretch of movement ends, from the numbers FrameMotion
// already measures each frame — no image data, so this runs (and is tested)
// without a camera.
//
// Picking the sharpest frame of an entire stretch against a fixed reference
// let a keyframe land anywhere from right beside the last one to well past
// the intended spacing, since nothing bounded how long the search for a
// sharper frame could run. This narrows that search to a small window
// instead: once shift or residual first gets close to target, whichever frame
// turns out sharpest within a short window afterward is the one confirmed —
// not whatever happened to be sharpest since the last keyframe.
//
// A second, independent ceiling confirms a keyframe on elapsed time alone
// when the picture never approaches target at all, which the window above
// would otherwise leave to run forever — a tripod session, a red light. That
// path always means "use whatever was sharpest since the last keyframe",
// since there is no near-target window to speak of.
class KeyframeSelector {
 public:
  // How much of target counts as "close enough" to start weighing candidates
  // for the window below, rather than the whole stretch.
  static constexpr float kApproachRatio = 0.85f;

  // How long to keep comparing candidates once shift or residual first
  // reaches target, in nanoseconds. Roughly four to six frames at the fps
  // this app records at (24-30) — enough for a neighbour of the crossing
  // frame to outrank it without stalling the writer queue behind the wait.
  static constexpr int64_t kPostCrossingWindowNs = 200000000LL;

  // Ceiling on how long a keyframe can go unconfirmed when shift and residual
  // never approach target at all. Independent of the window above: this
  // fires instead of it, not in addition to it.
  static constexpr int64_t kMaxStationaryIntervalNs = 5000000000LL;

  // Why ShouldConfirm() returned other than kNone.
  enum class ConfirmReason {
    kNone,        // keep waiting
    kWindow,      // the post-crossing window elapsed; use the window leader
    kStationary,  // the ceiling elapsed without ever nearing target
  };

  KeyframeSelector() = default;

  // Records one frame's measurement against the current reference. Call once
  // per frame, in timestamp order, before reading InWindow()/ShouldConfirm().
  void Observe(int64_t timestamp_ns, float shift, float residual,
              float target_shift, float target_residual);

  // True while shift or residual is within kApproachRatio of target, through
  // the end of the post-crossing window. A frame observed while this holds is
  // a candidate for kWindow below; one observed while it does not is not,
  // regardless of how sharp it is.
  bool InWindow() const { return in_window_; }

  ConfirmReason ShouldConfirm() const { return confirm_reason_; }

  // Tells the selector a keyframe was confirmed at timestamp_ns, so its
  // internal timers restart from there for the next stretch.
  void Confirmed(int64_t timestamp_ns);

  // Back to the state right after construction — no keyframe confirmed yet,
  // nothing observed.
  void Reset();

 private:
  bool has_last_keyframe_ = false;
  int64_t last_keyframe_ns_ = 0;
  bool crossed_ = false;
  int64_t crossed_at_ns_ = 0;
  bool in_window_ = false;
  ConfirmReason confirm_reason_ = ConfirmReason::kNone;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_KEYFRAME_SELECTOR_H
