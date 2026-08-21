#ifndef SENSOR_LOGGER_PENDING_FRAME_H
#define SENSOR_LOGGER_PENDING_FRAME_H

#include <cstdint>
#include <vector>

#include "camera_image.h"

namespace sensor_logger {

// A candidate frame held while sharper ones are still possible.
//
// The camera reclaims each image when the next is taken, so a frame that is not
// written immediately has to be copied out. Only one candidate is held at a
// time — the sharpest seen since the last write — which bounds the cost to a
// single frame's worth of memory and one copy each time the leader changes.
//
// Semi-planar chroma is copied as the single interleaved buffer it actually is.
// Taking the U and V planes at face value there would copy the same megabyte
// twice, since both point into that one buffer a byte apart.
class PendingFrame {
 public:
  // Copies everything needed to write this frame later.
  void Set(const FrameData& frame, float sharpness);

  void Clear() { valid_ = false; }

  bool valid() const { return valid_; }
  float sharpness() const { return sharpness_; }

  int64_t timestamp_ns() const { return timestamp_ns_; }

  int32_t width() const { return width_; }
  int32_t height() const { return height_; }
  ChromaLayout chroma_layout() const { return chroma_layout_; }

  int32_t luma_row_stride() const { return luma_row_stride_; }
  int32_t chroma_row_stride() const { return chroma_row_stride_; }
  int32_t chroma_pixel_stride() const { return chroma_pixel_stride_; }

  // Byte counts of the segments written, in order. The third is zero for
  // semi-planar chroma, where U and V share one segment.
  int32_t segment_length(int32_t index) const { return segment_lengths_[index]; }

  // The segments, concatenated.
  const std::vector<uint8_t>& pixels() const { return pixels_; }

  // Gives the buffer away once it has been written, and takes a spent one back
  // for the next frame.
  //
  // A frame at capture resolution is around twenty megabytes. Allocating that
  // fresh means the kernel hands over pages nobody has touched, and the copy
  // then faults on every one of them — measured at over a hundred milliseconds
  // on the tested device, which is three camera frames missed for each one
  // kept. A buffer that has already been used carries its pages with it.
  std::vector<uint8_t> ReleaseBuffer() { return std::move(pixels_); }
  void AdoptBuffer(std::vector<uint8_t>&& buffer) { pixels_ = std::move(buffer); }

 private:
  bool valid_ = false;
  float sharpness_ = 0.0f;

  int64_t timestamp_ns_ = 0;

  int32_t width_ = 0;
  int32_t height_ = 0;
  ChromaLayout chroma_layout_ = ChromaLayout::kPlanar;
  int32_t luma_row_stride_ = 0;
  int32_t chroma_row_stride_ = 0;
  int32_t chroma_pixel_stride_ = 0;
  int32_t segment_lengths_[3] = {0, 0, 0};
  std::vector<uint8_t> pixels_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PENDING_FRAME_H
