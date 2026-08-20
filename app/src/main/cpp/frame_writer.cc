#include "frame_writer.h"

#include <utility>

namespace sensor_logger {

FrameWriter::~FrameWriter() { Stop(); }

bool FrameWriter::Start(Sink sink, size_t max_queued) {
  if (running_) return false;

  sink_ = std::move(sink);
  max_queued_ = max_queued > 0 ? max_queued : 1;
  stopping_ = false;
  dropped_ = 0;
  running_ = true;
  thread_ = std::thread(&FrameWriter::Run, this);
  return true;
}

bool FrameWriter::Submit(PendingFrame&& frame) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) return false;
    if (queue_.size() >= max_queued_) {
      ++dropped_;
      return false;
    }
    queue_.push_back(std::move(frame));
  }
  not_empty_.notify_one();
  return true;
}

void FrameWriter::Stop() {
  if (!running_) return;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  not_empty_.notify_one();
  thread_.join();

  running_ = false;
  queue_.clear();
  sink_ = nullptr;
}

void FrameWriter::Run() {
  while (true) {
    PendingFrame frame;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_empty_.wait(lock, [this] { return stopping_ || !queue_.empty(); });

      // Stopping drains what is already queued before returning; those frames
      // were captured and counted, and dropping them at the end of a session
      // would lose exactly the ones nothing has written yet.
      if (queue_.empty()) return;

      frame = std::move(queue_.front());
      queue_.pop_front();
    }

    sink_(frame);
  }
}

}  // namespace sensor_logger
