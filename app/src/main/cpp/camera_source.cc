#include "camera_source.h"

#include <android/log.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>

#include <cstddef>

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

void OnSessionReady(void*, ACameraCaptureSession*) {}
void OnSessionActive(void*, ACameraCaptureSession*) {}
void OnSessionClosed(void*, ACameraCaptureSession*) {}

}  // namespace

CameraSource::~CameraSource() { Stop(); }

bool CameraSource::SelectCamera() {
  ACameraIdList* ids = nullptr;
  if (ACameraManager_getCameraIdList(manager_, &ids) != ACAMERA_OK ||
      ids == nullptr) {
    return false;
  }

  bool found = false;
  for (int i = 0; i < ids->numCameras && !found; ++i) {
    ACameraMetadata* characteristics = nullptr;
    if (ACameraManager_getCameraCharacteristics(manager_, ids->cameraIds[i],
                                                &characteristics) !=
        ACAMERA_OK) {
      continue;
    }

    ACameraMetadata_const_entry facing{};
    if (ACameraMetadata_getConstEntry(characteristics, ACAMERA_LENS_FACING,
                                      &facing) == ACAMERA_OK &&
        facing.count > 0 &&
        facing.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
      camera_id_ = ids->cameraIds[i];

      // Whether the camera's timestamps share a clock with the sensors. When
      // this is not REALTIME nothing in software can line the two streams up,
      // and a capture that looks fine will be unusable for anything inertial —
      // worth knowing at startup rather than at the workstation.
      ACameraMetadata_const_entry clock{};
      const bool realtime =
          ACameraMetadata_getConstEntry(characteristics,
                                        ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE,
                                        &clock) == ACAMERA_OK &&
          clock.count > 0 &&
          clock.data.u8[0] == ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
      __android_log_print(realtime ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, kTag,
                          "camera %s timestamp source: %s",
                          ids->cameraIds[i], realtime ? "REALTIME" : "UNKNOWN");

      ACameraMetadata_const_entry orientation{};
      if (ACameraMetadata_getConstEntry(characteristics,
                                        ACAMERA_SENSOR_ORIENTATION,
                                        &orientation) == ACAMERA_OK &&
          orientation.count > 0) {
        sensor_orientation_ = orientation.data.i32[0];
      }

      // Every YUV output size the device offers is logged, not just the one
      // chosen. When a capture comes back smaller than expected this is the
      // first thing worth reading.
      ACameraMetadata_const_entry configs{};
      if (ACameraMetadata_getConstEntry(
              characteristics, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
              &configs) == ACAMERA_OK) {
        for (uint32_t e = 0; e + 3 < configs.count; e += 4) {
          const int32_t format = configs.data.i32[e];
          const int32_t width = configs.data.i32[e + 1];
          const int32_t height = configs.data.i32[e + 2];
          const int32_t direction = configs.data.i32[e + 3];

          if (format != AIMAGE_FORMAT_YUV_420_888 ||
              direction !=
                  ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
            continue;
          }

          __android_log_print(ANDROID_LOG_INFO, kTag, "camera %s: yuv %dx%d",
                              camera_id_.c_str(), width, height);

          if (static_cast<int64_t>(width) * height >
              static_cast<int64_t>(capture_width_) * capture_height_) {
            capture_width_ = width;
            capture_height_ = height;
          }

          const bool small_enough = width <= kMaxPreviewWidth;
          const bool bigger_than_current =
              static_cast<int64_t>(width) * height >
              static_cast<int64_t>(preview_width_) * preview_height_;
          if (small_enough && bigger_than_current) {
            preview_width_ = width;
            preview_height_ = height;
          }
        }
      }

      found = capture_width_ > 0;
    }

    ACameraMetadata_free(characteristics);
  }

  ACameraManager_deleteCameraIdList(ids);

  if (!found) return false;

  // A device with nothing under the preview cap can still be previewed from the
  // capture stream; it costs upload bandwidth but it is not a failure.
  if (preview_width_ == 0) {
    preview_width_ = capture_width_;
    preview_height_ = capture_height_;
  }

  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "camera %s: capture %dx%d, preview %dx%d, orientation %d",
                      camera_id_.c_str(), capture_width_, capture_height_,
                      preview_width_, preview_height_, sensor_orientation_);
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

  ACameraCaptureSession_stateCallbacks state{};
  state.context = this;
  state.onReady = OnSessionReady;
  state.onActive = OnSessionActive;
  state.onClosed = OnSessionClosed;

  if (ACameraDevice_createCaptureSession(device_, outputs_, &state, &session_) !=
      ACAMERA_OK) {
    return false;
  }

  return ACameraCaptureSession_setRepeatingRequest(session_, nullptr, 1,
                                                   &request_, nullptr) ==
         ACAMERA_OK;
}

bool CameraSource::Start() {
  if (session_ != nullptr) return true;

  manager_ = ACameraManager_create();
  if (manager_ == nullptr) return false;

  if (!SelectCamera() || !OpenReaders() || !OpenDevice() || !StartSession()) {
    Stop();
    return false;
  }

  __android_log_print(ANDROID_LOG_INFO, kTag, "camera streaming");
  return true;
}

void CameraSource::Stop() {
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
