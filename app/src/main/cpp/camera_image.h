#ifndef SENSOR_LOGGER_CAMERA_IMAGE_H
#define SENSOR_LOGGER_CAMERA_IMAGE_H

#include <cstdint>

namespace sensor_logger {

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
