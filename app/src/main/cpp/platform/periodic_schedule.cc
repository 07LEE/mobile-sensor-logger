#include "periodic_schedule.h"

#include <time.h>

namespace sensor_logger {

int64_t BoottimeNowNs() {
  timespec now{};
  if (clock_gettime(CLOCK_BOOTTIME, &now) != 0) return 0;
  return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

bool PeriodicSchedule::Due(int64_t now_ns) {
  if (!initialized_) {
    initialized_ = true;
    next_due_ns_ = now_ns + interval_ns_;
    return true;
  }

  if (now_ns < next_due_ns_) return false;

  const int64_t intervals_late = (now_ns - next_due_ns_) / interval_ns_;
  next_due_ns_ += (intervals_late + 1) * interval_ns_;
  return true;
}

void PeriodicSchedule::Reset() {
  next_due_ns_ = 0;
  initialized_ = false;
}

}  // namespace sensor_logger
