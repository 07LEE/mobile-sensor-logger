#ifndef SENSOR_LOGGER_AR_SESSION_H
#define SENSOR_LOGGER_AR_SESSION_H

#include <arcore_c_api.h>
#include <jni.h>

#include <array>
#include <cstdint>
#include <vector>

#include "ar_handle.h"

namespace sensor_logger {

// One tracked point from ARCore's feature point cloud.
struct FeaturePoint {
  float x;
  float y;
  float z;
  float confidence;
};

// Camera pose as ARCore reports it: translation plus a rotation quaternion.
struct CameraPose {
  std::array<float, 3> translation;
  // (x, y, z, w), matching ArPose_getPoseRaw's ordering.
  std::array<float, 4> rotation;
};

// Pinhole intrinsics for the frame's image.
struct CameraIntrinsics {
  float focal_x;
  float focal_y;
  float principal_x;
  float principal_y;
  int32_t image_width;
  int32_t image_height;
};

// Everything worth recording from a single ARCore frame.
//
// Populated only when the camera is actually tracking; a frame captured while
// tracking is lost carries a pose that cannot be trusted, and writing it would
// silently corrupt the trajectory.
struct FrameData {
  int64_t timestamp_ns = 0;
  CameraPose pose{};
  CameraIntrinsics intrinsics{};
  std::vector<FeaturePoint> point_cloud;
  bool is_tracking = false;
};

// Owns the ARCore session and turns each update into a FrameData.
class ArSession {
 public:
  ArSession() = default;
  ~ArSession();

  ArSession(const ArSession&) = delete;
  ArSession& operator=(const ArSession&) = delete;

  // Creates the underlying session. Returns false if ARCore is unavailable or
  // the camera permission has not been granted.
  bool Create(JNIEnv* env, jobject context);

  bool Resume();
  void Pause();

  // Binds the GL texture ARCore renders the camera image into. Must be called
  // once the GL context exists and before the first Update.
  void SetCameraTexture(uint32_t texture_id);

  // Tells ARCore the drawable size so it can size the background correctly.
  void SetDisplayGeometry(int rotation, int width, int height);

  // Advances the session by one frame. Returns false if the update failed.
  bool Update(FrameData* out);

  bool IsValid() const { return session_ != nullptr; }

 private:
  bool ReadPose(ArCamera* camera, CameraPose* out) const;
  bool ReadIntrinsics(ArCamera* camera, CameraIntrinsics* out) const;
  void ReadPointCloud(std::vector<FeaturePoint>* out) const;

  ArSession_* session_ = nullptr;
  ArFrameHandle frame_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_AR_SESSION_H
