#ifndef SENSOR_LOGGER_FRAME_WRITER_H
#define SENSOR_LOGGER_FRAME_WRITER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "pending_frame.h"

namespace sensor_logger {

// Writes captured frames on a thread of its own.
//
// A frame is three megabytes. Writing one from the capture loop stalls that
// loop for as long as the write takes, and ARCore does not hold frames while
// nobody is asking for them — whatever arrived during the stall is gone. At the
// current rate, where a frame is written every few seconds, that is invisible.
// Keeping every frame instead means a write on every iteration, and the loop
// would spend most of its time in the filesystem.
//
// So the capture loop hands the frame over and moves on. The queue is bounded,
// because an unbounded one facing a disk that cannot keep up grows until the
// process is killed. When it is full a frame is dropped and counted rather than
// blocking the loop: dropping one frame costs one viewpoint, while blocking
// costs whatever else the camera produced in the meantime, and a run of drops
// is the honest signal that the capture is asking for more than the device can
// write.
class FrameWriter {
 public:
  // Frames held between the capture loop and the disk. Three megabytes each, so
  // this is the memory the buffering is allowed to cost.
  static constexpr size_t kDefaultMaxQueued = 8;

  FrameWriter() = default;
  ~FrameWriter();

  FrameWriter(const FrameWriter&) = delete;
  FrameWriter& operator=(const FrameWriter&) = delete;

  // Called on the writer thread, one frame at a time and in submission order.
  using Sink = std::function<void(PendingFrame&)>;

  bool Start(Sink sink, size_t max_queued = kDefaultMaxQueued);

  // Takes the frame unless the queue is full, in which case it is left
  // untouched and counted as dropped.
  bool Submit(PendingFrame&& frame);

  // Writes what is already queued, then stops the thread.
  void Stop();

  bool is_running() const { return running_; }
  int64_t dropped() const { return dropped_; }

 private:
  void Run();

  Sink sink_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<PendingFrame> queue_;
  size_t max_queued_ = kDefaultMaxQueued;
  bool stopping_ = false;
  std::atomic<bool> running_{false};
  std::atomic<int64_t> dropped_{0};
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_FRAME_WRITER_H
