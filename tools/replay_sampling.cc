// Replays a session's keyframe selection offline from motion_grid.bin and
// candidates.csv, under thresholds that may differ from the ones the session
// actually recorded with. See ADR 15.
//
// Links the same frame_motion.cc / keyframe_selector.cc production code
// SessionRecorder does — this is not a reimplementation of the selection
// rule, so run with the session's own thresholds its output is exactly
// frames.csv's timestamp column, which is the feature's own acceptance test.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "frame_motion.h"
#include "keyframe_selector.h"
#include "motion_grid_format.h"
#include "motion_grid_reader.h"

namespace {

using sensor_logger::FrameMotion;
using sensor_logger::GridFrame;
using sensor_logger::KeyframeSelector;
using sensor_logger::MotionGridHeader;
using sensor_logger::ReadMotionGrid;

// One motion_grid.bin frame plus the sharpness candidates.csv recorded for
// it — everything Replay() below needs, in one place.
struct ReplayFrame {
  int64_t timestamp_ns = 0;
  const std::vector<uint8_t>* grid = nullptr;
  float sharpness = 0.0f;
};

// candidates.csv: timestamp_ns,sharpness,shift,residual — only the first two
// columns matter here; shift/residual are specific to the thresholds that
// session actually ran with (see ADR 15) and are recomputed below instead.
bool ReadSharpness(const std::string& path,
                   std::unordered_map<int64_t, float>* out, std::string* error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    *error = "cannot open " + path;
    return false;
  }

  std::string line;
  std::getline(file, line);  // header row

  while (std::getline(file, line)) {
    if (line.empty()) continue;
    std::istringstream stream(line);
    std::string field;
    if (!std::getline(stream, field, ',')) continue;
    const int64_t timestamp_ns = std::atoll(field.c_str());
    if (!std::getline(stream, field, ',')) continue;
    (*out)[timestamp_ns] = std::strtof(field.c_str(), nullptr);
  }
  return true;
}

struct Candidate {
  bool valid = false;
  int64_t timestamp_ns = 0;
  float sharpness = 0.0f;
  std::vector<uint8_t> grid;

  void Set(const ReplayFrame& frame) {
    valid = true;
    timestamp_ns = frame.timestamp_ns;
    sharpness = frame.sharpness;
    grid = *frame.grid;
  }

  void Clear() {
    valid = false;
    grid.clear();
  }
};

// Mirrors SessionRecorder::Record()'s kSharpest-path decision exactly (same
// FrameMotion/KeyframeSelector calls, same window/stationary candidate
// bookkeeping) but against a pre-recorded grid instead of a live camera
// frame, and without any image data to write.
std::vector<int64_t> Replay(const std::vector<ReplayFrame>& frames,
                            int32_t grid_height, float min_shift,
                            float min_residual) {
  FrameMotion motion;
  motion.SetThresholds(min_shift, min_residual);
  KeyframeSelector selector;

  Candidate window_candidate;
  Candidate stationary_candidate;
  std::vector<int64_t> keyframes;

  for (const ReplayFrame& frame : frames) {
    const bool had_reference = motion.has_reference();
    motion.MeasureGrid(*frame.grid, grid_height);

    if (!had_reference) {
      selector.Confirmed(frame.timestamp_ns);
      window_candidate.Set(frame);
      stationary_candidate.Set(frame);
      continue;
    }

    selector.Observe(frame.timestamp_ns, motion.last_shift(), motion.last_residual(),
                     motion.min_shift(), motion.min_residual());

    if (selector.InWindow() &&
        (!window_candidate.valid || frame.sharpness > window_candidate.sharpness)) {
      window_candidate.Set(frame);
    }
    if (!stationary_candidate.valid || frame.sharpness > stationary_candidate.sharpness) {
      stationary_candidate.Set(frame);
    }

    const KeyframeSelector::ConfirmReason reason = selector.ShouldConfirm();
    if (reason == KeyframeSelector::ConfirmReason::kNone) continue;

    const Candidate& winner =
        (reason == KeyframeSelector::ConfirmReason::kWindow && window_candidate.valid)
            ? window_candidate
            : stationary_candidate;
    motion.Commit(winner.grid, grid_height);
    keyframes.push_back(winner.timestamp_ns);
    window_candidate.Clear();
    stationary_candidate.Clear();
    selector.Confirmed(frame.timestamp_ns);
  }

  // Mirrors SessionRecorder::Stop(): the last stretch is never closed by
  // movement, so whatever led it would otherwise be dropped from the replay
  // entirely. Same preference — the window candidate if the window ever
  // opened, the stationary one otherwise.
  if (window_candidate.valid || stationary_candidate.valid) {
    const Candidate& last =
        window_candidate.valid ? window_candidate : stationary_candidate;
    keyframes.push_back(last.timestamp_ns);
  }

  return keyframes;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: replay_sampling <session_dir> [--min-shift=F] "
                "[--min-residual=F]\n";
    return 2;
  }

  const std::string session_dir = argv[1];
  float min_shift = FrameMotion::kDefaultMinShift;
  float min_residual = FrameMotion::kDefaultMinResidual;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    const std::string shift_flag = "--min-shift=";
    const std::string residual_flag = "--min-residual=";
    if (arg.compare(0, shift_flag.size(), shift_flag) == 0) {
      min_shift = std::strtof(arg.c_str() + shift_flag.size(), nullptr);
    } else if (arg.compare(0, residual_flag.size(), residual_flag) == 0) {
      min_residual = std::strtof(arg.c_str() + residual_flag.size(), nullptr);
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 2;
    }
  }

  if (!FrameMotion::IsValidShiftThreshold(min_shift) ||
      !FrameMotion::IsValidResidualThreshold(min_residual)) {
    std::cerr << "error: thresholds must satisfy 0 < shift < "
              << FrameMotion::kMaxShift << " and 0 < residual < "
              << FrameMotion::kMaxResidual << "\n";
    return 1;
  }

  MotionGridHeader header;
  std::vector<GridFrame> grid_frames;
  std::string error;
  if (!ReadMotionGrid(session_dir + "/motion_grid.bin", &header, &grid_frames, &error)) {
    std::cerr << "error: " << error << "\n";
    return 1;
  }

  std::unordered_map<int64_t, float> sharpness_by_timestamp;
  if (!ReadSharpness(session_dir + "/candidates.csv", &sharpness_by_timestamp, &error)) {
    std::cerr << "error: " << error << "\n";
    return 1;
  }

  std::vector<ReplayFrame> frames;
  frames.reserve(grid_frames.size());
  for (const GridFrame& grid_frame : grid_frames) {
    const auto it = sharpness_by_timestamp.find(grid_frame.timestamp_ns);
    if (it == sharpness_by_timestamp.end()) {
      std::cerr << "error: candidates.csv has no row for timestamp "
                << grid_frame.timestamp_ns << " from motion_grid.bin\n";
      return 1;
    }
    ReplayFrame frame;
    frame.timestamp_ns = grid_frame.timestamp_ns;
    frame.grid = &grid_frame.grid;
    frame.sharpness = it->second;
    frames.push_back(frame);
  }

  const std::vector<int64_t> keyframes =
      Replay(frames, header.grid_height, min_shift, min_residual);

  for (const int64_t timestamp_ns : keyframes) {
    std::cout << timestamp_ns << '\n';
  }
  std::cerr << frames.size() << " frames replayed, " << keyframes.size()
            << " keyframes confirmed (shift=" << min_shift
            << " residual=" << min_residual << ")\n";
  return 0;
}
