#include "motion_grid_reader.h"

#include <cstring>
#include <fstream>

namespace sensor_logger {

bool ReadMotionGrid(const std::string& path, MotionGridHeader* header,
                   std::vector<GridFrame>* frames, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    *error = "cannot open " + path;
    return false;
  }

  file.read(reinterpret_cast<char*>(header), sizeof(*header));
  if (!file || file.gcount() != static_cast<std::streamsize>(sizeof(*header))) {
    *error = path + ": shorter than the " + std::to_string(sizeof(*header)) +
            "-byte header";
    return false;
  }
  if (std::memcmp(header->magic, kMotionGridMagic, sizeof(header->magic)) != 0) {
    *error = path + ": bad magic — not a motion_grid.bin file";
    return false;
  }
  if (header->format_version != kMotionGridFormatVersion) {
    *error = path + ": unsupported format_version " +
            std::to_string(header->format_version);
    return false;
  }
  if (header->grid_width <= 0 || header->grid_height <= 0) {
    *error = path + ": invalid grid dimensions in header";
    return false;
  }

  file.seekg(0, std::ios::end);
  const int64_t total_size = static_cast<int64_t>(file.tellg());
  const int64_t body_size = total_size - static_cast<int64_t>(sizeof(*header));
  const int64_t record_size =
      MotionGridRecordSize(header->grid_width, header->grid_height);
  if (body_size < 0 || body_size % record_size != 0) {
    *error = path + ": truncated — " + std::to_string(body_size) +
            " bytes after the header is not a multiple of the " +
            std::to_string(record_size) + "-byte record size";
    return false;
  }

  file.seekg(sizeof(*header), std::ios::beg);
  const int64_t record_count = body_size / record_size;
  const size_t grid_bytes =
      static_cast<size_t>(header->grid_width) * header->grid_height;

  std::vector<GridFrame> decoded;
  decoded.reserve(static_cast<size_t>(record_count));
  for (int64_t i = 0; i < record_count; ++i) {
    GridFrame f;
    file.read(reinterpret_cast<char*>(&f.timestamp_ns), sizeof(f.timestamp_ns));
    f.grid.resize(grid_bytes);
    file.read(reinterpret_cast<char*>(f.grid.data()),
             static_cast<std::streamsize>(grid_bytes));
    if (!file) {
      *error = path + ": read failed at record " + std::to_string(i) +
              " of " + std::to_string(record_count);
      return false;
    }
    decoded.push_back(std::move(f));
  }

  *frames = std::move(decoded);
  return true;
}

}  // namespace sensor_logger
