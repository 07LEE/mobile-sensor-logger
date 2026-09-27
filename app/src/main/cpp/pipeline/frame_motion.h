#ifndef SENSOR_LOGGER_FRAME_MOTION_H
#define SENSOR_LOGGER_FRAME_MOTION_H

#include <cstdint>
#include <vector>

#include "camera_image.h"

namespace sensor_logger {

// Measures how much the picture has changed from a reference frame. It does
// not decide anything: not whether that is enough to end a stretch of
// movement, and not what the reference should be next. KeyframeSelector owns
// both of those, from the shift and residual this reports each frame — this
// split is so the spacing decision can run, and be tested, without a camera.
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
  // Motion is measured on a fixed-width grid and searches this many cells in
  // either direction. A threshold above this single-axis limit can never be
  // reached by horizontal or vertical motion alone.
  static constexpr int32_t kGridWidth = 64;
  static constexpr int32_t kSearchRadius = 10;
  static constexpr float kMaxShift =
      static_cast<float>(kSearchRadius) / static_cast<float>(kGridWidth);

  // Fraction of the frame width the picture may slide before the stretch ends.
  static constexpr float kDefaultMinShift = 0.12f;

  // Presets the PRO panel's shift control steps through. Kept beside kMaxShift
  // so a new preset cannot be added above what the search can reach.
  static constexpr float kShiftPresets[] = {0.06f, 0.09f, kDefaultMinShift};

  // How much of the picture may fail to line up at the best offset, as a
  // fraction of full range, before the stretch ends regardless of the offset.
  static constexpr float kDefaultMinResidual = 0.06f;
  static constexpr float kMaxResidual = 1.0f;

  FrameMotion() = default;

  FrameMotion(float min_shift, float min_residual) {
    SetThresholds(min_shift, min_residual);
  }

  // Measures image against the reference: last_shift()/last_residual() read
  // the result afterward. Never changes the reference — see Commit() — except
  // when there isn't one yet, where the only sensible comparison is to itself,
  // so this frame becomes the reference and both measurements read zero.
  void Measure(const CameraImageView& image);

  // Same comparison as Measure(), for a caller that already has a downsampled
  // grid instead of a raw image — a replay tool reading ADR 15's
  // motion_grid.bin, which never has (or needs) the original frame.
  void MeasureGrid(const std::vector<uint8_t>& grid, int32_t grid_height);

  // False only before the first Measure() call in a session (or since Reset()).
  bool has_reference() const { return !reference_.empty(); }

  // Replaces the reference with a previously-measured frame's grid, once a
  // selector has picked which candidate becomes the next keyframe — not
  // necessarily the frame Measure() most recently saw. Grids are small enough
  // (kGridWidth * grid_height bytes) that a caller can hold on to several
  // candidates' worth without the cost a raw image would carry.
  void Commit(const std::vector<uint8_t>& grid, int32_t grid_height);

  // The grid Measure() computed for the frame it most recently saw, and its
  // height, for a caller to snapshot into a candidate it might later Commit().
  const std::vector<uint8_t>& current_grid() const { return current_; }
  int32_t current_grid_height() const { return grid_height_; }

  // Invalid values leave the corresponding threshold unchanged. Callers that
  // read external settings can therefore report the rejection without turning
  // selection into an unreachable or every-frame condition.
  bool SetThresholds(float min_shift, float min_residual);

  static bool IsValidShiftThreshold(float value);
  static bool IsValidResidualThreshold(float value);

  float min_shift() const { return min_shift_; }
  float min_residual() const { return min_residual_; }

  void Reset();

  // How the last-measured frame compared with the reference. Recorded per
  // frame and shown on screen, since between them they are the only signal the
  // device has that a capture is covering new ground.
  float last_shift() const { return last_shift_; }
  float last_residual() const { return last_residual_; }

 private:
  void Downsample(const CameraImageView& image, std::vector<uint8_t>* out);

  // Shared by Measure() and MeasureGrid(): compares current_ (already filled
  // in by whichever of the two was called) against reference_.
  void MeasureCurrentGrid();

  std::vector<uint8_t> reference_;
  std::vector<uint8_t> current_;
  int32_t grid_height_ = 0;

  float min_shift_ = kDefaultMinShift;
  float min_residual_ = kDefaultMinResidual;
  float last_shift_ = 0.0f;
  float last_residual_ = 0.0f;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_FRAME_MOTION_H
