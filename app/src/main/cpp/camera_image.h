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
