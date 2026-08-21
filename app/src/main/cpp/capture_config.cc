#include "capture_config.h"

#include <android/log.h>

#include <cstdlib>
#include <cstdio>
#include <fstream>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

std::string Trim(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

}  // namespace

bool CaptureConfig::Load(const std::string& directory) {
  const std::string path = directory + "/capture.conf";
  std::ifstream file(path);
  if (!file.is_open()) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "no capture.conf; using defaults (%s)",
                        Describe().c_str());
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    const size_t comment = line.find('#');
    if (comment != std::string::npos) line = line.substr(0, comment);

    const size_t equals = line.find('=');
    if (equals == std::string::npos) continue;

    const std::string key = Trim(line.substr(0, equals));
    const std::string value = Trim(line.substr(equals + 1));

    if (key == "capture") {
      if (value == "max") {
        capture_width = 0;
        capture_height = 0;
      } else {
        int width = 0;
        int height = 0;
        if (std::sscanf(value.c_str(), "%dx%d", &width, &height) == 2 &&
            width > 0 && height > 0) {
          capture_width = width;
          capture_height = height;
        } else {
          __android_log_print(ANDROID_LOG_WARN, kTag,
                              "capture.conf: cannot read capture '%s'",
                              value.c_str());
        }
      }
    } else if (key == "lens") {
      if (value == "main") {
        lens = Lens::kMain;
        lens_id.clear();
      } else if (value == "ultrawide") {
        lens = Lens::kUltrawide;
        lens_id.clear();
      } else if (!value.empty()) {
        lens = Lens::kExplicit;
        lens_id = value;
      }
    } else if (key == "shift" || key == "residual") {
      const double parsed = std::atof(value.c_str());
      // A threshold of zero would end a stretch on every frame and one above a
      // half can never be reached, so both are refused rather than silently
      // turning selection off or on.
      if (parsed > 0.0 && parsed < 0.5) {
        (key == "shift" ? min_shift : min_residual) =
            static_cast<float>(parsed);
      } else {
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "capture.conf: %s must be between 0 and 0.5, got "
                            "'%s'",
                            key.c_str(), value.c_str());
      }
    } else if (key == "shutter") {
      if (value == "auto") {
        max_exposure_ns = 0;
      } else {
        // Written the way a shutter speed is written: 1/120.
        int denominator = 0;
        if (std::sscanf(value.c_str(), "1/%d", &denominator) == 1 &&
            denominator > 0) {
          max_exposure_ns = 1000000000LL / denominator;
        } else {
          __android_log_print(ANDROID_LOG_WARN, kTag,
                              "capture.conf: shutter wants auto or 1/N, got "
                              "'%s'",
                              value.c_str());
        }
      }
    } else if (key == "mains") {
      if (value == "off") {
        mains_hz = 0;
      } else {
        const int hz = std::atoi(value.c_str());
        if (hz == 50 || hz == 60) {
          mains_hz = hz;
        } else {
          __android_log_print(ANDROID_LOG_WARN, kTag,
                              "capture.conf: mains wants 50, 60 or off, got "
                              "'%s'",
                              value.c_str());
        }
      }
    } else if (key == "retention") {
      if (value == "all") {
        retention = Retention::kAll;
      } else if (value == "sharpest") {
        retention = Retention::kSharpest;
      } else {
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "capture.conf: cannot read retention '%s'",
                            value.c_str());
      }
    }
  }

  __android_log_print(ANDROID_LOG_INFO, kTag, "capture.conf: %s",
                      Describe().c_str());
  return true;
}

std::string CaptureConfig::Describe() const {
  const char* lens_name = "main";
  if (lens == Lens::kUltrawide) {
    lens_name = "ultrawide";
  } else if (lens == Lens::kExplicit) {
    lens_name = lens_id.c_str();
  }

  char size[24];
  if (capture_width > 0) {
    std::snprintf(size, sizeof(size), "%dx%d", capture_width, capture_height);
  } else {
    std::snprintf(size, sizeof(size), "max");
  }

  char shutter[24];
  if (max_exposure_ns > 0) {
    std::snprintf(shutter, sizeof(shutter), "1/%lld",
                  (long long)(1000000000LL / max_exposure_ns));
  } else {
    std::snprintf(shutter, sizeof(shutter), "auto");
  }

  char buffer[192];
  std::snprintf(buffer, sizeof(buffer),
                "%s %s lens %s shift %.3f residual %.3f shutter %s mains %d",
                size, retention == Retention::kAll ? "all" : "sharpest",
                lens_name, min_shift, min_residual, shutter, mains_hz);
  return buffer;
}

}  // namespace sensor_logger
