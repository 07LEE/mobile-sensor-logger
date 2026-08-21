#include "keyframe_selector.h"

#include <algorithm>
#include <cmath>

namespace sensor_logger {
namespace {

float TranslationDistance(const CameraPose& a, const CameraPose& b) {
  const float dx = a.translation[0] - b.translation[0];
  const float dy = a.translation[1] - b.translation[1];
  const float dz = a.translation[2] - b.translation[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Angle of the rotation taking `a` to `b`, in radians.
//
// For unit quaternions the dot product is the cosine of half that angle. The
// absolute value folds q and -q together, which represent the same rotation and
// would otherwise read as 180 degrees apart.
float RotationAngle(const CameraPose& a, const CameraPose& b) {
  float dot = 0.0f;
  for (int i = 0; i < 4; ++i) dot += a.rotation[i] * b.rotation[i];

  dot = std::fabs(dot);
  if (dot > 1.0f) dot = 1.0f;  // guards acos against rounding drift

  return 2.0f * std::acos(dot);
}

}  // namespace

float KeyframeSelector::translation_threshold_m() const {
  // Small-angle: the sideways movement subtending `min_parallax_rad_` at the
  // scene is the distance times the tangent of it.
  return scene_distance_m_ * std::tan(min_parallax_rad_);
}

bool KeyframeSelector::Accept(const CameraPose& pose, float scene_distance_m) {
  // A distance that is zero, negative or not a number leaves the last usable
  // one in place. Comparisons against NaN are all false, so letting one through
  // would silently switch the sideways-movement criterion off for good.
  if (std::isfinite(scene_distance_m) && scene_distance_m > 0.0f) {
    scene_distance_m_ = std::clamp(scene_distance_m, kMinSceneDistanceM,
                                   kMaxSceneDistanceM);
  }

  if (!has_reference_) {
    reference_ = pose;
    has_reference_ = true;
    return true;
  }

  const bool moved =
      TranslationDistance(pose, reference_) >= translation_threshold_m();
  const bool turned = RotationAngle(pose, reference_) >= min_rotation_rad_;

  if (!moved && !turned) {
    ++rejected_;
    return false;
  }

  reference_ = pose;
  return true;
}

void KeyframeSelector::Reset() {
  has_reference_ = false;
  reference_ = CameraPose{};
  scene_distance_m_ = kAssumedSceneDistanceM;
  rejected_ = 0;
}

}  // namespace sensor_logger
