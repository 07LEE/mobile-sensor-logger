#include "pending_frame.h"

#include <cstring>

namespace sensor_logger {

void PendingFrame::Set(const FrameData& frame, float sharpness) {
  sharpness_ = sharpness;
  timestamp_ns_ = frame.timestamp_ns;
  pose_ = frame.pose;
  intrinsics_ = frame.intrinsics;
  point_cloud_ = frame.point_cloud;

  const CameraImageView& image = frame.image;
  width_ = image.width;
  height_ = image.height;
  num_planes_ = image.num_planes;

  size_t total = 0;
  for (int32_t i = 0; i < num_planes_; ++i) {
    plane_info_[i] = image.planes[i];
    // The copy owns its bytes; the source pointer would dangle after the next
    // ArSession::Update, so it is deliberately not carried over.
    plane_info_[i].data = nullptr;
    if (image.planes[i].length > 0) {
      total += static_cast<size_t>(image.planes[i].length);
    }
  }

  // resize rather than assign: the capacity survives between candidates, so
  // replacing the leader mid-window does not reallocate.
  pixels_.resize(total);

  size_t offset = 0;
  for (int32_t i = 0; i < num_planes_; ++i) {
    const ImagePlane& plane = image.planes[i];
    if (plane.data == nullptr || plane.length <= 0) continue;
    std::memcpy(pixels_.data() + offset, plane.data,
                static_cast<size_t>(plane.length));
    offset += static_cast<size_t>(plane.length);
  }

  valid_ = true;
}

}  // namespace sensor_logger
