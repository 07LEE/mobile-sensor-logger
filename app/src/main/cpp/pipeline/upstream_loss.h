#ifndef SENSOR_LOGGER_PIPELINE_UPSTREAM_LOSS_H
#define SENSOR_LOGGER_PIPELINE_UPSTREAM_LOSS_H

#include <cstdint>
#include <vector>

namespace sensor_logger {

struct UpstreamLossSnapshot {
  // Capture results between the first and last image the recorder saw that
  // have no image with the same timestamp: frames the camera finished and the
  // reader replaced before the recorder could take them.
  int64_t lost_in_range = 0;
  // Capture results before the first or after the last image. Recording starts
  // and stops between a result and its image, so a few of these are expected
  // and say nothing about loss.
  int64_t results_outside_range = 0;
};

// Matches capture results to images by sensor timestamp. A result and its
// image carry the same timestamp, so the two streams can be compared without
// assuming they were started and stopped at the same instant.
class UpstreamLoss {
 public:
  void Reset();

  // Every image that reached the recorder, usable or not.
  void ObserveImage(int64_t timestamp_ns);
  void ObserveResult(int64_t timestamp_ns);

  UpstreamLossSnapshot Snapshot() const;

 private:
  std::vector<int64_t> images_;
  std::vector<int64_t> results_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PIPELINE_UPSTREAM_LOSS_H
