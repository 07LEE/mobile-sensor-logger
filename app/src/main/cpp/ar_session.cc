#include "ar_session.h"

#include <android/log.h>

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

  ArFrame_create(session_, frame_.receive());
  return frame_.get() != nullptr;
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
  } else {
    out->point_cloud.clear();
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
