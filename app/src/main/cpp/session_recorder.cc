#include "session_recorder.h"

#include <sys/stat.h>

#include <cinttypes>
#include <cstdio>

namespace sensor_logger {
namespace {

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
  frames_ << "timestamp_ns,filename,width,height,num_planes,"
             "y_row_stride,y_pixel_stride,y_length,"
             "u_row_stride,u_pixel_stride,u_length,"
             "v_row_stride,v_pixel_stride,v_length\n";

  start_timestamp_ns_ = start_timestamp_ns;
  recorded_frames_ = 0;
  dropped_frames_ = 0;
  frames_without_image_ = 0;
  recording_ = true;
  return true;
}

bool SessionRecorder::WriteImage(const FrameData& frame) {
  const CameraImageView& image = frame.image;

  char filename[64];
  std::snprintf(filename, sizeof(filename), "%" PRId64 ".yuv",
                frame.timestamp_ns);

  std::ofstream out(session_path_ + "/frames/" + filename,
                    std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;

  // Planes are concatenated in ARCore's order; frames.csv carries the lengths
  // and strides needed to split them apart again.
  for (int32_t i = 0; i < image.num_planes; ++i) {
    const ImagePlane& plane = image.planes[i];
    if (plane.data == nullptr || plane.length <= 0) continue;
    out.write(reinterpret_cast<const char*>(plane.data), plane.length);
  }

  if (!out.good()) return false;
  out.close();

  frames_ << frame.timestamp_ns << ',' << filename << ',' << image.width << ','
          << image.height << ',' << image.num_planes;
  for (int32_t i = 0; i < 3; ++i) {
    const ImagePlane& plane = image.planes[i];
    frames_ << ',' << plane.row_stride << ',' << plane.pixel_stride << ','
            << plane.length;
  }
  frames_ << '\n';
  return true;
}

void SessionRecorder::Record(const FrameData& frame) {
  if (!recording_) return;

  // A pose from a frame that was not tracking is meaningless; counting the drop
  // is more useful than writing a value that looks valid.
  if (!frame.is_tracking) {
    ++dropped_frames_;
    return;
  }

  // The image is what gets processed later, so a pose without one is not worth
  // a row. Written first for that reason: if it fails, nothing else is logged.
  if (!frame.image.valid || !WriteImage(frame)) {
    ++frames_without_image_;
    return;
  }

  poses_ << frame.timestamp_ns << ',' << frame.pose.translation[0] << ','
         << frame.pose.translation[1] << ',' << frame.pose.translation[2] << ','
         << frame.pose.rotation[0] << ',' << frame.pose.rotation[1] << ','
         << frame.pose.rotation[2] << ',' << frame.pose.rotation[3] << ','
         << frame.intrinsics.focal_x << ',' << frame.intrinsics.focal_y << ','
         << frame.intrinsics.principal_x << ','
         << frame.intrinsics.principal_y << ','
         << frame.intrinsics.image_width << ','
         << frame.intrinsics.image_height << '\n';

  for (const FeaturePoint& point : frame.point_cloud) {
    points_ << frame.timestamp_ns << ',' << point.x << ',' << point.y << ','
            << point.z << ',' << point.confidence << '\n';
  }

  ++recorded_frames_;
}

void SessionRecorder::Stop() {
  if (!recording_) return;

  poses_.close();
  points_.close();
  frames_.close();
  WriteManifest(start_timestamp_ns_);
  recording_ = false;
}

void SessionRecorder::WriteManifest(int64_t end_timestamp_ns) {
  std::ofstream manifest(session_path_ + "/session.json",
                         std::ios::out | std::ios::trunc);
  if (!manifest.is_open()) return;

  manifest << "{\n"
           << "  \"start_timestamp_ns\": " << start_timestamp_ns_ << ",\n"
           << "  \"end_timestamp_ns\": " << end_timestamp_ns << ",\n"
           << "  \"recorded_frames\": " << recorded_frames_ << ",\n"
           << "  \"dropped_frames\": " << dropped_frames_ << ",\n"
           << "  \"frames_without_image\": " << frames_without_image_
           << ",\n"
           << "  \"pose_convention\": \"ARCore world, right-handed, "
              "quaternion (x,y,z,w)\"\n"
           << "}\n";
}

}  // namespace sensor_logger
