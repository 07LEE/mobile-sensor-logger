#include "imu_source.h"

#include <android/log.h>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

// 200Hz. Fast enough to integrate between camera frames without the volume
// mattering: a sample is a few dozen bytes against megabytes for an image.
constexpr int32_t kSampleIntervalUs = 5000;

}  // namespace

ImuSource::~ImuSource() { Stop(); }

bool ImuSource::Start(ALooper* looper, const char* package_name) {
  if (queue_ != nullptr) return true;

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

  queue_ = ASensorManager_createEventQueue(manager_, looper, kLooperIdent, nullptr,
                                           nullptr);
  if (queue_ == nullptr) return false;

  for (const ASensor* sensor : {accelerometer_, gyroscope_}) {
    if (sensor == nullptr) continue;
    ASensorEventQueue_enableSensor(queue_, sensor);
    // The rate is a request; the platform may deliver slower. Timestamps are
    // what the data is read by, so an approximate rate is not a problem.
    ASensorEventQueue_setEventRate(queue_, sensor, kSampleIntervalUs);
  }

  __android_log_print(ANDROID_LOG_INFO, kTag, "IMU started (accel=%d gyro=%d)",
                      accelerometer_ != nullptr, gyroscope_ != nullptr);
  return true;
}

void ImuSource::Stop() {
  if (queue_ == nullptr) return;

  for (const ASensor* sensor : {accelerometer_, gyroscope_}) {
    if (sensor != nullptr) ASensorEventQueue_disableSensor(queue_, sensor);
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
