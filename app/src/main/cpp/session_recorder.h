#ifndef SENSOR_LOGGER_SESSION_RECORDER_H
#define SENSOR_LOGGER_SESSION_RECORDER_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "ar_session.h"
#include "imu_source.h"
#include "keyframe_selector.h"
#include "pending_frame.h"

namespace sensor_logger {

// Writes one capture session to disk.
//
// Layout under <root>/<session_id>/:
//   poses.csv        one row per written frame
//   points.csv       feature points, tagged with the frame they came from
//   frames.csv       image dimensions, plane strides, and sharpness per frame
//   imu.csv          accelerometer and gyroscope readings
//   candidates.csv   pose and sharpness of every frame that was scored
//   frames/          one raw YUV_420_888 file per written frame
//
// candidates.csv is what makes the discarding reviewable. Selection throws away
// most of what the camera produced — thirty-odd images kept out of a couple of
// thousand scored — on thresholds that were guessed rather than measured. The
// images are gone, but a row per scored frame costs about a hundred bytes, so
// the pose, the sharpness, and how far away the scene was all survive. What a
// different threshold would have chosen can then be worked out from a capture
// already taken, instead of from another trip to the same place.
//
// Sharpness drives which frames survive. Handheld capture produces defocused
// and motion-smeared frames continuously, and which ones are bad cannot be
// predicted from the pose, so every tracked frame is scored and the sharpest of
// each stretch of movement is the one written. Keeping every frame is the other
// way to be sure of getting a sharp one, but a frame is megabytes at capture
// resolution and the camera produces thirty a second, so the disk runs out long
// before a useful capture is finished.
//
// A stretch ends when the camera has moved far enough that the next viewpoint
// is worth having, which KeyframeSelector decides.
//
// Images are written as the planes ARCore hands over, without conversion: there
// is no JPEG encoder in the NDK, and converting on the phone would spend the
// capture's frame budget on work the workstation can do later. The strides in
// frames.csv are what makes the files decodable.
//
// Frames that arrive while tracking is lost are dropped rather than written
// with a stale pose, and every category is counted so a session can be judged
// after the fact.
class SessionRecorder {
 public:
  SessionRecorder() = default;
  ~SessionRecorder();

  SessionRecorder(const SessionRecorder&) = delete;
  SessionRecorder& operator=(const SessionRecorder&) = delete;

  // Creates the session directory under `root` and opens the log files.
  bool Start(const std::string& root, int64_t start_timestamp_ns);

  // Offers a frame. Scored and buffered; written only if it ends up the
  // sharpest of its stretch.
  void Record(const FrameData& frame);

  // Inertial samples are written as they arrive, unfiltered. Unlike frames they
  // are not selected: the gaps are what would make the stream unusable for
  // integration, and the volume is trivial next to the images.
  void RecordImu(const std::vector<ImuSample>& samples);

  // Writes any buffered frame, then closes the session.
  void Stop();

  bool is_recording() const { return recording_; }
  int64_t written_frames() const { return written_frames_; }
  int64_t considered_frames() const { return considered_frames_; }
  int64_t untracked_frames() const { return untracked_frames_; }
  int64_t frames_without_image() const { return frames_without_image_; }
  int64_t imu_samples() const { return imu_samples_; }
  const std::string& session_path() const { return session_path_; }

 private:
  void WriteCandidate(const FrameData& frame, float sharpness);
  void FlushPending();
  bool WriteImage(const PendingFrame& frame, const std::string& filename);
  void WriteManifest(int64_t end_timestamp_ns);

  bool recording_ = false;
  std::string session_path_;
  std::ofstream poses_;
  std::ofstream points_;
  std::ofstream frames_;
  std::ofstream imu_;
  std::ofstream candidates_;

  // Reused by the median distance calculation so it does not allocate on every
  // frame the camera produces.
  std::vector<float> distance_scratch_;

  KeyframeSelector selector_;
  PendingFrame pending_;

  int64_t start_timestamp_ns_ = 0;
  int64_t last_timestamp_ns_ = 0;
  int64_t written_frames_ = 0;
  int64_t considered_frames_ = 0;
  int64_t untracked_frames_ = 0;
  int64_t frames_without_image_ = 0;
  int64_t imu_samples_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSION_RECORDER_H
