#ifndef SENSOR_LOGGER_SESSION_ITEM_H
#define SENSOR_LOGGER_SESSION_ITEM_H

#include <string>

namespace sensor_logger {

struct SessionItem {
  std::string name;
  std::string full_path;
  double megabytes = 0.0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSION_ITEM_H
