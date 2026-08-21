#ifndef SENSOR_LOGGER_CAPTURE_CONFIG_H
#define SENSOR_LOGGER_CAPTURE_CONFIG_H

#include <cstdint>
#include <string>

namespace sensor_logger {

// What to keep out of what the camera produces.
enum class Retention {
  // The sharpest frame of each stretch of movement. Handheld capture produces
  // defocused and smeared frames continuously and this is what avoids them.
  kSharpest,
  // Every frame. Costs about twenty megabytes each at full resolution, so this
  // fills a phone in minutes; it exists for short takes where the selection
  // rule is what is being questioned.
  kAll,
};

// Read once at startup from `capture.conf` in the app's external files
// directory, which is where `adb push` can reach without the app having any UI
// to change it from.
//
//   capture   = max | 1920x1080
//   retention = sharpest | all
//
// Anything missing keeps its default. An unreadable file is not an error: the
// defaults are a working configuration, and a capture that refused to start
// because of a typo in a settings file would be worse than one that ignored it.
struct CaptureConfig {
  // 0 means the largest size the camera offers.
  int32_t capture_width = 0;
  int32_t capture_height = 0;
  Retention retention = Retention::kSharpest;

  // Loads from `<directory>/capture.conf`. Returns false if there was no file,
  // which leaves every field at its default.
  bool Load(const std::string& directory);

  // For the manifest and the readout, so a capture says how it was taken.
  std::string Describe() const;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_CAPTURE_CONFIG_H
