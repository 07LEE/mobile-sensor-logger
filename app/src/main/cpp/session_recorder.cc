#include "session_recorder.h"

#include <sys/stat.h>
#include <sys/system_properties.h>

#include <cinttypes>
#include <cstdio>
#include <utility>

#include "sharpness.h"

namespace sensor_logger {
namespace {

// Every 4th pixel on both axes. Blur is a low-frequency effect, so the estimate
// survives subsampling, and this has to run on every frame the camera produces.
constexpr int32_t kSharpnessStep = 4;

// A system property, or empty when the platform will not say.
//
// Read straight from the property store rather than through android.os.Build
// over JNI: the values are the same and this needs no Java at all.
std::string SystemProperty(const char* name) {
  char value[PROP_VALUE_MAX] = {};
  const int length = __system_property_get(name, value);
  return length > 0 ? std::string(value, static_cast<size_t>(length))
                    : std::string();
}

// JSON string contents. Model names are vendor strings and there is no promise
// about what is in them.
std::string Escaped(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) >= 0x20) {
      out += c;
    }
  }
  return out;
}

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
                            const CameraInfo& camera,
                            const CaptureConfig& config) {
  if (recording_) return false;
  if (!MakeDirectory(root)) return false;

  session_path_ = root + "/" + SessionId(start_timestamp_ns);
  if (!MakeDirectory(session_path_)) return false;
  if (!MakeDirectory(session_path_ + "/frames")) return false;

  frames_.open(session_path_ + "/frames.csv", std::ios::out | std::ios::trunc);
  imu_.open(session_path_ + "/imu.csv", std::ios::out | std::ios::trunc);
  candidates_.open(session_path_ + "/candidates.csv",
                   std::ios::out | std::ios::trunc);
  capture_.open(session_path_ + "/capture.csv", std::ios::out | std::ios::trunc);
  if (!frames_.is_open() || !imu_.is_open() || !candidates_.is_open() ||
      !capture_.is_open()) {
    frames_.close();
    imu_.close();
    candidates_.close();
    capture_.close();
    return false;
  }

  frames_ << "timestamp_ns,filename,width,height,sharpness,chroma_layout,"
             "luma_row_stride,chroma_row_stride,chroma_pixel_stride,"
             "segment0_length,segment1_length,segment2_length\n";
  imu_ << "timestamp_ns,sensor,x,y,z\n";
  candidates_ << "timestamp_ns,sharpness,shift,residual\n";
  capture_ << "timestamp_ns,exposure_ns,sensitivity,focus_diopters,"
              "rolling_shutter_skew_ns,ae_state,awb_state,af_state,"
              "physical_id\n";

  motion_.Reset();
  motion_.SetThresholds(config.min_shift, config.min_residual);
  pending_.Clear();
  writer_.Start([this](PendingFrame& frame) { WriteFrame(frame); });

  start_timestamp_ns_ = start_timestamp_ns;
  camera_ = camera;
  retention_ = config.retention;
  last_timestamp_ns_ = start_timestamp_ns;
  written_frames_ = 0;
  written_bytes_ = 0;
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

  // Keeping everything skips the comparison entirely: each frame goes straight
  // out, and the sharpness and motion still land in candidates.csv so the
  // selection rule can be judged against a take it did not get to make.
  if (retention_ == Retention::kAll) {
    pending_.Set(frame, sharpness);
    FlushPending();
    return;
  }

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

void SessionRecorder::RecordCaptureResults(
    const std::vector<CaptureResult>& results) {
  if (!recording_ || results.empty()) return;

  for (const CaptureResult& result : results) {
    capture_ << result.timestamp_ns << ',' << result.exposure_ns << ','
             << result.sensitivity << ',' << result.focus_distance << ','
             << result.rolling_shutter_skew_ns << ',' << result.ae_state << ','
             << result.awb_state << ',' << result.af_state << ','
             << result.physical_id << '\n';
  }
  capture_.flush();
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

  // Its buffer went with it; take a spent one so the next copy is not into
  // freshly mapped pages.
  pending_.AdoptBuffer(writer_.TakeBuffer());
}

void SessionRecorder::WriteFrame(PendingFrame& frame) {
  const std::string filename = FrameFilename(frame.timestamp_ns());
  if (!WriteImage(frame, filename)) {
    ++frames_without_image_;
    return;
  }

  ++written_frames_;
  written_bytes_ += static_cast<int64_t>(frame.pixels().size());

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
  capture_.close();
  WriteManifest(last_timestamp_ns_);
  recording_ = false;
}

void SessionRecorder::WriteManifest(int64_t end_timestamp_ns) {
  std::ofstream manifest(session_path_ + "/session.json",
                         std::ios::out | std::ios::trunc);
  if (!manifest.is_open()) return;

  // Which phone this came from. Every measurement in a session is a property of
  // its camera and its sensors, and the format assumptions along with them, so
  // a capture that does not say what took it cannot be checked against another.
  manifest << "{\n"
           << "  \"device\": \""
           << Escaped(SystemProperty("ro.product.manufacturer")) << " "
           << Escaped(SystemProperty("ro.product.model")) << "\",\n"
           << "  \"android_release\": \""
           << Escaped(SystemProperty("ro.build.version.release")) << "\",\n"
           << "  \"android_sdk\": "
           << SystemProperty("ro.build.version.sdk") << ",\n"
           << "  \"start_timestamp_ns\": " << start_timestamp_ns_ << ",\n"
           << "  \"end_timestamp_ns\": " << end_timestamp_ns << ",\n"
           << "  \"written_frames\": " << written_frames_.load() << ",\n"
           << "  \"written_bytes\": " << written_bytes_.load() << ",\n"
           << "  \"considered_frames\": " << considered_frames_ << ",\n"
           << "  \"frames_without_image\": " << frames_without_image_.load()
           << ",\n"
           << "  \"dropped_frames\": " << writer_.dropped() << ",\n"
           << "  \"imu_samples\": " << imu_samples_ << ",\n"
           << "  \"retention\": \""
           << (retention_ == Retention::kAll ? "all" : "sharpest") << "\",\n"
           << "  \"min_shift\": " << motion_.min_shift() << ",\n"
           << "  \"min_residual\": " << motion_.min_residual() << ",\n"
           << "  \"camera_id\": \"" << camera_.id << "\",\n"
           << "  \"focal_length_mm\": " << camera_.focal_length_mm << ",\n"
           << "  \"aperture\": " << camera_.aperture << ",\n"
           << "  \"sensor_size_mm\": [" << camera_.sensor_width_mm << ", "
           << camera_.sensor_height_mm << "],\n"
           << "  \"calibration_note\": \"intrinsics [fx, fy, cx, cy, skew] "
              "and distortion [k1, k2, k3, p1, p2] as the manufacturer measured "
              "them, against pre_correction_active_array; scale if that "
              "differs from the frame size\",\n"
           << "  \"intrinsics\": ";
  if (camera_.has_calibration) {
    manifest << "[" << camera_.intrinsics[0] << ", " << camera_.intrinsics[1]
             << ", " << camera_.intrinsics[2] << ", " << camera_.intrinsics[3]
             << ", " << camera_.intrinsics[4] << "],\n"
             << "  \"distortion\": [" << camera_.distortion[0] << ", "
             << camera_.distortion[1] << ", " << camera_.distortion[2] << ", "
             << camera_.distortion[3] << ", " << camera_.distortion[4] << "],\n"
             << "  \"pre_correction_active_array\": ["
             << camera_.pre_correction_array[0] << ", "
             << camera_.pre_correction_array[1] << ", "
             << camera_.pre_correction_array[2] << ", "
             << camera_.pre_correction_array[3] << "],\n";
  } else {
    manifest << "null,\n";
  }

  if (camera_.has_pose) {
    manifest << "  \"lens_pose_translation_m\": ["
             << camera_.pose_translation[0] << ", "
             << camera_.pose_translation[1] << ", "
             << camera_.pose_translation[2] << "],\n"
             << "  \"lens_pose_rotation_xyzw\": [" << camera_.pose_rotation[0]
             << ", " << camera_.pose_rotation[1] << ", "
             << camera_.pose_rotation[2] << ", " << camera_.pose_rotation[3]
             << "],\n"
             << "  \"lens_pose_reference\": " << camera_.pose_reference
             << ",\n";
  }

  manifest << "  \"stabilisation\": \"off, both optical and digital; either "
              "one changes the camera geometry between frames\",\n"
           << "  \"logical_multi_camera\": "
           << (camera_.logical_multi_camera ? "true" : "false") << ",\n"
           << "  \"sensor_orientation\": " << camera_.sensor_orientation
           << ",\n"
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
