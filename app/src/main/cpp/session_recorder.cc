#include "session_recorder.h"

#include <sys/stat.h>

#include <cinttypes>
#include <cstdio>

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

// ARCore timestamps are nanoseconds on the system clock, which is monotonic and
// carries no wall time, so the id is derived from the session's own start
// instant rather than a date.
std::string SessionId(int64_t start_timestamp_ns) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "session_%" PRId64,
                start_timestamp_ns / 1000000);
  return buffer;
}

const char* ChromaLayoutName(ChromaLayout layout) {
  switch (layout) {
    case ChromaLayout::kSemiPlanarUFirst:
      return "semi_planar_uv";
    case ChromaLayout::kSemiPlanarVFirst:
      return "semi_planar_vu";
    case ChromaLayout::kPlanar:
      break;
  }
  return "planar";
}

std::string FrameFilename(int64_t timestamp_ns) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%" PRId64 ".yuv", timestamp_ns);
  return buffer;
}

}  // namespace

SessionRecorder::~SessionRecorder() { Stop(); }

bool SessionRecorder::Start(const std::string& root,
                            int64_t start_timestamp_ns) {
  if (recording_) return false;
  if (!MakeDirectory(root)) return false;

  session_path_ = root + "/" + SessionId(start_timestamp_ns);
  if (!MakeDirectory(session_path_)) return false;
  if (!MakeDirectory(session_path_ + "/frames")) return false;

  poses_.open(session_path_ + "/poses.csv", std::ios::out | std::ios::trunc);
  points_.open(session_path_ + "/points.csv", std::ios::out | std::ios::trunc);
  frames_.open(session_path_ + "/frames.csv", std::ios::out | std::ios::trunc);
  if (!poses_.is_open() || !points_.is_open() || !frames_.is_open()) {
    poses_.close();
    points_.close();
    frames_.close();
    return false;
  }

  poses_ << "timestamp_ns,tx,ty,tz,qx,qy,qz,qw,"
            "fx,fy,cx,cy,image_width,image_height\n";
  points_ << "timestamp_ns,x,y,z,confidence\n";
  frames_ << "timestamp_ns,filename,width,height,sharpness,chroma_layout,"
             "luma_row_stride,chroma_row_stride,chroma_pixel_stride,"
             "segment0_length,segment1_length,segment2_length\n";

  start_timestamp_ns_ = start_timestamp_ns;
  last_timestamp_ns_ = start_timestamp_ns;
  written_frames_ = 0;
  considered_frames_ = 0;
  untracked_frames_ = 0;
  frames_without_image_ = 0;
  selector_.Reset();
  pending_.Clear();
  recording_ = true;
  return true;
}

void SessionRecorder::Record(const FrameData& frame) {
  if (!recording_) return;

  last_timestamp_ns_ = frame.timestamp_ns;

  // A pose from a frame that was not tracking is meaningless, and so is the
  // image that goes with it, since nothing could place it later.
  if (!frame.is_tracking) {
    ++untracked_frames_;
    return;
  }

  if (!frame.image.valid) {
    ++frames_without_image_;
    return;
  }

  ++considered_frames_;

  const ImagePlane& luma = frame.image.planes[0];
  const float sharpness =
      LumaSharpness(luma.data, frame.image.width, frame.image.height,
                    luma.row_stride, kSharpnessStep);

  // Moving past the threshold closes the current stretch: whatever the sharpest
  // frame in it turned out to be is written now, and this frame opens the next.
  if (selector_.Accept(frame.pose)) {
    FlushPending();
    pending_.Set(frame, sharpness);
    return;
  }

  // Still within the stretch — this frame only matters if it beats the leader.
  if (!pending_.valid() || sharpness > pending_.sharpness()) {
    pending_.Set(frame, sharpness);
  }
}

void SessionRecorder::FlushPending() {
  if (!pending_.valid()) return;

  const std::string filename = FrameFilename(pending_.timestamp_ns());
  if (!WriteImage(pending_, filename)) {
    ++frames_without_image_;
    pending_.Clear();
    return;
  }

  const CameraPose& pose = pending_.pose();
  const CameraIntrinsics& intrinsics = pending_.intrinsics();

  poses_ << pending_.timestamp_ns() << ',' << pose.translation[0] << ','
         << pose.translation[1] << ',' << pose.translation[2] << ','
         << pose.rotation[0] << ',' << pose.rotation[1] << ','
         << pose.rotation[2] << ',' << pose.rotation[3] << ','
         << intrinsics.focal_x << ',' << intrinsics.focal_y << ','
         << intrinsics.principal_x << ',' << intrinsics.principal_y << ','
         << intrinsics.image_width << ',' << intrinsics.image_height << '\n';

  for (const FeaturePoint& point : pending_.point_cloud()) {
    points_ << pending_.timestamp_ns() << ',' << point.x << ',' << point.y
            << ',' << point.z << ',' << point.confidence << '\n';
  }

  ++written_frames_;
  pending_.Clear();

  // Flushed per frame rather than at Stop(). A session that ends by the process
  // being killed — which is how a backgrounded capture usually ends — would
  // otherwise leave the images on disk with empty CSVs describing them.
  poses_.flush();
  points_.flush();
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

  poses_.close();
  points_.close();
  frames_.close();
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
           << "  \"written_frames\": " << written_frames_ << ",\n"
           << "  \"considered_frames\": " << considered_frames_ << ",\n"
           << "  \"untracked_frames\": " << untracked_frames_ << ",\n"
           << "  \"frames_without_image\": " << frames_without_image_ << ",\n"
           << "  \"sharpness_metric\": \"variance of Laplacian on luma, "
              "subsampled; comparable only between frames of the same scene\",\n"
           << "  \"pose_convention\": \"ARCore world, right-handed, "
              "quaternion (x,y,z,w)\"\n"
           << "}\n";
}

}  // namespace sensor_logger
