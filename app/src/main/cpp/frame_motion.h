#ifndef SENSOR_LOGGER_FRAME_MOTION_H
#define SENSOR_LOGGER_FRAME_MOTION_H

#include <cstdint>
#include <vector>

#include "camera_image.h"

namespace sensor_logger {

// Decides where one stretch of movement ends and the next begins, by measuring
// how much the picture changed.
//
// This does not pick the frame to keep — SessionRecorder writes the sharpest
// frame of each stretch, not the one that closed it, because the frame that
// happens to cross the threshold is as likely to be blurred as any other. What
// this bounds is how much the written viewpoints overlap.
//
// Measured in the image rather than derived from a pose. Overlap is what a
// reconstruction actually needs, and a pose only implies it once the distance
// to the scene is known: five centimetres beside a desk is a different view,
// and five centimetres beside a far wall is the same photograph. Reading the
// picture skips the estimate — and the point cloud it would have come from,
// which was never reliable.
//
// Two measurements, because one of them alone misses the commonest motion
// there is. A search for the offset that best lines the current frame up with
// the reference catches panning and sideways movement. Walking forward changes
// scale rather than position, and turning about the lens axis changes neither,
// so both leave the offset small; what they do leave is a picture that no
// offset lines up, which is what the leftover difference measures.
class FrameMotion {
 public:
  // Fraction of the frame width the picture may slide before the stretch ends.
  static constexpr float kDefaultMinShift = 0.12f;

  // How much of the picture may fail to line up at the best offset, as a
  // fraction of full range, before the stretch ends regardless of the offset.
  static constexpr float kDefaultMinResidual = 0.06f;

  FrameMotion() = default;

  FrameMotion(float min_shift, float min_residual)
      : min_shift_(min_shift), min_residual_(min_residual) {}

  // True when the picture has changed enough to start a new stretch, which also
  // makes this frame the reference for the next one.
  bool Accept(const CameraImageView& image);

  void SetThresholds(float min_shift, float min_residual) {
    min_shift_ = min_shift;
    min_residual_ = min_residual;
  }

  float min_shift() const { return min_shift_; }
  float min_residual() const { return min_residual_; }

  void Reset();

  // How the last frame compared with the reference. Recorded per frame and
  // shown on screen, since between them they are the only signal the device has
  // that a capture is covering new ground.
  float last_shift() const { return last_shift_; }
  float last_residual() const { return last_residual_; }

  int64_t rejected() const { return rejected_; }

 private:
  void Downsample(const CameraImageView& image, std::vector<uint8_t>* out);

  std::vector<uint8_t> reference_;
  std::vector<uint8_t> current_;
  int32_t grid_height_ = 0;

  float min_shift_ = kDefaultMinShift;
  float min_residual_ = kDefaultMinResidual;
  float last_shift_ = 0.0f;
  float last_residual_ = 0.0f;
  int64_t rejected_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_FRAME_MOTION_H
