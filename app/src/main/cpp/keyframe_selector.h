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
// Holding the phone still never closes a stretch, so nothing accumulates;
// sweeping it closes one every few centimetres.
class KeyframeSelector {
 public:
  // Defaults are a starting point, not a measured optimum: they have never been
  // run against a real capture.
  static constexpr float kDefaultMinTranslationM = 0.05f;
  static constexpr float kDefaultMinRotationRad = 0.10f;  // ~5.7 degrees

  KeyframeSelector() = default;

  KeyframeSelector(float min_translation_m, float min_rotation_rad)
      : min_translation_m_(min_translation_m),
        min_rotation_rad_(min_rotation_rad) {}

  // True when `pose` is far enough from the last accepted one to start a new
  // stretch, which also moves the reference forward.
  bool Accept(const CameraPose& pose);

  void Reset();

  int64_t rejected() const { return rejected_; }

 private:
  bool has_reference_ = false;
  CameraPose reference_{};
  float min_translation_m_ = kDefaultMinTranslationM;
  float min_rotation_rad_ = kDefaultMinRotationRad;
  int64_t rejected_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_KEYFRAME_SELECTOR_H
