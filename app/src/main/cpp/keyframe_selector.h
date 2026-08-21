#ifndef SENSOR_LOGGER_KEYFRAME_SELECTOR_H
#define SENSOR_LOGGER_KEYFRAME_SELECTOR_H

#include "ar_session.h"

namespace sensor_logger {

// Decides where one stretch of movement ends and the next begins.
//
// This does not pick the frame to keep — SessionRecorder writes the sharpest
// frame of each stretch, not the one that closed it, because the frame that
// happens to cross the threshold is as likely to be blurred as any other. What
// this bounds is how far apart written viewpoints end up.
//
// Sideways movement is measured as the angle it turns the scene through, not as
// a distance. Distance alone cannot tell the two cases apart: five centimetres
// beside a desk is a genuinely different angle on the subject, and five
// centimetres beside a far wall is the same photograph. A threshold in metres
// therefore over-samples across a room and under-samples up close — exactly
// backwards, since the close-up work is where detail has to survive and the
// walk across a room is where the disk fills up.
//
// Turning in place is a separate criterion, because it changes what is in view
// while producing no parallax at all.
//
// Holding the phone still never closes a stretch, so nothing accumulates.
class KeyframeSelector {
 public:
  // Defaults are a starting point, not a measured optimum. Wider baselines
  // reconstruct from fewer frames but eventually stop matching at all, and
  // where that line falls has not been tested against a real capture.
  static constexpr float kDefaultMinParallaxRad = 0.09f;  // ~5 degrees
  static constexpr float kDefaultMinRotationRad = 0.10f;  // ~5.7 degrees

  // How far away the scene is assumed to be before ARCore has produced any
  // feature points to measure it from.
  static constexpr float kAssumedSceneDistanceM = 2.0f;

  // The measured distance is clamped into this range. A cloud of a few points
  // on a blank wall, or one stray point metres behind everything else, would
  // otherwise move the threshold by an order of magnitude.
  static constexpr float kMinSceneDistanceM = 0.15f;
  static constexpr float kMaxSceneDistanceM = 15.0f;

  KeyframeSelector() = default;

  KeyframeSelector(float min_parallax_rad, float min_rotation_rad)
      : min_parallax_rad_(min_parallax_rad),
        min_rotation_rad_(min_rotation_rad) {}

  // True when `pose` is far enough from the last accepted one to start a new
  // stretch, which also moves the reference forward.
  //
  // `scene_distance_m` is how far away what the camera is looking at is, or 0
  // when that is not known this frame; the last known value carries over, since
  // the scene does not usually change distance in the frame the point cloud
  // happens to come back empty.
  bool Accept(const CameraPose& pose, float scene_distance_m);

  void Reset();

  int64_t rejected() const { return rejected_; }

  // The sideways movement, in metres, that currently closes a stretch. Follows
  // the scene distance, so it is only meaningful alongside the frame it was
  // read for.
  float translation_threshold_m() const;

 private:
  bool has_reference_ = false;
  CameraPose reference_{};
  float scene_distance_m_ = kAssumedSceneDistanceM;
  float min_parallax_rad_ = kDefaultMinParallaxRad;
  float min_rotation_rad_ = kDefaultMinRotationRad;
  int64_t rejected_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_KEYFRAME_SELECTOR_H
