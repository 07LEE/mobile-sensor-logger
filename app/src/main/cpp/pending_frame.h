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
  int32_t num_planes() const { return num_planes_; }

  // Plane metadata as ARCore reported it, so the copy stays decodable.
  const ImagePlane& plane_info(int32_t index) const { return plane_info_[index]; }

  // The planes, concatenated in ARCore's order.
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
  int32_t num_planes_ = 0;
  ImagePlane plane_info_[3];
  std::vector<uint8_t> pixels_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PENDING_FRAME_H
