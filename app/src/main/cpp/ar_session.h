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

// How the chroma planes are arranged in memory.
//
// Android's YUV_420_888 permits either. Planar keeps U and V in separate
// buffers; semi-planar interleaves them into one, so the two plane pointers
// differ by a single byte and each plane's reported length covers almost the
// same memory. Copying both planes there would duplicate a megabyte per frame.
enum class ChromaLayout {
  kPlanar,
  kSemiPlanarUFirst,
  kSemiPlanarVFirst,
};

// One plane of a YUV_420_888 image. Non-owning.
struct ImagePlane {
  const uint8_t* data = nullptr;
  int32_t length = 0;
  int32_t row_stride = 0;
  int32_t pixel_stride = 0;
};

// A view onto the current frame's camera image.
//
// The pixel data belongs to ARCore and stays valid only until the next
// ArSession::Update, which releases the image it came from. Consumers must
// write it out during the same iteration rather than holding the pointers.
struct CameraImageView {
  bool valid = false;
  int32_t width = 0;
  int32_t height = 0;
  int32_t num_planes = 0;
  ChromaLayout chroma_layout = ChromaLayout::kPlanar;
  ImagePlane planes[3];
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
  CameraImageView image;
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
  // Picks the camera configuration with the largest CPU-accessible image, since
  // that resolution caps the detail any later processing can recover.
  void SelectLargestCpuImageConfig();

  bool ReadPose(ArCamera* camera, CameraPose* out) const;
  bool ReadIntrinsics(ArCamera* camera, CameraIntrinsics* out) const;
  void ReadPointCloud(std::vector<FeaturePoint>* out) const;
  void ReadCameraImage(CameraImageView* out);

  ArSession_* session_ = nullptr;
  ArFrameHandle frame_;

  // Held across the caller's use of FrameData::image and released on the next
  // Update, because the plane pointers stay valid only while it lives.
  ArImageHandle image_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_AR_SESSION_H
