#include "camera_source.h"

#include <android/log.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImage.h>

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

// Enough that the reader can hand one out while the next is being filled, and
// few enough that a stalled writer cannot pin the whole buffer pool. Each is a
// full frame of memory.
constexpr int32_t kCaptureBuffers = 4;
constexpr int32_t kPreviewBuffers = 2;

// The preview only has to be legible on a phone screen. Anything larger is
// texture upload spent on pixels nobody sees.
constexpr int32_t kMaxPreviewWidth = 1280;

void OnDisconnected(void*, ACameraDevice*) {
  __android_log_print(ANDROID_LOG_WARN, kTag, "camera disconnected");
}

void OnError(void*, ACameraDevice*, int error) {
  __android_log_print(ANDROID_LOG_ERROR, kTag, "camera error %d", error);
}

// Results are kept only until the capture loop takes them. If it stops taking
// them the camera is not being read either, and a queue that grew regardless
// would be storing frames nobody is recording.
constexpr size_t kMaxQueuedResults = 256;

int32_t ReadU8(const ACameraMetadata* result, uint32_t tag) {
  ACameraMetadata_const_entry entry{};
  if (ACameraMetadata_getConstEntry(result, tag, &entry) != ACAMERA_OK ||
      entry.count == 0) {
    return -1;
  }
  return entry.data.u8[0];
}

void OnSessionReady(void*, ACameraCaptureSession*) {}
void OnSessionActive(void*, ACameraCaptureSession*) {}
void OnSessionClosed(void*, ACameraCaptureSession*) {}

}  // namespace

CameraSource::~CameraSource() { Stop(); }

void CameraSource::OnCaptureCompleted(void* context, ACameraCaptureSession*,
                                      ACaptureRequest*,
                                      const ACameraMetadata* result) {
  auto* self = static_cast<CameraSource*>(context);
  if (self == nullptr || result == nullptr) return;

  CaptureResult out;
  ACameraMetadata_const_entry entry{};

  // The timestamp is what ties this to the image; without it the row belongs to
  // no frame in particular.
  if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_TIMESTAMP, &entry) !=
          ACAMERA_OK ||
      entry.count == 0) {
    return;
  }
  out.timestamp_ns = entry.data.i64[0];

  if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_EXPOSURE_TIME,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out.exposure_ns = entry.data.i64[0];
  }
  if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_SENSITIVITY,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out.sensitivity = entry.data.i32[0];
  }
  if (ACameraMetadata_getConstEntry(result, ACAMERA_LENS_FOCUS_DISTANCE,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out.focus_distance = entry.data.f[0];
  }
  if (ACameraMetadata_getConstEntry(
          result, ACAMERA_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID, &entry) ==
          ACAMERA_OK &&
      entry.count > 0) {
    out.physical_id.assign(reinterpret_cast<const char*>(entry.data.u8),
                           entry.count);
    // The value is a null-terminated string inside a byte array.
    const size_t end = out.physical_id.find('\0');
    if (end != std::string::npos) out.physical_id.resize(end);
  }

  out.ae_state = ReadU8(result, ACAMERA_CONTROL_AE_STATE);
  out.awb_state = ReadU8(result, ACAMERA_CONTROL_AWB_STATE);
  out.af_state = ReadU8(result, ACAMERA_CONTROL_AF_STATE);

  std::lock_guard<std::mutex> lock(self->results_mutex_);
  if (self->results_.size() >= kMaxQueuedResults) self->results_.pop_front();
  self->results_.push_back(std::move(out));
}

bool CameraSource::LockExposureAndFocus() {
  if (session_ == nullptr || request_ == nullptr) return false;

  const uint8_t on = 1;
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AE_LOCK, 1, &on);
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AWB_LOCK, 1, &on);

  // Focus is set rather than locked. Locking autofocus keeps wherever it last
  // hunted to, which is whatever happened to be under the lens at the time;
  // the hyperfocal distance is a decision instead of an accident.
  const uint8_t af_off = ACAMERA_CONTROL_AF_MODE_OFF;
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AF_MODE, 1, &af_off);

  if (hyperfocal_diopters_ > 0.0f) {
    ACaptureRequest_setEntry_float(request_, ACAMERA_LENS_FOCUS_DISTANCE, 1,
                                   &hyperfocal_diopters_);
  }

  ACameraCaptureSession_captureCallbacks callbacks{};
  callbacks.context = this;
  callbacks.onCaptureCompleted = OnCaptureCompleted;

  if (ACameraCaptureSession_setRepeatingRequest(session_, &callbacks, 1,
                                                &request_, nullptr) !=
      ACAMERA_OK) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "could not lock the camera");
    return false;
  }

  locked_ = true;
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "locked: exposure, white balance, focus at %.2f diopters "
                      "(%.2fm)",
                      hyperfocal_diopters_,
                      hyperfocal_diopters_ > 0.0f ? 1.0f / hyperfocal_diopters_
                                                  : 0.0f);
  return true;
}

void CameraSource::UnlockExposureAndFocus() {
  if (session_ == nullptr || request_ == nullptr || !locked_) return;

  const uint8_t off = 0;
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AE_LOCK, 1, &off);
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AWB_LOCK, 1, &off);

  const uint8_t af_continuous = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_AF_MODE, 1,
                              &af_continuous);

  ACameraCaptureSession_captureCallbacks callbacks{};
  callbacks.context = this;
  callbacks.onCaptureCompleted = OnCaptureCompleted;
  ACameraCaptureSession_setRepeatingRequest(session_, &callbacks, 1, &request_,
                                            nullptr);
  locked_ = false;
}

void CameraSource::DrainResults(std::vector<CaptureResult>* out) {
  out->clear();

  std::lock_guard<std::mutex> lock(results_mutex_);
  out->reserve(results_.size());
  for (CaptureResult& result : results_) out->push_back(std::move(result));
  results_.clear();
}

// Reads one camera's characteristics: what it is, and the two output sizes to
// use if it is chosen.
bool CameraSource::ReadCamera(const char* id, ACameraMetadata* characteristics,
                              const CaptureConfig& config, CameraInfo* out,
                              int32_t* capture_width, int32_t* capture_height,
                              int32_t* preview_width,
                              int32_t* preview_height) const {
  ACameraMetadata_const_entry entry{};

  out->id = id;

  if (ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out->focal_length_mm = entry.data.f[0];
  }
  if (ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_LENS_INFO_AVAILABLE_APERTURES,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out->aperture = entry.data.f[0];
  }
  if (ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_SENSOR_INFO_PHYSICAL_SIZE,
                                    &entry) == ACAMERA_OK &&
      entry.count >= 2) {
    out->sensor_width_mm = entry.data.f[0];
    out->sensor_height_mm = entry.data.f[1];
  }
  if (ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_LENS_INFO_HYPERFOCAL_DISTANCE,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out->hyperfocal_diopters = entry.data.f[0];
  }
  if (ACameraMetadata_getConstEntry(characteristics, ACAMERA_SENSOR_ORIENTATION,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0) {
    out->sensor_orientation = entry.data.i32[0];
  }
  if (ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_REQUEST_AVAILABLE_CAPABILITIES,
                                    &entry) == ACAMERA_OK) {
    for (uint32_t i = 0; i < entry.count; ++i) {
      if (entry.data.u8[i] ==
          ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA) {
        out->logical_multi_camera = true;
      }
    }
  }

  if (ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_LENS_INTRINSIC_CALIBRATION,
                                    &entry) == ACAMERA_OK &&
      entry.count >= 5) {
    for (int i = 0; i < 5; ++i) out->intrinsics[i] = entry.data.f[i];
    out->has_calibration = true;

    if (ACameraMetadata_getConstEntry(characteristics, ACAMERA_LENS_DISTORTION,
                                      &entry) == ACAMERA_OK &&
        entry.count >= 5) {
      for (int i = 0; i < 5; ++i) out->distortion[i] = entry.data.f[i];
    }
    if (ACameraMetadata_getConstEntry(
            characteristics,
            ACAMERA_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, &entry) ==
            ACAMERA_OK &&
        entry.count >= 4) {
      for (int i = 0; i < 4; ++i) out->pre_correction_array[i] = entry.data.i32[i];
    }
  }

  if (ACameraMetadata_getConstEntry(characteristics, ACAMERA_LENS_POSE_ROTATION,
                                    &entry) == ACAMERA_OK &&
      entry.count >= 4) {
    for (int i = 0; i < 4; ++i) out->pose_rotation[i] = entry.data.f[i];
    out->has_pose = true;

    if (ACameraMetadata_getConstEntry(characteristics,
                                      ACAMERA_LENS_POSE_TRANSLATION, &entry) ==
            ACAMERA_OK &&
        entry.count >= 3) {
      for (int i = 0; i < 3; ++i) out->pose_translation[i] = entry.data.f[i];
    }
    if (ACameraMetadata_getConstEntry(characteristics,
                                      ACAMERA_LENS_POSE_REFERENCE, &entry) ==
            ACAMERA_OK &&
        entry.count > 0) {
      out->pose_reference = entry.data.u8[0];
    }
  }

  // Whether this camera's timestamps share a clock with the sensors. When they
  // do not, nothing in software can line the image and inertial streams up, and
  // a capture that looks fine is unusable for anything inertial.
  const bool realtime =
      ACameraMetadata_getConstEntry(characteristics,
                                    ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE,
                                    &entry) == ACAMERA_OK &&
      entry.count > 0 &&
      entry.data.u8[0] == ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
  if (!realtime) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "camera %s timestamp source is not REALTIME", id);
  }

  std::vector<std::pair<int32_t, int32_t>> sizes;
  if (ACameraMetadata_getConstEntry(
          characteristics, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
          &entry) == ACAMERA_OK) {
    for (uint32_t e = 0; e + 3 < entry.count; e += 4) {
      if (entry.data.i32[e] != AIMAGE_FORMAT_YUV_420_888 ||
          entry.data.i32[e + 3] !=
              ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
        continue;
      }
      sizes.emplace_back(entry.data.i32[e + 1], entry.data.i32[e + 2]);
    }
  }
  if (sizes.empty()) return false;

  *capture_width = 0;
  *capture_height = 0;

  // What was asked for, if this camera has it exactly. Falling back silently to
  // a different resolution would be worse than ignoring the setting.
  for (const auto& size : sizes) {
    if (size.first == config.capture_width &&
        size.second == config.capture_height) {
      *capture_width = size.first;
      *capture_height = size.second;
      break;
    }
  }
  if (*capture_width == 0) {
    for (const auto& size : sizes) {
      if (static_cast<int64_t>(size.first) * size.second >
          static_cast<int64_t>(*capture_width) * *capture_height) {
        *capture_width = size.first;
        *capture_height = size.second;
      }
    }
  }

  // The preview is the largest size under the cap *with the same shape as the
  // capture*. Devices offer several aspect ratios, and the largest that fits is
  // often not the capture's — which would put a different field of view on
  // screen from the one being recorded, on the one display whose job is showing
  // what is being recorded. It is also what limits the frame rate: at capture
  // rate this is uploaded every frame.
  const float capture_aspect =
      static_cast<float>(*capture_width) / *capture_height;
  *preview_width = 0;
  *preview_height = 0;
  for (const auto& size : sizes) {
    if (size.first > kMaxPreviewWidth || size.second <= 0) continue;
    if (std::fabs(static_cast<float>(size.first) / size.second -
                  capture_aspect) > 0.02f) {
      continue;
    }
    if (static_cast<int64_t>(size.first) * size.second >
        static_cast<int64_t>(*preview_width) * *preview_height) {
      *preview_width = size.first;
      *preview_height = size.second;
    }
  }
  if (*preview_width == 0) {
    *preview_width = *capture_width;
    *preview_height = *capture_height;
  }

  out->width = *capture_width;
  out->height = *capture_height;
  return true;
}

bool CameraSource::SelectCamera(const CaptureConfig& config) {
  ACameraIdList* ids = nullptr;
  if (ACameraManager_getCameraIdList(manager_, &ids) != ACAMERA_OK ||
      ids == nullptr) {
    return false;
  }

  struct Candidate {
    CameraInfo info;
    int32_t capture_width, capture_height, preview_width, preview_height;
  };
  std::vector<Candidate> back;
  rear_ids_.clear();

  for (int i = 0; i < ids->numCameras; ++i) {
    ACameraMetadata* characteristics = nullptr;
    if (ACameraManager_getCameraCharacteristics(manager_, ids->cameraIds[i],
                                                &characteristics) !=
        ACAMERA_OK) {
      continue;
    }

    ACameraMetadata_const_entry facing{};
    const bool is_back =
        ACameraMetadata_getConstEntry(characteristics, ACAMERA_LENS_FACING,
                                      &facing) == ACAMERA_OK &&
        facing.count > 0 && facing.data.u8[0] == ACAMERA_LENS_FACING_BACK;

    if (is_back) {
      Candidate candidate{};
      if (ReadCamera(ids->cameraIds[i], characteristics, config,
                     &candidate.info, &candidate.capture_width,
                     &candidate.capture_height, &candidate.preview_width,
                     &candidate.preview_height)) {
        // Every rear camera is logged, not just the one chosen. A phone has
        // several lenses of very different focal length and only some are
        // offered to applications; which is which is worth being able to read.
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "camera %s: %.1fmm f/%.1f, yuv up to %dx%d%s",
            candidate.info.id.c_str(), candidate.info.focal_length_mm,
            candidate.info.aperture, candidate.capture_width,
            candidate.capture_height,
            candidate.info.logical_multi_camera ? ", logical" : "");
        rear_ids_.push_back(candidate.info.id);
        back.push_back(std::move(candidate));
      }
    }

    ACameraMetadata_free(characteristics);
  }

  ACameraManager_deleteCameraIdList(ids);
  if (back.empty()) return false;

  shortest_focal_mm_ = back.front().info.focal_length_mm;
  longest_focal_mm_ = back.front().info.focal_length_mm;
  for (const Candidate& candidate : back) {
    const float focal = candidate.info.focal_length_mm;
    if (focal <= 0.0f) continue;
    if (shortest_focal_mm_ <= 0.0f || focal < shortest_focal_mm_) {
      shortest_focal_mm_ = focal;
    }
    if (focal > longest_focal_mm_) longest_focal_mm_ = focal;
  }

  const Candidate* chosen = &back.front();

  if (config.lens == Lens::kUltrawide) {
    // The shortest focal length there is. Also a physical camera rather than a
    // logical one on the tested device, so the lens cannot change mid-session.
    for (const Candidate& candidate : back) {
      if (candidate.info.focal_length_mm > 0.0f &&
          (chosen->info.focal_length_mm <= 0.0f ||
           candidate.info.focal_length_mm < chosen->info.focal_length_mm)) {
        chosen = &candidate;
      }
    }
  } else if (config.lens == Lens::kExplicit) {
    const Candidate* named = nullptr;
    for (const Candidate& candidate : back) {
      if (candidate.info.id == config.lens_id) named = &candidate;
    }
    if (named != nullptr) {
      chosen = named;
    } else {
      __android_log_print(ANDROID_LOG_WARN, kTag,
                          "no rear camera '%s'; using %s",
                          config.lens_id.c_str(), chosen->info.id.c_str());
    }
  }

  info_ = chosen->info;
  hyperfocal_diopters_ = chosen->info.hyperfocal_diopters;
  camera_id_ = chosen->info.id;
  sensor_orientation_ = chosen->info.sensor_orientation;
  capture_width_ = chosen->capture_width;
  capture_height_ = chosen->capture_height;
  preview_width_ = chosen->preview_width;
  preview_height_ = chosen->preview_height;

  if (config.capture_width > 0 &&
      (capture_width_ != config.capture_width ||
       capture_height_ != config.capture_height)) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "camera %s does not offer %dx%d; using %dx%d",
                        camera_id_.c_str(), config.capture_width,
                        config.capture_height, capture_width_, capture_height_);
  }

  // A logical camera can swap physical lenses on its own, and the intrinsics go
  // with them. Nothing here stops that; it is flagged so a capture that comes
  // back inconsistent has somewhere to start.
  if (info_.logical_multi_camera) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "camera %s is logical: the lens may change by itself",
                        camera_id_.c_str());
  }

  if (info_.has_calibration) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "calibration: f %.1f %.1f, c %.1f %.1f, k %.4f %.4f "
                        "%.4f, against %dx%d",
                        info_.intrinsics[0], info_.intrinsics[1],
                        info_.intrinsics[2], info_.intrinsics[3],
                        info_.distortion[0], info_.distortion[1],
                        info_.distortion[2], info_.pre_correction_array[2],
                        info_.pre_correction_array[3]);
  } else {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "camera %s publishes no calibration",
                        camera_id_.c_str());
  }

  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "using camera %s: %.1fmm, capture %dx%d, preview %dx%d, "
                      "orientation %d",
                      camera_id_.c_str(), info_.focal_length_mm, capture_width_,
                      capture_height_, preview_width_, preview_height_,
                      sensor_orientation_);
  return true;
}

bool CameraSource::OpenReaders() {
  if (AImageReader_new(capture_width_, capture_height_,
                       AIMAGE_FORMAT_YUV_420_888, kCaptureBuffers,
                       &capture_reader_) != AMEDIA_OK) {
    return false;
  }
  if (AImageReader_new(preview_width_, preview_height_,
                       AIMAGE_FORMAT_YUV_420_888, kPreviewBuffers,
                       &preview_reader_) != AMEDIA_OK) {
    return false;
  }

  return AImageReader_getWindow(capture_reader_, &capture_window_) ==
             AMEDIA_OK &&
         AImageReader_getWindow(preview_reader_, &preview_window_) == AMEDIA_OK;
}

bool CameraSource::OpenDevice() {
  ACameraDevice_StateCallbacks callbacks{};
  callbacks.context = this;
  callbacks.onDisconnected = OnDisconnected;
  callbacks.onError = OnError;

  return ACameraManager_openCamera(manager_, camera_id_.c_str(), &callbacks,
                                   &device_) == ACAMERA_OK;
}

bool CameraSource::StartSession() {
  if (ACaptureSessionOutputContainer_create(&outputs_) != ACAMERA_OK) {
    return false;
  }

  if (ACaptureSessionOutput_create(capture_window_, &capture_output_) !=
          ACAMERA_OK ||
      ACaptureSessionOutput_create(preview_window_, &preview_output_) !=
          ACAMERA_OK) {
    return false;
  }

  ACaptureSessionOutputContainer_add(outputs_, capture_output_);
  ACaptureSessionOutputContainer_add(outputs_, preview_output_);

  // TEMPLATE_PREVIEW rather than STILL_CAPTURE: this is a continuous stream at
  // frame rate, not a shutter press, and the still template asks the device for
  // per-shot processing that would not keep up.
  if (ACameraDevice_createCaptureRequest(device_, TEMPLATE_PREVIEW, &request_) !=
      ACAMERA_OK) {
    return false;
  }

  if (ACameraOutputTarget_create(capture_window_, &capture_target_) !=
          ACAMERA_OK ||
      ACameraOutputTarget_create(preview_window_, &preview_target_) !=
          ACAMERA_OK) {
    return false;
  }

  ACaptureRequest_addTarget(request_, capture_target_);
  ACaptureRequest_addTarget(request_, preview_target_);

  // Stabilisation off, both kinds. Digital stabilisation crops and warps each
  // frame independently and optical stabilisation moves the lens, so either one
  // leaves a camera whose geometry changes frame to frame — which is the one
  // thing a reconstruction assumes does not happen. What they buy is a steadier
  // picture, and sharpness selection is already the answer to shake here.
  const uint8_t video_stabilization =
      ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
  ACaptureRequest_setEntry_u8(request_, ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE,
                              1, &video_stabilization);

  const uint8_t optical_stabilization =
      ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_OFF;
  ACaptureRequest_setEntry_u8(request_, ACAMERA_LENS_OPTICAL_STABILIZATION_MODE,
                              1, &optical_stabilization);

  ACameraCaptureSession_stateCallbacks state{};
  state.context = this;
  state.onReady = OnSessionReady;
  state.onActive = OnSessionActive;
  state.onClosed = OnSessionClosed;

  if (ACameraDevice_createCaptureSession(device_, outputs_, &state, &session_) !=
      ACAMERA_OK) {
    return false;
  }

  ACameraCaptureSession_captureCallbacks callbacks{};
  callbacks.context = this;
  callbacks.onCaptureCompleted = OnCaptureCompleted;

  return ACameraCaptureSession_setRepeatingRequest(session_, &callbacks, 1,
                                                   &request_, nullptr) ==
         ACAMERA_OK;
}

const char* CameraSource::LensName() const {
  if (info_.focal_length_mm <= 0.0f) return "CAMERA";

  // With one rear camera there is nothing to be wide or narrow relative to.
  // Calling it ultra-wide because it happens to be the shortest focal length
  // present would be a lie on most phones.
  if (rear_ids_.size() < 2 || shortest_focal_mm_ <= 0.0f) return "CAMERA";

  const float ratio = info_.focal_length_mm / shortest_focal_mm_;
  if (ratio < 1.5f) return "ULTRAWIDE";
  if (ratio < 3.0f) return "WIDE";
  return "TELE";
}

std::string CameraSource::NextRearCameraId(const std::string& id) const {
  if (rear_ids_.empty()) return id;

  for (size_t i = 0; i < rear_ids_.size(); ++i) {
    if (rear_ids_[i] == id) return rear_ids_[(i + 1) % rear_ids_.size()];
  }
  return rear_ids_.front();
}

bool CameraSource::Start(const CaptureConfig& config) {
  if (session_ != nullptr) return true;

  manager_ = ACameraManager_create();
  if (manager_ == nullptr) return false;

  if (!SelectCamera(config) || !OpenReaders() || !OpenDevice() || !StartSession()) {
    Stop();
    return false;
  }

  __android_log_print(ANDROID_LOG_INFO, kTag, "camera streaming");
  return true;
}

void CameraSource::Stop() {
  locked_ = false;
  {
    std::lock_guard<std::mutex> lock(results_mutex_);
    results_.clear();
  }

  if (capture_image_ != nullptr) {
    AImage_delete(capture_image_);
    capture_image_ = nullptr;
  }
  if (preview_image_ != nullptr) {
    AImage_delete(preview_image_);
    preview_image_ = nullptr;
  }

  if (session_ != nullptr) {
    ACameraCaptureSession_stopRepeating(session_);
    ACameraCaptureSession_close(session_);
    session_ = nullptr;
  }
  if (request_ != nullptr) {
    ACaptureRequest_free(request_);
    request_ = nullptr;
  }
  if (capture_target_ != nullptr) {
    ACameraOutputTarget_free(capture_target_);
    capture_target_ = nullptr;
  }
  if (preview_target_ != nullptr) {
    ACameraOutputTarget_free(preview_target_);
    preview_target_ = nullptr;
  }
  if (outputs_ != nullptr) {
    ACaptureSessionOutputContainer_free(outputs_);
    outputs_ = nullptr;
  }
  if (capture_output_ != nullptr) {
    ACaptureSessionOutput_free(capture_output_);
    capture_output_ = nullptr;
  }
  if (preview_output_ != nullptr) {
    ACaptureSessionOutput_free(preview_output_);
    preview_output_ = nullptr;
  }
  if (device_ != nullptr) {
    ACameraDevice_close(device_);
    device_ = nullptr;
  }

  // After the device, so nothing is still writing into them.
  if (capture_reader_ != nullptr) {
    AImageReader_delete(capture_reader_);
    capture_reader_ = nullptr;
  }
  if (preview_reader_ != nullptr) {
    AImageReader_delete(preview_reader_);
    preview_reader_ = nullptr;
  }

  if (manager_ != nullptr) {
    ACameraManager_delete(manager_);
    manager_ = nullptr;
  }
}

bool CameraSource::ReadImage(AImage* image, CameraImageView* out) {
  *out = CameraImageView{};

  if (AImage_getWidth(image, &out->width) != AMEDIA_OK ||
      AImage_getHeight(image, &out->height) != AMEDIA_OK ||
      AImage_getNumberOfPlanes(image, &out->num_planes) != AMEDIA_OK) {
    return false;
  }

  for (int32_t i = 0; i < out->num_planes && i < 3; ++i) {
    uint8_t* data = nullptr;
    int32_t length = 0;
    if (AImage_getPlaneData(image, i, &data, &length) != AMEDIA_OK) return false;

    out->planes[i].data = data;
    out->planes[i].length = length;
    AImage_getPlaneRowStride(image, i, &out->planes[i].row_stride);
    AImage_getPlanePixelStride(image, i, &out->planes[i].pixel_stride);
  }

  // Semi-planar chroma reports two planes that are the same buffer one byte
  // apart. Copying both would duplicate a megabyte a frame and, read back as
  // two planes, swap the colours.
  if (out->num_planes >= 3 && out->planes[1].data != nullptr &&
      out->planes[2].data != nullptr) {
    const ptrdiff_t offset = out->planes[2].data - out->planes[1].data;
    if (offset == 1) {
      out->chroma_layout = ChromaLayout::kSemiPlanarUFirst;
    } else if (offset == -1) {
      out->chroma_layout = ChromaLayout::kSemiPlanarVFirst;
    } else {
      out->chroma_layout = ChromaLayout::kPlanar;
    }
  }

  out->valid = true;
  return true;
}

bool CameraSource::AcquireFrame(FrameData* out) {
  if (capture_reader_ == nullptr) return false;

  AImage* image = nullptr;
  // The newest, not the next: a backlog means the disk fell behind, and the
  // frames to skip are the stale ones rather than the current viewpoint.
  if (AImageReader_acquireLatestImage(capture_reader_, &image) != AMEDIA_OK ||
      image == nullptr) {
    return false;
  }

  if (capture_image_ != nullptr) AImage_delete(capture_image_);
  capture_image_ = image;

  *out = FrameData{};
  if (AImage_getTimestamp(image, &out->timestamp_ns) != AMEDIA_OK) return false;
  return ReadImage(image, &out->image);
}

void CameraSource::DrainPreview() {
  if (preview_reader_ == nullptr) return;

  AImage* image = nullptr;
  if (AImageReader_acquireLatestImage(preview_reader_, &image) != AMEDIA_OK ||
      image == nullptr) {
    return;
  }
  AImage_delete(image);
}

bool CameraSource::AcquirePreviewFrame(CameraImageView* out) {
  if (preview_reader_ == nullptr) return false;

  AImage* image = nullptr;
  if (AImageReader_acquireLatestImage(preview_reader_, &image) != AMEDIA_OK ||
      image == nullptr) {
    return false;
  }

  if (preview_image_ != nullptr) AImage_delete(preview_image_);
  preview_image_ = image;

  return ReadImage(image, out);
}

}  // namespace sensor_logger
