#ifndef SENSOR_LOGGER_SESSION_RECORDER_H
#define SENSOR_LOGGER_SESSION_RECORDER_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "camera_image.h"
#include "camera_source.h"
#include "capture_config.h"
#include "frame_motion.h"
#include "frame_writer.h"
#include "imu_source.h"
#include "pending_frame.h"
#include "session_item.h"

namespace sensor_logger {

// Writes one capture session to disk.
//
// Layout under <root>/<session_id>/:
//   frames.csv       dimensions, plane strides and sharpness per written frame
//   imu.csv          accelerometer and gyroscope readings
//   candidates.csv   sharpness and motion of every frame that was scored
//   capture.csv      exposure, sensitivity, focus and lens, per frame
//   frames/          one raw YUV_420_888 file per written frame
//
// No poses. The device records what a reconstruction is computed from and
// leaves the computing to a workstation, which has the compute for a global
// optimisation and can be re-run against the same capture as many times as the
// result needs.
//
// Sharpness drives which frames survive. Handheld capture produces defocused
// and motion-smeared frames continuously, and which ones are bad cannot be
// predicted from anything but the picture, so every frame is scored and the
// sharpest of each stretch of movement is the one written. Keeping every frame
// is the other way to be sure of getting a sharp one, but a frame is megabytes
// at capture resolution and the camera produces thirty a second, so the disk
// runs out long before a useful capture is finished.
//
// A stretch ends when the picture has changed enough to be worth another frame,
// which FrameMotion decides.
//
// candidates.csv is what makes the discarding reviewable. Selection throws away
// most of what the camera produced, on thresholds that were guessed rather than
// measured. The images are gone, but a row per scored frame costs about fifty
// bytes, so what a different threshold would have chosen can be worked out from
// a capture already taken instead of from another trip to the same place.
//
// Images are written as the camera hands them over, without conversion: there
// is no JPEG encoder in the NDK, and converting on the phone would spend the
// capture's frame budget on work the workstation can do later. The strides in
// frames.csv are what makes the files decodable.
class SessionRecorder {
 public:
  SessionRecorder() = default;
  ~SessionRecorder();

  SessionRecorder(const SessionRecorder&) = delete;
  SessionRecorder& operator=(const SessionRecorder&) = delete;

  // Creates the session directory under `root` and opens the log files.
  bool Start(const std::string& root, int64_t start_timestamp_ns,
             const CameraInfo& camera, const CaptureConfig& config);

  // Offers a frame. Scored and buffered; written only if it ends up the
  // sharpest of its stretch.
  void Record(const FrameData& frame);

  // Inertial samples are written as they arrive, unfiltered. Unlike frames they
  // are not selected: the gaps are what would make the stream unusable for
  // integration, and the volume is trivial next to the images.
  void RecordImu(const std::vector<ImuSample>& samples);

  // What the camera reported about frames it has finished taking. Written
  // whole rather than selected: these are the settings a reconstruction
  // assumes held still, and the only way to know they did is to have them.
  void RecordCaptureResults(const std::vector<CaptureResult>& results);

  // Writes any buffered frame, then closes the session.
  void Stop();

  bool is_recording() const { return recording_; }
  int64_t written_frames() const { return written_frames_; }
  int64_t considered_frames() const { return considered_frames_; }
  int64_t frames_without_image() const { return frames_without_image_; }
  int64_t dropped_frames() const { return writer_.dropped(); }
  int64_t imu_samples() const { return imu_samples_; }

  // Bytes of image written, and how long the session has been running by the
  // camera's clock. Between them they say how fast the disk is filling, which
  // is the number that decides how long a capture can go on.
  int64_t written_bytes() const { return written_bytes_; }
  int64_t elapsed_ns() const {
    return last_timestamp_ns_ > start_timestamp_ns_
               ? last_timestamp_ns_ - start_timestamp_ns_
               : 0;
  }
  const std::string& session_path() const { return session_path_; }

  // Scans <root> directory and returns list of session items.
  static std::vector<SessionItem> GetSessions(const std::string& session_root);

  // Deletes one specific session directory.
  static bool DeleteSessionPath(const std::string& full_path);

  // How far the picture slid since the last written frame, and how much of it
  // no offset lines up. Between them they are the only signal the device has
  // that a capture is covering new ground.
  float last_shift() const { return motion_.last_shift(); }
  float last_residual() const { return motion_.last_residual(); }

 private:
  void WriteCandidate(int64_t timestamp_ns, float sharpness);
  void FlushPending();

  // Both run on the writer thread. Nothing else touches frames_,
  // written_frames_ or frames_without_image_ while it is running, which is what
  // keeps them free of locking.
  void WriteFrame(PendingFrame& frame);
  bool WriteImage(const PendingFrame& frame, const std::string& filename);

  void WriteManifest(int64_t end_timestamp_ns);

  bool recording_ = false;
  std::string session_path_;
  std::ofstream frames_;
  std::ofstream imu_;
  std::ofstream candidates_;
  std::ofstream capture_;

  FrameMotion motion_;
  PendingFrame pending_;
  FrameWriter writer_;

  int64_t start_timestamp_ns_ = 0;
  CameraInfo camera_{};
  Retention retention_ = Retention::kSharpest;
  int64_t last_timestamp_ns_ = 0;
  std::atomic<int64_t> written_frames_{0};
  std::atomic<int64_t> written_bytes_{0};
  std::atomic<int64_t> frames_without_image_{0};
  int64_t considered_frames_ = 0;
  int64_t imu_samples_ = 0;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSION_RECORDER_H
