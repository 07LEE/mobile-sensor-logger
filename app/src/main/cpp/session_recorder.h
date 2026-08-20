#ifndef SENSOR_LOGGER_SESSION_RECORDER_H
#define SENSOR_LOGGER_SESSION_RECORDER_H

#include <cstdint>
#include <fstream>
#include <string>

#include "ar_session.h"

namespace sensor_logger {

// Writes one capture session to disk.
//
// Layout under <root>/<session_id>/:
//   poses.csv        one row per recorded frame
//   points.csv       feature points, tagged with the frame they came from
//   frames/          reserved for image data
//
// Frames that arrive while tracking is lost are dropped rather than written
// with a stale pose, and the count is kept so a session can be judged after
// the fact.
class SessionRecorder {
 public:
  SessionRecorder() = default;
  ~SessionRecorder();

  SessionRecorder(const SessionRecorder&) = delete;
  SessionRecorder& operator=(const SessionRecorder&) = delete;

  // Creates the session directory under `root` and opens the log files.
  bool Start(const std::string& root, int64_t start_timestamp_ns);

  // Appends a frame. Ignored unless recording, and skipped when not tracking.
  void Record(const FrameData& frame);

  void Stop();

  bool is_recording() const { return recording_; }
  int64_t recorded_frames() const { return recorded_frames_; }
  int64_t dropped_frames() const { return dropped_frames_; }
  const std::string& session_path() const { return session_path_; }

 private:
  void WriteManifest(int64_t end_timestamp_ns);

  bool recording_ = false;
  std::string session_path_;
  std::ofstream poses_;
  std::ofstream points_;

  int64_t start_timestamp_ns_ = 0;
  int64_t recorded_frames_ = 0;
  int64_t dropped_frames_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSION_RECORDER_H
