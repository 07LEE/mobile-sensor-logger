#include "pending_frame.h"

#include <algorithm>
#include <cstring>

namespace sensor_logger {

void PendingFrame::Set(const FrameData& frame, float sharpness) {
  sharpness_ = sharpness;
  timestamp_ns_ = frame.timestamp_ns;

  const CameraImageView& image = frame.image;
  width_ = image.width;
  height_ = image.height;
  chroma_layout_ = image.chroma_layout;

  const ImagePlane& luma = image.planes[0];
  const ImagePlane& u = image.planes[1];
  const ImagePlane& v = image.planes[2];

  luma_row_stride_ = luma.row_stride;
  chroma_row_stride_ = u.row_stride;
  chroma_pixel_stride_ = u.pixel_stride;

  // Sources to copy, in the order they end up in the file.
  const uint8_t* sources[3] = {luma.data, nullptr, nullptr};
  segment_lengths_[0] = luma.length;
  segment_lengths_[1] = 0;
  segment_lengths_[2] = 0;

  if (chroma_layout_ == ChromaLayout::kPlanar) {
    sources[1] = u.data;
    sources[2] = v.data;
    segment_lengths_[1] = u.length;
    segment_lengths_[2] = v.length;
  } else {
    // One interleaved buffer, entered at whichever of U or V comes first. Each
    // plane's reported length stops a byte short of the end because that byte
    // belongs to the other plane, so the buffer is one longer.
    const bool u_first = chroma_layout_ == ChromaLayout::kSemiPlanarUFirst;
    sources[1] = u_first ? u.data : v.data;
    segment_lengths_[1] = std::max(u.length, v.length) + 1;
  }

  size_t total = 0;
  for (int32_t i = 0; i < 3; ++i) {
    if (sources[i] != nullptr && segment_lengths_[i] > 0) {
      total += static_cast<size_t>(segment_lengths_[i]);
    } else {
      segment_lengths_[i] = 0;
    }
  }

  // resize rather than assign: the capacity survives between candidates, so
  // replacing the leader mid-window does not reallocate.
  pixels_.resize(total);

  size_t offset = 0;
  for (int32_t i = 0; i < 3; ++i) {
    if (segment_lengths_[i] <= 0) continue;
    std::memcpy(pixels_.data() + offset, sources[i],
                static_cast<size_t>(segment_lengths_[i]));
    offset += static_cast<size_t>(segment_lengths_[i]);
  }

  valid_ = true;
}

}  // namespace sensor_logger
