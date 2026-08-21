#include "session_recorder.h"

#include <sys/stat.h>

#include <cinttypes>
#include <cstdio>
#include <utility>

#include "sharpness.h"

namespace sensor_logger {
namespace {

// Every 4th pixel on both axes. Blur is a low-frequency effect, so the estimate
// survives subsampling, and this has to run on every frame the camera produces.
constexpr int32_t kSharpnessStep = 4;

bool MakeDirectory(const std::string& path) {
  if (mkdir(path.c_str(), 0755) == 0) return true;
  // Reusing an existing directory is fine; anything else is a real failure.
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// Camera timestamps are nanoseconds on the boot clock, which is monotonic and
// carries no wall time, so the id is derived from the session's own start
// instant rather than a date.
std::string SessionId(int64_t start_timestamp_ns) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "session_%" PRId64,
                start_timestamp_ns / 1000000);
  return buffer;
}

std::string FrameFilename(int64_t timestamp_ns) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%" PRId64 ".yuv", timestamp_ns);
  return buffer;
}

const char* ChromaLayoutName(ChromaLayout layout) {
  switch (layout) {
    case ChromaLayout::kPlanar:
      return "planar";
    case ChromaLayout::kSemiPlanarUFirst:
      return "semi_planar_uv";
    case ChromaLayout::kSemiPlanarVFirst:
      return "semi_planar_vu";
  }
  return "planar";
}

}  // namespace

SessionRecorder::~SessionRecorder() { Stop(); }

bool SessionRecorder::Start(const std::string& root,
                            int64_t start_timestamp_ns,
                            int32_t sensor_orientation) {
  if (recording_) return false;
  if (!MakeDirectory(root)) return false;

  session_path_ = root + "/" + SessionId(start_timestamp_ns);
  if (!MakeDirectory(session_path_)) return false;
  if (!MakeDirectory(session_path_ + "/frames")) return false;

  frames_.open(session_path_ + "/frames.csv", std::ios::out | std::ios::trunc);
  imu_.open(session_path_ + "/imu.csv", std::ios::out | std::ios::trunc);
  candidates_.open(session_path_ + "/candidates.csv",
                   std::ios::out | std::ios::trunc);
  if (!frames_.is_open() || !imu_.is_open() || !candidates_.is_open()) {
    frames_.close();
    imu_.close();
    candidates_.close();
    return false;
  }

  frames_ << "timestamp_ns,filename,width,height,sharpness,chroma_layout,"
             "luma_row_stride,chroma_row_stride,chroma_pixel_stride,"
             "segment0_length,segment1_length,segment2_length\n";
  imu_ << "timestamp_ns,sensor,x,y,z\n";
  candidates_ << "timestamp_ns,sharpness,shift,residual\n";

  motion_.Reset();
  pending_.Clear();
  writer_.Start([this](PendingFrame& frame) { WriteFrame(frame); });

  start_timestamp_ns_ = start_timestamp_ns;
  sensor_orientation_ = sensor_orientation;
  last_timestamp_ns_ = start_timestamp_ns;
  written_frames_ = 0;
  considered_frames_ = 0;
  frames_without_image_ = 0;
  imu_samples_ = 0;

  recording_ = true;
  return true;
}

void SessionRecorder::Record(const FrameData& frame) {
  if (!recording_) return;

  last_timestamp_ns_ = frame.timestamp_ns;

  if (!frame.image.valid) {
    ++frames_without_image_;
    return;
  }

  ++considered_frames_;

  const ImagePlane& luma = frame.image.planes[0];
  const float sharpness =
      LumaSharpness(luma.data, frame.image.width, frame.image.height,
                    luma.row_stride, kSharpnessStep);

  // Before the decision, so the row describes the frame as it was offered
  // rather than as it was treated.
  const bool moved = motion_.Accept(frame.image);
  WriteCandidate(frame.timestamp_ns, sharpness);

  // Enough movement closes the current stretch: whatever the sharpest frame in
  // it turned out to be is written now, and this frame opens the next.
  if (moved) {
    FlushPending();
    pending_.Set(frame, sharpness);
    return;
  }

  // Still within the stretch — this frame only matters if it beats the leader.
  if (!pending_.valid() || sharpness > pending_.sharpness()) {
    pending_.Set(frame, sharpness);
  }
}

void SessionRecorder::RecordImu(const std::vector<ImuSample>& samples) {
  if (!recording_ || samples.empty()) return;

  for (const ImuSample& sample : samples) {
    imu_ << sample.timestamp_ns << ',' << (sample.is_gyroscope ? "gyro" : "accel")
         << ',' << sample.x << ',' << sample.y << ',' << sample.z << '\n';
  }
  imu_samples_ += static_cast<int64_t>(samples.size());
  imu_.flush();
}

void SessionRecorder::WriteCandidate(int64_t timestamp_ns, float sharpness) {
  candidates_ << timestamp_ns << ',' << sharpness << ',' << motion_.last_shift()
              << ',' << motion_.last_residual() << '\n';
  candidates_.flush();
}

void SessionRecorder::FlushPending() {
  if (!pending_.valid()) return;

  // Dropped rather than written late, and counted by the writer. The queue only
  // fills when the disk is behind, and the frame that would clear the backlog
  // by waiting is the one whose viewpoint the capture is currently at.
  writer_.Submit(std::move(pending_));
  pending_.Clear();
}

void SessionRecorder::WriteFrame(PendingFrame& frame) {
  const std::string filename = FrameFilename(frame.timestamp_ns());
  if (!WriteImage(frame, filename)) {
    ++frames_without_image_;
    return;
  }

  ++written_frames_;

  // Flushed per frame rather than at Stop(). A session that ends by the process
  // being killed — which is how a backgrounded capture usually ends — would
  // otherwise leave the images on disk with an empty index describing them.
  frames_.flush();
}

bool SessionRecorder::WriteImage(const PendingFrame& frame,
                                 const std::string& filename) {
  std::ofstream out(session_path_ + "/frames/" + filename,
                    std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;

  out.write(reinterpret_cast<const char*>(frame.pixels().data()),
            static_cast<std::streamsize>(frame.pixels().size()));
  if (!out.good()) return false;
  out.close();

  frames_ << frame.timestamp_ns() << ',' << filename << ',' << frame.width()
          << ',' << frame.height() << ',' << frame.sharpness() << ','
          << ChromaLayoutName(frame.chroma_layout()) << ','
          << frame.luma_row_stride() << ',' << frame.chroma_row_stride() << ','
          << frame.chroma_pixel_stride();
  for (int32_t i = 0; i < 3; ++i) {
    frames_ << ',' << frame.segment_length(i);
  }
  frames_ << '\n';
  return true;
}

void SessionRecorder::Stop() {
  if (!recording_) return;

  // The last stretch is never closed by movement, so its leader would otherwise
  // be lost.
  FlushPending();

  // Before the streams close: the writer thread is what writes to them.
  writer_.Stop();

  frames_.close();
  imu_.close();
  candidates_.close();
  WriteManifest(last_timestamp_ns_);
  recording_ = false;
}

void SessionRecorder::WriteManifest(int64_t end_timestamp_ns) {
  std::ofstream manifest(session_path_ + "/session.json",
                         std::ios::out | std::ios::trunc);
  if (!manifest.is_open()) return;

  manifest << "{\n"
           << "  \"start_timestamp_ns\": " << start_timestamp_ns_ << ",\n"
           << "  \"end_timestamp_ns\": " << end_timestamp_ns << ",\n"
           << "  \"written_frames\": " << written_frames_.load() << ",\n"
           << "  \"considered_frames\": " << considered_frames_ << ",\n"
           << "  \"frames_without_image\": " << frames_without_image_.load()
           << ",\n"
           << "  \"dropped_frames\": " << writer_.dropped() << ",\n"
           << "  \"imu_samples\": " << imu_samples_ << ",\n"
           << "  \"sensor_orientation\": " << sensor_orientation_ << ",\n"
           << "  \"sensor_orientation_note\": \"degrees clockwise the frames "
              "must be rotated to appear upright; they are written as the "
              "sensor reads them\",\n"
           << "  \"imu_note\": \"m/s^2 and rad/s in the device frame, on the "
              "same clock as the frame timestamps\",\n"
           << "  \"sharpness_metric\": \"variance of Laplacian on luma, "
              "subsampled; comparable only between frames of the same scene\",\n"
           << "  \"selection\": \"sharpest frame of each stretch; a stretch "
              "ends when the picture shifts or stops matching\"\n"
           << "}\n";
}

}  // namespace sensor_logger
