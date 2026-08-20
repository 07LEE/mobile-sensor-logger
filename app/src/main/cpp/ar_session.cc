#include "ar_session.h"

#include <android/log.h>

#include <utility>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

void LogError(const char* what) {
  __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", what);
}

}  // namespace

ArSession::~ArSession() {
  // The frame must go before the session that produced it.
  frame_.reset();
  if (session_ != nullptr) {
    ArSession_destroy(session_);
    session_ = nullptr;
  }
}

bool ArSession::Create(JNIEnv* env, jobject context) {
  if (ArSession_create(env, context, &session_) != AR_SUCCESS) {
    LogError("ArSession_create failed");
    session_ = nullptr;
    return false;
  }

  ArConfigHandle config;
  ArConfig_create(session_, config.receive());

  // Blocking mode ties Update() to the camera frame rate, so every camera frame
  // is seen once. The non-blocking mode would let the render loop outrun the
  // camera and repeat frames, which would duplicate captures.
  ArConfig_setUpdateMode(session_, config.get(), AR_UPDATE_MODE_BLOCKING);
  ArConfig_setFocusMode(session_, config.get(), AR_FOCUS_MODE_AUTO);

  if (ArSession_configure(session_, config.get()) != AR_SUCCESS) {
    LogError("ArSession_configure failed");
    return false;
  }

  SelectLargestCpuImageConfig();

  ArFrame_create(session_, frame_.receive());
  return frame_.get() != nullptr;
}

void ArSession::SelectLargestCpuImageConfig() {
  ArCameraConfigFilterHandle filter;
  ArCameraConfigFilter_create(session_, filter.receive());
  if (!filter) return;

  ArCameraConfigListHandle configs;
  ArCameraConfigList_create(session_, configs.receive());
  if (!configs) return;

  ArSession_getSupportedCameraConfigsWithFilter(session_, filter.get(),
                                                configs.get());

  int32_t count = 0;
  ArCameraConfigList_getSize(session_, configs.get(), &count);
  if (count <= 0) return;

  ArCameraConfigHandle best;
  int64_t best_pixels = 0;

  for (int32_t i = 0; i < count; ++i) {
    ArCameraConfigHandle candidate;
    ArCameraConfig_create(session_, candidate.receive());
    ArCameraConfigList_getItem(session_, configs.get(), i, candidate.get());

    int32_t width = 0;
    int32_t height = 0;
    ArCameraConfig_getImageDimensions(session_, candidate.get(), &width,
                                      &height);

    const int64_t pixels = static_cast<int64_t>(width) * height;
    if (pixels > best_pixels) {
      best_pixels = pixels;
      best = std::move(candidate);
    }
  }

  if (!best) return;

  if (ArSession_setCameraConfig(session_, best.get()) != AR_SUCCESS) {
    LogError("ArSession_setCameraConfig failed; keeping the default");
    return;
  }

  int32_t width = 0;
  int32_t height = 0;
  ArCameraConfig_getImageDimensions(session_, best.get(), &width, &height);
  __android_log_print(ANDROID_LOG_INFO, kTag, "CPU image config: %dx%d", width,
                      height);
}

bool ArSession::Resume() {
  if (session_ == nullptr) return false;
  return ArSession_resume(session_) == AR_SUCCESS;
}

void ArSession::Pause() {
  if (session_ != nullptr) ArSession_pause(session_);
}

void ArSession::SetCameraTexture(uint32_t texture_id) {
  if (session_ != nullptr) ArSession_setCameraTextureName(session_, texture_id);
}

void ArSession::SetDisplayGeometry(int rotation, int width, int height) {
  if (session_ != nullptr) {
    ArSession_setDisplayGeometry(session_, rotation, width, height);
  }
}

bool ArSession::Update(FrameData* out) {
  if (session_ == nullptr || !frame_) return false;

  if (ArSession_update(session_, frame_.get()) != AR_SUCCESS) {
    LogError("ArSession_update failed");
    return false;
  }

  ArFrame_getTimestamp(session_, frame_.get(), &out->timestamp_ns);

  // Not an owning handle: the camera belongs to the frame and is invalidated by
  // the next update, so it must not be released here.
  ArCamera* camera = nullptr;
  ArFrame_acquireCamera(session_, frame_.get(), &camera);

  ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
  ArCamera_getTrackingState(session_, camera, &tracking_state);
  out->is_tracking = tracking_state == AR_TRACKING_STATE_TRACKING;

  if (out->is_tracking) {
    ReadPose(camera, &out->pose);
    ReadIntrinsics(camera, &out->intrinsics);
    ReadPointCloud(&out->point_cloud);
    ReadCameraImage(&out->image);
  } else {
    out->point_cloud.clear();
    // Releases the previous frame's image too, so a dropped frame does not
    // leave one held against the pool.
    image_.reset();
    out->image = CameraImageView{};
  }

  ArCamera_release(camera);
  return true;
}

bool ArSession::ReadPose(ArCamera* camera, CameraPose* out) const {
  ArPoseHandle pose;
  ArPose_create(session_, nullptr, pose.receive());
  if (!pose) return false;

  ArCamera_getPose(session_, camera, pose.get());

  // getPoseRaw writes seven floats: quaternion (x, y, z, w) then translation.
  float raw[7] = {0};
  ArPose_getPoseRaw(session_, pose.get(), raw);

  out->rotation = {raw[0], raw[1], raw[2], raw[3]};
  out->translation = {raw[4], raw[5], raw[6]};
  return true;
}

bool ArSession::ReadIntrinsics(ArCamera* camera, CameraIntrinsics* out) const {
  ArCameraIntrinsicsHandle intrinsics;
  ArCameraIntrinsics_create(session_, intrinsics.receive());
  if (!intrinsics) return false;

  // Image intrinsics, not texture intrinsics: they describe the CPU image that
  // gets written to disk, which is what a reconstruction consumes.
  ArCamera_getImageIntrinsics(session_, camera, intrinsics.get());

  ArCameraIntrinsics_getFocalLength(session_, intrinsics.get(), &out->focal_x,
                                    &out->focal_y);
  ArCameraIntrinsics_getPrincipalPoint(session_, intrinsics.get(),
                                       &out->principal_x, &out->principal_y);
  ArCameraIntrinsics_getImageDimensions(session_, intrinsics.get(),
                                        &out->image_width, &out->image_height);
  return true;
}

void ArSession::ReadCameraImage(CameraImageView* out) {
  *out = CameraImageView{};

  // Releasing last frame's image first: ARCore draws from a small pool, and
  // holding two at once exhausts it within a few frames.
  image_.reset();

  const ArStatus status =
      ArFrame_acquireCameraImage(session_, frame_.get(), image_.receive());
  if (status != AR_SUCCESS) {
    // NOT_YET_AVAILABLE happens routinely on the first frames; only a genuinely
    // exhausted pool is worth reporting.
    if (status == AR_ERROR_RESOURCE_EXHAUSTED) {
      LogError("camera image pool exhausted");
    }
    return;
  }

  ArImage_getWidth(session_, image_.get(), &out->width);
  ArImage_getHeight(session_, image_.get(), &out->height);
  ArImage_getNumberOfPlanes(session_, image_.get(), &out->num_planes);

  if (out->num_planes > 3) out->num_planes = 3;

  for (int32_t i = 0; i < out->num_planes; ++i) {
    ImagePlane& plane = out->planes[i];
    ArImage_getPlaneData(session_, image_.get(), i, &plane.data, &plane.length);
    ArImage_getPlaneRowStride(session_, image_.get(), i, &plane.row_stride);
    ArImage_getPlanePixelStride(session_, image_.get(), i, &plane.pixel_stride);
  }

  out->valid = out->num_planes > 0 && out->planes[0].data != nullptr;
}

void ArSession::ReadPointCloud(std::vector<FeaturePoint>* out) const {
  out->clear();

  ArPointCloudHandle cloud;
  if (ArFrame_acquirePointCloud(session_, frame_.get(), cloud.receive()) !=
      AR_SUCCESS) {
    return;
  }

  int32_t count = 0;
  ArPointCloud_getNumberOfPoints(session_, cloud.get(), &count);
  if (count <= 0) return;

  const float* data = nullptr;
  ArPointCloud_getData(session_, cloud.get(), &data);
  if (data == nullptr) return;

  // Four floats per point: position then a confidence in [0, 1].
  out->reserve(static_cast<size_t>(count));
  for (int32_t i = 0; i < count; ++i) {
    const float* p = data + i * 4;
    out->push_back(FeaturePoint{p[0], p[1], p[2], p[3]});
  }
}

}  // namespace sensor_logger
