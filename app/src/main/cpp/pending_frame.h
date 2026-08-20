#ifndef SENSOR_LOGGER_PENDING_FRAME_H
#define SENSOR_LOGGER_PENDING_FRAME_H

#include <cstdint>
#include <vector>

#include "ar_session.h"

namespace sensor_logger {

// A candidate frame held while sharper ones are still possible.
//
// ARCore releases each frame's image on the next update, so a frame that is not
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
  const CameraPose& pose() const { return pose_; }
  const CameraIntrinsics& intrinsics() const { return intrinsics_; }
  const std::vector<FeaturePoint>& point_cloud() const { return point_cloud_; }

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

 private:
  bool valid_ = false;
  float sharpness_ = 0.0f;

  int64_t timestamp_ns_ = 0;
  CameraPose pose_{};
  CameraIntrinsics intrinsics_{};
  std::vector<FeaturePoint> point_cloud_;

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
