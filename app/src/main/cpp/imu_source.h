#ifndef SENSOR_LOGGER_IMU_SOURCE_H
#define SENSOR_LOGGER_IMU_SOURCE_H

#include <android/looper.h>
#include <android/sensor.h>

#include <cstdint>
#include <vector>

namespace sensor_logger {

// One accelerometer or gyroscope reading.
//
// Kept as separate rows rather than fused pairs: the two sensors deliver on
// their own schedules, and pairing them here would mean inventing a timestamp
// for one of them. Anything consuming this can interpolate as it sees fit.
struct ImuSample {
  int64_t timestamp_ns;
  bool is_gyroscope;  // false for accelerometer
  float x;
  float y;
  float z;
};

// Accelerometer and gyroscope, read through the NDK sensor API.
//
// Recorded because ARCore's poses are not the only thing the capture might be
// used for. Raw inertial data is what a different VIO implementation would need,
// what gives bundle adjustment an inertial constraint, and what carries metric
// scale that monocular structure-from-motion cannot recover on its own. It costs
// almost nothing next to the images: a few kilobytes a second against megabytes
// a frame.
//
// Samples carry the hardware timestamp, on the same clock as ARCore's frame
// timestamps, which is what makes the two streams comparable at all.
class ImuSource {
 public:
  // Identifier this queue reports itself under on the shared looper. The glue
  // uses 1 and 2. Callers need it: the queue has no poll source, so a looper
  // drain loop can only tell these events apart by the identifier, and it has
  // to read them or pollOnce will keep handing back the same one.
  static constexpr int kLooperIdent = 3;

  ImuSource() = default;
  ~ImuSource();

  ImuSource(const ImuSource&) = delete;
  ImuSource& operator=(const ImuSource&) = delete;

  // Attaches to `looper`. Returns false if neither sensor is available.
  bool Start(ALooper* looper, const char* package_name);

  void Stop();

  // Moves everything queued since the last call into `out`. Called once per
  // camera frame, so each call typically yields several samples.
  void Drain(std::vector<ImuSample>* out);

  bool is_running() const { return queue_ != nullptr; }

 private:
  ASensorManager* manager_ = nullptr;
  ASensorEventQueue* queue_ = nullptr;
  const ASensor* accelerometer_ = nullptr;
  const ASensor* gyroscope_ = nullptr;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_IMU_SOURCE_H
