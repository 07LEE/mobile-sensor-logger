#include "imu_source.h"

#include <android/log.h>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

}  // namespace

ImuSource::~ImuSource() { Stop(); }

bool ImuSource::Start(ALooper* looper, const char* package_name) {
  if (queue_ != nullptr) return true;
  info_ = ImuInfo{};

  manager_ = ASensorManager_getInstanceForPackage(package_name);
  if (manager_ == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "no sensor manager");
    return false;
  }

  accelerometer_ =
      ASensorManager_getDefaultSensor(manager_, ASENSOR_TYPE_ACCELEROMETER);
  gyroscope_ =
      ASensorManager_getDefaultSensor(manager_, ASENSOR_TYPE_GYROSCOPE);

  if (accelerometer_ == nullptr && gyroscope_ == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "no accelerometer or gyroscope");
    return false;
  }

  auto describe = [](const ASensor* sensor, ImuSensorInfo* info) {
    if (sensor == nullptr) return;
    info->available = true;
    const char* name = ASensor_getName(sensor);
    const char* vendor = ASensor_getVendor(sensor);
    info->name = name != nullptr ? name : "";
    info->vendor = vendor != nullptr ? vendor : "";
    info->min_delay_us = ASensor_getMinDelay(sensor);
  };
  describe(accelerometer_, &info_.accelerometer);
  describe(gyroscope_, &info_.gyroscope);

  queue_ = ASensorManager_createEventQueue(manager_, looper, kLooperIdent, nullptr,
                                           nullptr);
  if (queue_ == nullptr) return false;

  auto configure = [this](const ASensor* sensor, ImuSensorInfo* info) {
    if (sensor == nullptr) return;
    info->enable_result = ASensorEventQueue_enableSensor(queue_, sensor);
    if (info->enable_result != 0) return;
    // The rate is a request; the platform may deliver slower. Timestamps are
    // what the data is read by, so an approximate rate is not a problem.
    info->set_rate_result = ASensorEventQueue_setEventRate(
        queue_, sensor, info_.requested_interval_us);
  };
  configure(accelerometer_, &info_.accelerometer);
  configure(gyroscope_, &info_.gyroscope);

  const bool any_enabled =
      info_.accelerometer.enable_result == 0 ||
      info_.gyroscope.enable_result == 0;
  if (!any_enabled) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "could not enable either IMU sensor");
    ASensorManager_destroyEventQueue(manager_, queue_);
    queue_ = nullptr;
    return false;
  }

  __android_log_print(
      ANDROID_LOG_INFO, kTag,
      "IMU started (requested=%dus accel=%s enable=%d rate=%d "
      "gyro=%s enable=%d rate=%d)",
      info_.requested_interval_us,
      info_.accelerometer.available ? info_.accelerometer.name.c_str() : "none",
      info_.accelerometer.enable_result, info_.accelerometer.set_rate_result,
      info_.gyroscope.available ? info_.gyroscope.name.c_str() : "none",
      info_.gyroscope.enable_result, info_.gyroscope.set_rate_result);
  return true;
}

void ImuSource::Stop() {
  if (queue_ == nullptr) return;

  if (accelerometer_ != nullptr && info_.accelerometer.enable_result == 0) {
    ASensorEventQueue_disableSensor(queue_, accelerometer_);
  }
  if (gyroscope_ != nullptr && info_.gyroscope.enable_result == 0) {
    ASensorEventQueue_disableSensor(queue_, gyroscope_);
  }

  if (manager_ != nullptr) {
    ASensorManager_destroyEventQueue(manager_, queue_);
  }
  queue_ = nullptr;
}

void ImuSource::Drain(std::vector<ImuSample>* out) {
  out->clear();
  if (queue_ == nullptr) return;

  ASensorEvent events[64];
  ssize_t count = 0;

  // Loops because a burst can exceed the buffer; at 200Hz across two sensors a
  // camera frame's worth is well under this, but a stalled frame is not.
  while ((count = ASensorEventQueue_getEvents(queue_, events,
                                              std::size(events))) > 0) {
    for (ssize_t i = 0; i < count; ++i) {
      const ASensorEvent& event = events[i];
      const bool gyro = event.type == ASENSOR_TYPE_GYROSCOPE;
      if (!gyro && event.type != ASENSOR_TYPE_ACCELEROMETER) continue;

      out->push_back(ImuSample{event.timestamp, gyro, event.vector.x,
                               event.vector.y, event.vector.z});
    }

    if (static_cast<size_t>(count) < std::size(events)) break;
  }
}

}  // namespace sensor_logger
