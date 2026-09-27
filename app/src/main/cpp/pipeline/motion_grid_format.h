#ifndef SENSOR_LOGGER_MOTION_GRID_FORMAT_H
#define SENSOR_LOGGER_MOTION_GRID_FORMAT_H

#include <cstdint>

namespace sensor_logger {

// On-disk format for a session's motion_grid.bin — see ADR 15. Written by
// SessionRecorder, read by tools/replay_sampling. Little-endian only: every
// platform this ever runs on (ARM on the phone, x86-64 on the workstation that
// replays it) is, so nothing here does byte-order conversion.
//
// Layout: this header once, then one record per considered frame until EOF —
//   record: timestamp_ns (int64)  grid (uint8[grid_width * grid_height])
// grid is row-major, grid[y * grid_width + x], y increasing downward from the
// top of the frame — the same indexing FrameMotion::Downsample() itself uses.
// Fixed-size records (grid_width/grid_height are constant for a session, so
// each record is the same size) are what make truncation trivial to detect: a
// reader checks the bytes after the header are an exact multiple of the
// record size, rather than trusting a partial trailing record.
struct MotionGridHeader {
  char magic[4];
  uint32_t format_version;
  int32_t grid_width;
  int32_t grid_height;
};

static_assert(sizeof(MotionGridHeader) == 16,
             "MotionGridHeader must have no padding — it is written as raw bytes");

constexpr char kMotionGridMagic[4] = {'S', 'L', 'M', 'G'};
constexpr uint32_t kMotionGridFormatVersion = 1;

// Bytes of one record for a session with this grid size — a timestamp plus the
// grid. Shared so the reader's truncation check uses the exact same formula
// the writer's layout implies, rather than a second copy of the arithmetic.
constexpr int64_t MotionGridRecordSize(int32_t grid_width, int32_t grid_height) {
  return static_cast<int64_t>(sizeof(int64_t)) +
        static_cast<int64_t>(grid_width) * static_cast<int64_t>(grid_height);
}

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_MOTION_GRID_FORMAT_H
