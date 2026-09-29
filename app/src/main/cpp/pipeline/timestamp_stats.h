#ifndef SENSOR_LOGGER_PIPELINE_TIMESTAMP_STATS_H
#define SENSOR_LOGGER_PIPELINE_TIMESTAMP_STATS_H

#include <cstdint>
#include <vector>

namespace sensor_logger {

struct TimestampStatsSnapshot {
  int64_t samples = 0;
  int64_t valid_intervals = 0;
  int64_t mean_period_ns = 0;
  int64_t median_period_ns = 0;
  int64_t max_gap_ns = 0;
  int64_t non_monotonic_timestamps = 0;
};

// Streaming timestamp cadence summary with bounded memory.
//
// Mean and maximum use the exact positive intervals. Median uses 50us bins up
// to one second, which costs about 80KB per stream regardless of session
// length and is precise enough to distinguish a 5ms IMU cadence from camera
// cadences measured in tens of milliseconds. Gaps above one second share the
// final bin; max_gap_ns still preserves their exact value.
class TimestampStats {
 public:
  static constexpr int64_t kMedianBinWidthNs = 50'000;
  static constexpr int64_t kMaxMedianGapNs = 1'000'000'000LL;

  TimestampStats();

  void Observe(int64_t timestamp_ns);
  void Reset();
  TimestampStatsSnapshot Snapshot() const;

 private:
  int64_t samples_ = 0;
  int64_t valid_intervals_ = 0;
  int64_t total_period_ns_ = 0;
  int64_t max_gap_ns_ = 0;
  int64_t non_monotonic_timestamps_ = 0;
  int64_t last_timestamp_ns_ = 0;
  std::vector<uint32_t> histogram_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PIPELINE_TIMESTAMP_STATS_H
