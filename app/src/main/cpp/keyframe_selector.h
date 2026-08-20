#ifndef SENSOR_LOGGER_KEYFRAME_SELECTOR_H
#define SENSOR_LOGGER_KEYFRAME_SELECTOR_H

#include "ar_session.h"

namespace sensor_logger {

// Decides which frames are worth writing.
//
// Recording every tracked frame is not viable: at the resolutions this app asks
// for, a frame is megabytes and the camera produces thirty a second. Nor is it
// useful — thirty near-identical views of one spot add nothing a reconstruction
// can triangulate, while the disk and write bandwidth they consume is what
// limits how long a capture can run.
//
// A frame is kept when the camera has moved or turned far enough since the last
// kept one, which spends the budget on parallax instead of duplicates. Holding
// the phone still records nothing after the first frame; sweeping it records
// steadily.
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

  // True when `pose` is far enough from the last accepted one. Accepting a
  // frame updates the reference, so the caller must only call this for frames
  // it will actually write.
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
