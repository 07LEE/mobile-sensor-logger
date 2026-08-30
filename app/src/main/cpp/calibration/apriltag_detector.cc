#include "apriltag_detector.h"

extern "C" {
#include "apriltag.h"
#include "common/zarray.h"
#include "tag36h11.h"
}

namespace sensor_logger {

AprilTagDetector::AprilTagDetector() {
  family_ = tag36h11_create();
  detector_ = apriltag_detector_create();
  apriltag_detector_add_family(detector_, family_);
  // nthreads=1 and quad_decimate=2.0 are apriltag_detector_create's own
  // defaults — left as-is rather than restated here. quad_decimate trims
  // detection to a downsampled copy internally, which is what keeps this
  // affordable to run against full-resolution frames; the HUD only needs a
  // tag count, not sub-pixel corners.
}

AprilTagDetector::~AprilTagDetector() {
  apriltag_detector_destroy(detector_);
  tag36h11_destroy(family_);
}

int AprilTagDetector::Detect(const uint8_t* luma, int32_t width,
                             int32_t height, int32_t stride) {
  if (luma == nullptr || width <= 0 || height <= 0) return 0;

  image_u8_t image = {
      .width = width,
      .height = height,
      .stride = stride,
      .buf = const_cast<uint8_t*>(luma),
  };

  zarray_t* detections = apriltag_detector_detect(detector_, &image);
  const int count = zarray_size(detections);
  apriltag_detections_destroy(detections);
  return count;
}

}  // namespace sensor_logger
