#ifndef SENSOR_LOGGER_APRILTAG_DETECTOR_H
#define SENSOR_LOGGER_APRILTAG_DETECTOR_H

#include <cstdint>

struct apriltag_family;
struct apriltag_detector;

namespace sensor_logger {

// Live tag36h11 AprilTag detection for the PRO panel's EXTRINSIC capture
// mode — see docs/adr/0011-pro-panel-extrinsic-capture-button.md. This is
// capture guidance only: how many tags of the AprilGrid are visible right
// now, for the `TAGS N/M` HUD line. It does not estimate a pose or solve
// anything — that stays Kalibr's job, offline, on the workstation.
class AprilTagDetector {
 public:
  AprilTagDetector();
  ~AprilTagDetector();

  AprilTagDetector(const AprilTagDetector&) = delete;
  AprilTagDetector& operator=(const AprilTagDetector&) = delete;

  // Runs detection on one luma plane and returns how many tags were found.
  // `stride` is the plane's row stride, not the same as `width`.
  int Detect(const uint8_t* luma, int32_t width, int32_t height,
            int32_t stride);

 private:
  apriltag_family* family_ = nullptr;
  apriltag_detector* detector_ = nullptr;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_APRILTAG_DETECTOR_H
