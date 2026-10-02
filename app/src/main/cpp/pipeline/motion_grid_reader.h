#ifndef SENSOR_LOGGER_MOTION_GRID_READER_H
#define SENSOR_LOGGER_MOTION_GRID_READER_H

#include <cstdint>
#include <string>
#include <vector>

#include "motion_grid_format.h"

namespace sensor_logger {

// One decoded record from a motion_grid.bin file.
struct GridFrame {
  int64_t timestamp_ns = 0;
  std::vector<uint8_t> grid;
};

// Reads and validates a whole motion_grid.bin file. False on any problem —
// missing file, bad magic, unsupported format_version, or a size that is not
// header-plus-a-whole-number-of-records — with `*error` set to say which.
// Never returns a partially-decoded `frames`; a caller sees either a complete,
// consistent file or a reported error, not a silent truncation.
bool ReadMotionGrid(const std::string& path, MotionGridHeader* header,
                   std::vector<GridFrame>* frames, std::string* error);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_MOTION_GRID_READER_H
