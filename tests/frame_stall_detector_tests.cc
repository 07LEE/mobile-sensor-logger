#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "frame_stall_detector.h"

namespace {

using sensor_logger::FrameStallDetector;

constexpr int64_t kPeriodNs = 33'000'000;

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestSteadyFramesDoNotStall() {
  FrameStallDetector detector;
  detector.Reset(0);
  for (int64_t t = kPeriodNs; t <= 10 * kPeriodNs; t += kPeriodNs) {
    detector.Check(t, kPeriodNs);
    detector.OnFrame(t);
  }
  Require(!detector.stalled(), "steady frames should not stall");
}

void TestMissingFramesStall() {
  FrameStallDetector detector;
  detector.Reset(0);
  detector.OnFrame(kPeriodNs);
  detector.Check(2 * kPeriodNs, kPeriodNs);
  Require(!detector.stalled(), "one period late is not a stall");
  detector.Check(4 * kPeriodNs, kPeriodNs);
  Require(detector.stalled(), "three periods without a frame is a stall");
}

void TestStallLatchesUntilReset() {
  FrameStallDetector detector;
  detector.Reset(0);
  detector.Check(10 * kPeriodNs, kPeriodNs);
  detector.OnFrame(11 * kPeriodNs);
  Require(detector.stalled(), "a later frame should not clear the stall");
  detector.Reset(12 * kPeriodNs);
  Require(!detector.stalled(), "reset should clear the stall");
}

}  // namespace

int main() {
  TestSteadyFramesDoNotStall();
  TestMissingFramesStall();
  TestStallLatchesUntilReset();
  return 0;
}
