#ifndef SENSOR_LOGGER_CAMERA_IMAGE_H
#define SENSOR_LOGGER_CAMERA_IMAGE_H

#include <cstdint>
#include <string>

namespace sensor_logger {

// What produced a session's frames.
//
// Recorded because reconstruction needs it and because a phone has several
// lenses that are not interchangeable: an ultra-wide and a periscope disagree
// about focal length by a factor of eight, and frames from the two cannot be
// solved as one camera.
struct CameraInfo {
  std::string id;
  int32_t width = 0;
  int32_t height = 0;
  int32_t sensor_orientation = 0;
  float focal_length_mm = 0.0f;
  float aperture = 0.0f;
  float sensor_width_mm = 0.0f;
  float sensor_height_mm = 0.0f;
  bool logical_multi_camera = false;

  // Where to put the lens so that everything from half that distance to
  // infinity is acceptably sharp, in diopters. On a short focal length this is
  // close enough to cover a room without ever hunting.
  float hyperfocal_diopters = 0.0f;

  // What the sensor will accept when it is driven by hand.
  int64_t min_exposure_ns = 0;
  int64_t max_exposure_ns = 0;
  int32_t min_sensitivity = 0;
  int32_t max_sensitivity = 0;

  // The calibration the manufacturer measured for this lens, if the device
  // publishes it. Solving for intrinsics from the images alone needs wide
  // coverage and a lot of frames; being handed them is worth a great deal when
  // the capture is sparse.
  //
  // Both are expressed against the pre-correction active array, which is not
  // the size the frames come out at, so that rectangle is recorded alongside
  // and anything using these has to scale.
  bool has_calibration = false;
  float intrinsics[5] = {0, 0, 0, 0, 0};   // fx, fy, cx, cy, skew
  float distortion[5] = {0, 0, 0, 0, 0};   // k1, k2, k3, p1, p2
  int32_t pre_correction_array[4] = {0, 0, 0, 0};  // x, y, width, height

  // Where this lens sits relative to the device's reference point, and what
  // that reference is. On the tested device it is the primary camera rather
  // than the gyroscope, so this gives the offset between lenses and not the
  // camera-to-IMU transform.
  bool has_pose = false;
  float pose_translation[3] = {0, 0, 0};
  float pose_rotation[4] = {0, 0, 0, 0};   // x, y, z, w
  int32_t pose_reference = -1;
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

// A view onto one camera frame.
//
// The pixel data belongs to the image it came from and stays valid only until
// that image is released, which happens when the next one is acquired.
// Consumers must copy or write it out during the same iteration rather than
// holding the pointers.
struct CameraImageView {
  bool valid = false;
  int32_t width = 0;
  int32_t height = 0;
  int32_t num_planes = 0;
  ChromaLayout chroma_layout = ChromaLayout::kPlanar;
  ImagePlane planes[3];
};

// Everything worth recording from a single frame.
//
// There is no pose here. The device records what a reconstruction is computed
// from and leaves the computing to a workstation.
struct FrameData {
  int64_t timestamp_ns = 0;
  CameraImageView image;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_CAMERA_IMAGE_H
