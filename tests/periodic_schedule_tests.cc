#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "periodic_schedule.h"

namespace {

using sensor_logger::BoottimeNowNs;
using sensor_logger::PeriodicSchedule;

constexpr int64_t kSecondNs = 1'000'000'000LL;
constexpr int64_t kIntervalNs = 2 * kSecondNs;

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestFirstCheckIsImmediate() {
  PeriodicSchedule schedule(kIntervalNs);
  Require(schedule.Due(37), "the first check should be due immediately");
  Require(!schedule.Due(37), "the same timestamp should not run twice");
}

void TestExactIntervalIsDue() {
  PeriodicSchedule schedule(kIntervalNs);
  Require(schedule.Due(0), "initial check");
  Require(!schedule.Due(kIntervalNs - 1), "one nanosecond early");
  Require(schedule.Due(kIntervalNs), "exact interval boundary");
  Require(!schedule.Due(kIntervalNs + 1), "next interval should not be due");
}

void TestLatePollSkipsCatchUpBurst() {
  PeriodicSchedule schedule(kIntervalNs);
  Require(schedule.Due(0), "initial check");
  Require(schedule.Due(5 * kSecondNs), "late check should run once");
  Require(!schedule.Due(5 * kSecondNs), "late check should not catch up twice");
  Require(!schedule.Due(6 * kSecondNs - 1), "next aligned boundary is still future");
  Require(schedule.Due(6 * kSecondNs), "cadence should remain aligned after delay");
}

void TestResetMakesNextCheckImmediate() {
  PeriodicSchedule schedule(kIntervalNs);
  Require(schedule.Due(0), "initial check");
  schedule.Reset();
  Require(schedule.Due(kSecondNs), "reset should request a prompt sample");
}

std::vector<int64_t> DueTimesAtFps(int32_t fps) {
  PeriodicSchedule schedule(kIntervalNs);
  std::vector<int64_t> result;
  for (int32_t frame = 0; frame <= fps * 10; ++frame) {
    const int64_t timestamp_ns =
        static_cast<int64_t>(frame) * kSecondNs / fps;
    if (schedule.Due(timestamp_ns)) result.push_back(timestamp_ns);
  }
  return result;
}

void TestCadenceDoesNotDependOnCameraFps() {
  const std::vector<int64_t> expected = {
      0, 2 * kSecondNs, 4 * kSecondNs,
      6 * kSecondNs, 8 * kSecondNs, 10 * kSecondNs};
  for (int32_t fps : {15, 24, 30}) {
    Require(DueTimesAtFps(fps) == expected,
            "15/24/30fps should produce the same two-second cadence");
  }
}

void TestBoottimeClockIsAvailableAndMonotonic() {
  const int64_t first = BoottimeNowNs();
  const int64_t second = BoottimeNowNs();
  Require(first > 0, "CLOCK_BOOTTIME should be available");
  Require(second >= first, "CLOCK_BOOTTIME should not move backwards");
}

}  // namespace

int main() {
  const std::vector<std::pair<const char*, void (*)()>> tests = {
      {"first check is immediate", TestFirstCheckIsImmediate},
      {"exact interval is due", TestExactIntervalIsDue},
      {"late poll skips catch-up burst", TestLatePollSkipsCatchUpBurst},
      {"reset makes next check immediate", TestResetMakesNextCheckImmediate},
      {"cadence is independent of camera fps", TestCadenceDoesNotDependOnCameraFps},
      {"boottime clock is monotonic", TestBoottimeClockIsAvailableAndMonotonic},
  };

  for (const auto& test : tests) {
    test.second();
    std::cout << "PASS: " << test.first << '\n';
  }
  return 0;
}
