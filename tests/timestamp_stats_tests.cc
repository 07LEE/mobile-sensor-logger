#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "timestamp_stats.h"

namespace {

using sensor_logger::TimestampStats;
using sensor_logger::TimestampStatsSnapshot;

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestEmptyAndSingleSampleHaveNoIntervals() {
  TimestampStats stats;
  Require(stats.Snapshot().valid_intervals == 0, "empty stats");
  stats.Observe(100);
  const TimestampStatsSnapshot snapshot = stats.Snapshot();
  Require(snapshot.samples == 1, "one sample should be counted");
  Require(snapshot.valid_intervals == 0, "one sample has no interval");
}

void TestConstantCadence() {
  TimestampStats stats;
  for (int64_t timestamp : {1'000'000LL, 6'000'000LL, 11'000'000LL,
                            16'000'000LL}) {
    stats.Observe(timestamp);
  }
  const TimestampStatsSnapshot snapshot = stats.Snapshot();
  Require(snapshot.samples == 4, "all samples should be counted");
  Require(snapshot.valid_intervals == 3, "three intervals");
  Require(snapshot.mean_period_ns == 5'000'000, "exact mean");
  Require(snapshot.median_period_ns == 5'000'000, "binned median");
  Require(snapshot.max_gap_ns == 5'000'000, "maximum gap");
}

void TestMedianAndMaximumDescribeJitter() {
  TimestampStats stats;
  int64_t timestamp = 1;
  stats.Observe(timestamp);
  for (int64_t gap : {4'900'000LL, 5'000'000LL, 5'100'000LL,
                      20'000'000LL, 5'000'000LL}) {
    timestamp += gap;
    stats.Observe(timestamp);
  }
  const TimestampStatsSnapshot snapshot = stats.Snapshot();
  Require(snapshot.mean_period_ns == 8'000'000, "mean should include long gap");
  Require(snapshot.median_period_ns == 5'000'000, "median should resist long gap");
  Require(snapshot.max_gap_ns == 20'000'000, "maximum should preserve long gap");
}

void TestNonMonotonicTimestampIsReported() {
  TimestampStats stats;
  stats.Observe(10'000'000);
  stats.Observe(9'000'000);
  stats.Observe(15'000'000);
  const TimestampStatsSnapshot snapshot = stats.Snapshot();
  Require(snapshot.samples == 3, "invalid timestamps are still observed");
  Require(snapshot.valid_intervals == 1, "invalid interval should be excluded");
  Require(snapshot.non_monotonic_timestamps == 1, "regression should be counted");
  Require(snapshot.max_gap_ns == 5'000'000,
          "next gap should use the last valid timestamp");
}

void TestResetClearsEverything() {
  TimestampStats stats;
  stats.Observe(1);
  stats.Observe(10'000'001);
  stats.Reset();
  const TimestampStatsSnapshot snapshot = stats.Snapshot();
  Require(snapshot.samples == 0, "reset samples");
  Require(snapshot.valid_intervals == 0, "reset intervals");
  Require(snapshot.max_gap_ns == 0, "reset gap");
}

}  // namespace

int main() {
  const std::vector<std::pair<const char*, void (*)()>> tests = {
      {"empty and single sample", TestEmptyAndSingleSampleHaveNoIntervals},
      {"constant cadence", TestConstantCadence},
      {"median and maximum", TestMedianAndMaximumDescribeJitter},
      {"non-monotonic timestamp", TestNonMonotonicTimestampIsReported},
      {"reset", TestResetClearsEverything},
  };

  for (const auto& test : tests) {
    test.second();
    std::cout << "PASS: " << test.first << '\n';
  }
  return 0;
}
