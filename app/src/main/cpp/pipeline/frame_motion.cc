#include "frame_motion.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace sensor_logger {
namespace {

// The picture is compared at this width. Small enough that a frame costs
// microseconds and that sensor noise averages out, large enough that the offset
// search still resolves a fraction of the frame worth caring about.
constexpr int32_t kGridWidth = 64;

// Offsets searched, in grid columns. Has to exceed the shift threshold, or the
// search saturates before the threshold is ever crossed and the stretch never
// ends.
constexpr int32_t kSearchRadius = 10;

}  // namespace

void FrameMotion::Downsample(const CameraImageView& image,
                             std::vector<uint8_t>* out) {
  grid_height_ =
      kGridWidth * image.height / (image.width > 0 ? image.width : 1);
  if (grid_height_ < 1) grid_height_ = 1;

  out->resize(static_cast<size_t>(kGridWidth) * grid_height_);

  const ImagePlane& luma = image.planes[0];
  for (int32_t y = 0; y < grid_height_; ++y) {
    const int32_t source_y = y * image.height / grid_height_;
    const uint8_t* row = luma.data + static_cast<size_t>(source_y) * luma.row_stride;

    for (int32_t x = 0; x < kGridWidth; ++x) {
      const int32_t source_x = x * image.width / kGridWidth;
      (*out)[static_cast<size_t>(y) * kGridWidth + x] =
          row[static_cast<size_t>(source_x) * luma.pixel_stride];
    }
  }
}

bool FrameMotion::Accept(const CameraImageView& image) {
  if (!image.valid || image.planes[0].data == nullptr) return false;

  Downsample(image, &current_);

  if (reference_.size() != current_.size()) {
    reference_ = current_;
    last_shift_ = 0.0f;
    last_residual_ = 0.0f;
    return true;
  }

  // Best whole-pixel offset by sum of absolute differences over the part that
  // overlaps at that offset, normalised so offsets with less overlap are not
  // rewarded for comparing fewer pixels.
  float best_error = std::numeric_limits<float>::max();
  int32_t best_dx = 0;
  int32_t best_dy = 0;

  for (int32_t dy = -kSearchRadius; dy <= kSearchRadius; ++dy) {
    for (int32_t dx = -kSearchRadius; dx <= kSearchRadius; ++dx) {
      int64_t sum = 0;
      int32_t counted = 0;

      for (int32_t y = std::max(0, -dy); y < std::min(grid_height_, grid_height_ - dy); ++y) {
        const uint8_t* current_row = &current_[static_cast<size_t>(y) * kGridWidth];
        const uint8_t* reference_row =
            &reference_[static_cast<size_t>(y + dy) * kGridWidth];

        for (int32_t x = std::max(0, -dx); x < std::min(kGridWidth, kGridWidth - dx); ++x) {
          sum += std::abs(static_cast<int>(current_row[x]) -
                          static_cast<int>(reference_row[x + dx]));
          ++counted;
        }
      }

      if (counted == 0) continue;
      const float error = static_cast<float>(sum) / static_cast<float>(counted);
      if (error < best_error) {
        best_error = error;
        best_dx = dx;
        best_dy = dy;
      }
    }
  }

  last_shift_ = std::sqrt(static_cast<float>(best_dx * best_dx + best_dy * best_dy)) /
                static_cast<float>(kGridWidth);
  last_residual_ = best_error / 255.0f;

  if (last_shift_ < min_shift_ && last_residual_ < min_residual_) {
    ++rejected_;
    return false;
  }

  reference_ = current_;
  return true;
}

void FrameMotion::Reset() {
  reference_.clear();
  last_shift_ = 0.0f;
  last_residual_ = 0.0f;
  rejected_ = 0;
}

}  // namespace sensor_logger
