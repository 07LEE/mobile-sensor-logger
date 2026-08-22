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
//   lens      = ultrawide | main | <camera id>
//   shift     = 0.12        how far the picture may slide before a new frame
//   residual  = 0.06        how much of it may stop matching
//   shutter   = auto | 1/120  longest exposure allowed once locked
//   mains     = 60 | 50 | off how often the lights pulse, for flicker
//
// Anything missing keeps its default. An unreadable file is not an error: the
// defaults are a working configuration, and a capture that refused to start
// because of a typo in a settings file would be worse than one that ignored it.
// Which of the rear lenses to open.
enum class Lens {
  // The camera the system offers first. On a phone this is a logical camera
  // that picks a physical lens by zoom ratio, which means it can change lens
  // mid-session and change the intrinsics with it. Best detail at arm's length
  // and beyond.
  kMain,
  // The shortest focal length the device exposes, and the default.
  //
  // It wins at both ends of the range this is used at. A frame covers far more
  // of a room, which matters when the free space is the budget, and it focuses
  // closer than the main lens does — 5cm against 10cm on the tested device,
  // which is enough to beat it on close detail despite the wider field, since
  // detail goes as the reciprocal of distance.
  //
  // It is also a physical camera rather than a logical one, so the lens cannot
  // change underneath a capture.
  kUltrawide,
  // A camera id spelled out. Whatever it is, it is used as given.
  kExplicit,
};

struct CaptureConfig {
  // 0 means the largest size the camera offers.
  int32_t capture_width = 0;
  int32_t capture_height = 0;
  Retention retention = Retention::kSharpest;
  Lens lens = Lens::kUltrawide;
  std::string lens_id;  // only when lens is kExplicit

  // What ends a stretch of movement, and so how densely a capture is sampled.
  // Halving them roughly doubles the frames kept. Defaults live in
  // FrameMotion; these carry whatever the file said.
  float min_shift = 0.12f;
  float min_residual = 0.06f;

  // The longest exposure allowed once the camera is locked, in nanoseconds, or
  // 0 to take whatever the scene metered to.
  //
  // Motion blur is the exposure time multiplied by how fast the camera is
  // turning, and a blurred frame is worse for a reconstruction than a noisy
  // one: noise averages out across views and blur does not. Capping the
  // exposure trades one for the other, since the sensitivity has to rise to
  // compensate.
  //
  // It does nothing for rolling shutter, which is set by how long the sensor
  // takes to read itself out and not by how long it was exposed.
  int64_t max_exposure_ns = 0;

  // Mains frequency in hertz, or 0 to ignore flicker.
  //
  // Lighting on alternating current pulses at twice the mains frequency, and a
  // rolling shutter exposes each row at a different point in that cycle, so an
  // exposure that is not a whole number of half-cycles bands the frame. Sixty
  // hertz means 8.333ms; a capped exposure is rounded down to a multiple of it.
  //
  // The platform's own metering already does this — the exposures it chose on
  // the tested device, 1/30 and 1/24, are both exact multiples — which is
  // precisely the protection that setting the exposure by hand gives up.
  int32_t mains_hz = 60;

  // Loads from `<directory>/capture.conf`. Returns false if there was no file,
  // which leaves every field at its default.
  bool Load(const std::string& directory);

  // For the manifest and the readout, so a capture says how it was taken.
  std::string Describe() const;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_CAPTURE_CONFIG_H
