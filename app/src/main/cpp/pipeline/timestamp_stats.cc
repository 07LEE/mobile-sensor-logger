#include "timestamp_stats.h"

#include <algorithm>

namespace sensor_logger {
namespace {

constexpr size_t kHistogramBins =
    static_cast<size_t>(TimestampStats::kMaxMedianGapNs /
                        TimestampStats::kMedianBinWidthNs) +
    1;

}  // namespace

TimestampStats::TimestampStats() : histogram_(kHistogramBins, 0) {}

void TimestampStats::Observe(int64_t timestamp_ns) {
  ++samples_;
  if (last_timestamp_ns_ == 0) {
    last_timestamp_ns_ = timestamp_ns;
    return;
  }

  const int64_t period_ns = timestamp_ns - last_timestamp_ns_;
  if (period_ns <= 0) {
    ++non_monotonic_timestamps_;
    return;
  }

  last_timestamp_ns_ = timestamp_ns;
  ++valid_intervals_;
  total_period_ns_ += period_ns;
  max_gap_ns_ = std::max(max_gap_ns_, period_ns);
  const size_t bin = std::min(
      static_cast<size_t>(period_ns / kMedianBinWidthNs),
      histogram_.size() - 1);
  ++histogram_[bin];
}

void TimestampStats::Reset() {
  samples_ = 0;
  valid_intervals_ = 0;
  total_period_ns_ = 0;
  max_gap_ns_ = 0;
  non_monotonic_timestamps_ = 0;
  last_timestamp_ns_ = 0;
  std::fill(histogram_.begin(), histogram_.end(), 0);
}

TimestampStatsSnapshot TimestampStats::Snapshot() const {
  TimestampStatsSnapshot out;
  out.samples = samples_;
  out.valid_intervals = valid_intervals_;
  out.max_gap_ns = max_gap_ns_;
  out.non_monotonic_timestamps = non_monotonic_timestamps_;
  if (valid_intervals_ == 0) return out;

  out.mean_period_ns = total_period_ns_ / valid_intervals_;
  const int64_t target = (valid_intervals_ - 1) / 2;
  int64_t cumulative = 0;
  for (size_t i = 0; i < histogram_.size(); ++i) {
    cumulative += histogram_[i];
    if (cumulative > target) {
      out.median_period_ns =
          static_cast<int64_t>(i) * kMedianBinWidthNs;
      break;
    }
  }
  return out;
}

}  // namespace sensor_logger
