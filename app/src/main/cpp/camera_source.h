#ifndef SENSOR_LOGGER_CAMERA_SOURCE_H
#define SENSOR_LOGGER_CAMERA_SOURCE_H

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "camera_image.h"
#include "capture_config.h"

namespace sensor_logger {

// What the camera reports about one frame after it has taken it.
//
// Everything here is a thing that can change between frames and that a
// reconstruction assumes did not. Exposure and sensitivity move the brightness,
// focus moves the effective focal length, and on a logical camera the lens
// itself can change — each of which quietly invalidates a camera model solved
// across the session. Recorded so it can be checked rather than hoped for.
struct CaptureResult {
  int64_t timestamp_ns = 0;
  int64_t exposure_ns = 0;
  int32_t sensitivity = 0;
  float focus_distance = 0.0f;  // diopters; 0 is infinity
  int32_t ae_state = -1;
  int32_t awb_state = -1;
  int32_t af_state = -1;
  std::string physical_id;  // empty unless the camera is logical
};

// The rear camera, opened directly through the NDK.
//
// Two output streams. The capture stream is the largest YUV_420_888 size the
// device offers, since that resolution caps the detail any later reconstruction
// can recover. The preview stream is small on purpose: the screen cannot show
// more, and uploading a full-resolution frame as a texture every frame would
// cost more than the capture does.
//
// Frames are taken by polling rather than through the reader's callback. The
// callback arrives on a thread of the framework's choosing, and taking it there
// would mean holding the capture path's state under a lock for the sake of
// removing a poll that already costs nothing — the loop is woken by the sensor
// queue several hundred times a second regardless.
class CameraSource {
 public:
  CameraSource() = default;
  ~CameraSource();

  CameraSource(const CameraSource&) = delete;
  CameraSource& operator=(const CameraSource&) = delete;

  // Opens the rear camera and starts both streams. Returns false if no camera
  // is usable, which on Android usually means the permission is not held yet.
  //
  // A requested size is taken only if the camera offers it exactly; anything
  // else falls back to the largest, since a silently different resolution is
  // worse than an ignored setting.
  bool Start(const CaptureConfig& config);
  void Stop();

  bool is_running() const { return session_ != nullptr; }

  // Takes the newest capture frame, if one has arrived since the last call.
  // Returns false when none has. The previous frame is released here, so its
  // pixels must not be used past this call.
  bool AcquireFrame(FrameData* out);

  // Same for the preview stream, which is read and dropped independently: the
  // screen wants the newest frame, and the recorder wants every frame.
  bool AcquirePreviewFrame(CameraImageView* out);

  // Holds exposure, white balance and focus where they are.
  //
  // A reconstruction solves one camera across a whole session. Autofocus moves
  // the effective focal length as it hunts, and auto exposure and white balance
  // move the brightness and the colour, so all three break that assumption
  // frame by frame. Locking them is what makes the images comparable.
  //
  // Called once the scene has been metered rather than at startup: the values
  // are whatever the room needed, which is better than any number chosen in
  // advance. Focus is the exception — it goes to the hyperfocal distance, where
  // everything from half of it to infinity is acceptably sharp.
  bool LockExposureAndFocus();

  // Back to metering the scene. Called when a session ends so the next one
  // locks to the room it is actually in rather than to the last one.
  void UnlockExposureAndFocus();

  bool is_locked() const { return locked_; }

  // Moves the capture results that have arrived since the last call into `out`.
  // They come in on a framework thread, so this is where they cross over.
  void DrainResults(std::vector<CaptureResult>* out);

  // Throws away whatever the preview stream has produced. Called when nothing
  // is drawing it: the reader holds a fixed number of buffers and the camera
  // stalls on a stream whose buffers are never given back.
  void DrainPreview();

  const CameraInfo& info() const { return info_; }

  // The rear camera after `id` in the order the system lists them, wrapping
  // round. Survives Stop(), so switching lens does not have to enumerate again.
  std::string NextRearCameraId(const std::string& id) const;

  // What to call the lens in use, relative to the others this device offers.
  // A focal length in millimetres says nothing to someone holding the phone;
  // which of its lenses is pointing at the room does.
  //
  // Named by ratio to the widest rather than by absolute focal length, since
  // what counts as wide depends entirely on the sensor behind it.
  const char* LensName() const;

  // How many rear cameras this device offers the app. One means there is
  // nothing to switch between, which is not an error but does mean the control
  // for it should not exist.
  size_t rear_camera_count() const { return rear_ids_.size(); }

  int32_t capture_width() const { return capture_width_; }
  int32_t capture_height() const { return capture_height_; }
  int32_t preview_width() const { return preview_width_; }
  int32_t preview_height() const { return preview_height_; }

  // Degrees the sensor image must be rotated to appear upright on the display.
  int32_t sensor_orientation() const { return sensor_orientation_; }

 private:
  bool SelectCamera(const CaptureConfig& config);
  bool ReadCamera(const char* id, ACameraMetadata* characteristics,
                  const CaptureConfig& config, CameraInfo* out,
                  int32_t* capture_width, int32_t* capture_height,
                  int32_t* preview_width, int32_t* preview_height) const;
  bool OpenReaders();
  bool OpenDevice();
  bool StartSession();
  static bool ReadImage(AImage* image, CameraImageView* out);

  std::string camera_id_;
  std::vector<std::string> rear_ids_;
  bool locked_ = false;
  float hyperfocal_diopters_ = 0.0f;
  float shortest_focal_mm_ = 0.0f;
  float longest_focal_mm_ = 0.0f;
  CameraInfo info_;
  int32_t sensor_orientation_ = 0;

  int32_t capture_width_ = 0;
  int32_t capture_height_ = 0;
  int32_t preview_width_ = 0;
  int32_t preview_height_ = 0;

  ACameraManager* manager_ = nullptr;
  ACameraDevice* device_ = nullptr;
  AImageReader* capture_reader_ = nullptr;
  AImageReader* preview_reader_ = nullptr;
  ANativeWindow* capture_window_ = nullptr;
  ANativeWindow* preview_window_ = nullptr;
  ACaptureSessionOutputContainer* outputs_ = nullptr;
  ACaptureSessionOutput* capture_output_ = nullptr;
  ACaptureSessionOutput* preview_output_ = nullptr;
  ACameraOutputTarget* capture_target_ = nullptr;
  ACameraOutputTarget* preview_target_ = nullptr;
  ACaptureRequest* request_ = nullptr;
  ACameraCaptureSession* session_ = nullptr;

  // Held so the pixels stay mapped while the caller writes them out, and
  // released when the next frame is taken.
  AImage* capture_image_ = nullptr;
  AImage* preview_image_ = nullptr;

  static void OnCaptureCompleted(void* context, ACameraCaptureSession* session,
                                 ACaptureRequest* request,
                                 const ACameraMetadata* result);

  std::mutex results_mutex_;
  std::deque<CaptureResult> results_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_CAMERA_SOURCE_H
