#include "capture_config.h"

#include <android/log.h>

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

  char buffer[96];
  if (capture_width > 0) {
    std::snprintf(buffer, sizeof(buffer), "%dx%d %s lens %s", capture_width,
                  capture_height,
                  retention == Retention::kAll ? "all" : "sharpest", lens_name);
  } else {
    std::snprintf(buffer, sizeof(buffer), "max %s lens %s",
                  retention == Retention::kAll ? "all" : "sharpest", lens_name);
  }
  return buffer;
}

}  // namespace sensor_logger
