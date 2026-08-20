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
//   frames.csv       image dimensions and plane strides, per frame
//   frames/          one raw YUV_420_888 file per frame
//
// Images are written as the planes ARCore hands over, without conversion: there
// is no JPEG encoder in the NDK, and converting on the phone would spend the
// capture's frame budget on work the workstation can do later. The cost is size
// — the strides in frames.csv are what makes the files decodable.
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
  int64_t frames_without_image() const { return frames_without_image_; }
  const std::string& session_path() const { return session_path_; }

 private:
  // Returns false if the image could not be written, in which case the frame is
  // not logged either — a pose whose image is missing cannot be processed.
  bool WriteImage(const FrameData& frame);
  void WriteManifest(int64_t end_timestamp_ns);

  bool recording_ = false;
  std::string session_path_;
  std::ofstream poses_;
  std::ofstream points_;
  std::ofstream frames_;

  int64_t start_timestamp_ns_ = 0;
  int64_t recorded_frames_ = 0;
  int64_t dropped_frames_ = 0;
  int64_t frames_without_image_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSION_RECORDER_H
