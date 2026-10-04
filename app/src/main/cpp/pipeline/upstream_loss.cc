#include "upstream_loss.h"

#include <algorithm>

namespace sensor_logger {

void UpstreamLoss::Reset() {
  images_.clear();
  results_.clear();
}

void UpstreamLoss::ObserveImage(int64_t timestamp_ns) {
  images_.push_back(timestamp_ns);
}

void UpstreamLoss::ObserveResult(int64_t timestamp_ns) {
  results_.push_back(timestamp_ns);
}

UpstreamLossSnapshot UpstreamLoss::Snapshot() const {
  UpstreamLossSnapshot snapshot;
  if (images_.empty()) {
    snapshot.results_outside_range = static_cast<int64_t>(results_.size());
    return snapshot;
  }

  std::vector<int64_t> images = images_;
  std::sort(images.begin(), images.end());
  const int64_t first = images.front();
  const int64_t last = images.back();

  for (int64_t result : results_) {
    if (result < first || result > last) {
      ++snapshot.results_outside_range;
    } else if (!std::binary_search(images.begin(), images.end(), result)) {
      ++snapshot.lost_in_range;
    }
  }
  return snapshot;
}

}  // namespace sensor_logger
