#ifndef SENSOR_LOGGER_PLATFORM_PERIODIC_SCHEDULE_H
#define SENSOR_LOGGER_PLATFORM_PERIODIC_SCHEDULE_H

#include <cstdint>

namespace sensor_logger {

// Slow platform queries are intentionally independent of camera rate. Two
// seconds keeps disk-space and thermal state reasonably fresh without making
// statvfs and JNI work part of every frame.
constexpr int64_t kFreeSpaceCheckIntervalNs = 2'000'000'000LL;
constexpr int64_t kDeviceStatusSampleIntervalNs = 2'000'000'000LL;

// Nanoseconds since boot, including time spent suspended. Camera and sensor
// timestamps use this clock when CameraInfo::timestamps_realtime is true.
int64_t BoottimeNowNs();

// One-shot polling gate for work that should run at a wall-clock cadence.
//
// The first check is due immediately. A late check runs once, not once per
// missed interval, then advances to the first future boundary. That prevents
// backgrounding or a stalled loop from causing a burst of JNI/filesystem work.
class PeriodicSchedule {
 public:
  explicit PeriodicSchedule(int64_t interval_ns) : interval_ns_(interval_ns) {}

  bool Due(int64_t now_ns);
  void Reset();

 private:
  int64_t interval_ns_;
  int64_t next_due_ns_ = 0;
  bool initialized_ = false;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PLATFORM_PERIODIC_SCHEDULE_H
