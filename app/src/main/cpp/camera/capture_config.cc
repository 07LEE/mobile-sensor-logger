#include "capture_config.h"

#include <android/log.h>

#include <algorithm>
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
    } else if (key == "fps") {
      if (value == "auto") {
        fixed_fps = 0;
      } else {
        const int parsed = std::atoi(value.c_str());
        // Above the sensor's own ceiling at full resolution the request would
        // just be refused, but there is no characteristic read here to check
        // it against, so this only catches the typos, not the too-high ones.
        if (parsed > 0 && parsed <= 240) {
          fixed_fps = parsed;
        } else {
          __android_log_print(ANDROID_LOG_WARN, kTag,
                              "capture.conf: fps wants auto or a positive "
                              "number, got '%s'",
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

bool CaptureConfig::Save(const std::string& directory) const {
  const std::string path = directory + "/capture.conf";
  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    __android_log_print(ANDROID_LOG_WARN, kTag, "could not write %s",
                        path.c_str());
    return false;
  }

  if (capture_width > 0) {
    file << "capture = " << capture_width << "x" << capture_height << "\n";
  } else {
    file << "capture = max\n";
  }

  file << "retention = " << (retention == Retention::kAll ? "all" : "sharpest")
       << "\n";

  if (lens == Lens::kUltrawide) {
    file << "lens = ultrawide\n";
  } else if (lens == Lens::kExplicit) {
    file << "lens = " << lens_id << "\n";
  } else {
    file << "lens = main\n";
  }

  // Clamped to what Load() actually accepts (0, 0.5) — otherwise a value that
  // reached this struct out of range some other way would round-trip through
  // capture.conf as something Load() then silently rejects back to whatever
  // default was compiled in, with nothing on disk showing that happened.
  const auto clamp_threshold = [](float v) {
    return std::clamp(v, 0.001f, 0.499f);
  };
  file << "shift = " << clamp_threshold(min_shift) << "\n";
  file << "residual = " << clamp_threshold(min_residual) << "\n";

  if (max_exposure_ns > 0) {
    file << "shutter = 1/" << (1000000000LL / max_exposure_ns) << "\n";
  } else {
    file << "shutter = auto\n";
  }

  if (mains_hz == 0) {
    file << "mains = off\n";
  } else {
    file << "mains = " << mains_hz << "\n";
  }

  if (fixed_fps > 0) {
    file << "fps = " << fixed_fps << "\n";
  } else {
    file << "fps = auto\n";
  }

  return file.good();
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

  char fps[16];
  if (fixed_fps > 0) {
    std::snprintf(fps, sizeof(fps), "%d", fixed_fps);
  } else {
    std::snprintf(fps, sizeof(fps), "auto");
  }

  char buffer[224];
  std::snprintf(
      buffer, sizeof(buffer),
      "%s %s lens %s shift %.3f residual %.3f shutter %s mains %d fps %s",
      size, retention == Retention::kAll ? "all" : "sharpest", lens_name,
      min_shift, min_residual, shutter, mains_hz, fps);
  return buffer;
}

}  // namespace sensor_logger
