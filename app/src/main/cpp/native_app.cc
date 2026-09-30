#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <sys/statvfs.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "camera_image.h"
#include "camera_source.h"
#include "capture_config.h"
#include "device_status.h"
#include "frame_motion.h"
#include "imu_source.h"
#include "input.h"
#include "location.h"
#include "log.h"
#include "permissions.h"
#include "periodic_schedule.h"
#include "preview_renderer.h"
#include "recording_service.h"
#include "session_recorder.h"

namespace {

using sensor_logger::CameraImageView;
using sensor_logger::CameraSource;
using sensor_logger::CaptureResult;
using sensor_logger::CaptureConfig;
using sensor_logger::Retention;
using sensor_logger::FrameData;
using sensor_logger::FrameMotion;
using sensor_logger::ImuSample;
using sensor_logger::Action;
using sensor_logger::ImuSource;
using sensor_logger::Input;
using sensor_logger::PendingFrame;
using sensor_logger::PeriodicSchedule;
using sensor_logger::PreviewRenderer;
using sensor_logger::SessionRecorder;
using sensor_logger::SessionsOverlay;
using sensor_logger::GetLocationData;
using sensor_logger::HasCameraPermission;
using sensor_logger::LocationData;
using sensor_logger::LogError;
using sensor_logger::LogInfo;
using sensor_logger::LogOptionalPermissions;
using sensor_logger::ReadThermalSample;
using sensor_logger::RequestCameraPermission;
using sensor_logger::StartRecordingService;
using sensor_logger::StopRecordingService;
using sensor_logger::ThermalSample;

constexpr char kTag[] = "sensor_logger";

// Holds everything that depends on the window, so it can be torn down and
// rebuilt when the app goes to background and comes back.
struct AppState {
  android_app* app = nullptr;

  EGLDisplay display = EGL_NO_DISPLAY;
  EGLSurface surface = EGL_NO_SURFACE;
  EGLContext context = EGL_NO_CONTEXT;
  int width = 0;
  int height = 0;

  CameraSource camera;
  ImuSource imu;
  PreviewRenderer preview;
  SessionRecorder recorder;
  Input input;
  CaptureConfig config;
  bool capturing = false;

  // Free space, refreshed on a timer rather than per frame: statvfs on the
  // shared storage is a round trip through the filesystem daemon, and the
  // number moves slowly enough that a couple of seconds stale is honest.
  int64_t free_bytes = 0;
  PeriodicSchedule free_space_schedule{
      sensor_logger::kFreeSpaceCheckIntervalNs};
  int64_t last_timestamp_ns = 0;

  // Same cadence as the free-space schedule, for the same reason: cheap enough
  // to poll often. Both schedules use CLOCK_BOOTTIME rather than camera frames,
  // so a stalled camera or a different FPS does not change their meaning.
  // Polled whether or not anything is recording — battery
  // percent is shown on the HUD all the time — but only written to
  // thermal.csv while recording, where the point (see ADR 9) is lining a
  // throttling event up against a capture.csv gap a couple of seconds wide,
  // not catching the exact frame it started on.
  PeriodicSchedule thermal_schedule{
      sensor_logger::kDeviceStatusSampleIntervalNs};
  ThermalSample last_thermal;

  // The camera's own account of the last frame it finished, kept whether or not
  // anything is being recorded. Watching exposure and focus settle is how the
  // decision to start is made, and they only settle while nothing is locked.
  sensor_logger::CaptureResult last_result;

  // A metered result pinned ahead of time by the lock button, so recording
  // can be started from wherever the phone happens to be pointed without
  // locking onto whatever that happens to meter to — pointing at a blown-out
  // window right as the record key is pressed otherwise dooms the whole
  // session. Stays pinned across sessions until the button clears it.
  bool exposure_pinned = false;
  sensor_logger::CaptureResult pinned_result;

  // Why the last session ended, when it was not asked to. Shown until the next
  // one starts, since the reason is worth more at the moment of finding out
  // than in a log read afterwards.
  const char* stop_reason = nullptr;

  // The picture is not drawn at all unless asked for.
  //
  // The numbers are what the screen is for during a capture; the picture is for
  // aiming, which is a thing done between captures more than during one. Not
  // drawing it also skips uploading a frame as two textures every frame, which
  // is what was capping the frame rate, and leaves nearly the whole OLED panel
  // switched off.
  bool preview_visible = false;
  bool sessions_overlay_visible = false;
  bool pro_panel_visible = false;
  int pending_delete_index = -1;
  int sessions_page = 0;
  bool permission_granted = false;

  // The sessions overlay's contents. Fetched once when the overlay opens
  // (each session's size costs a recursive stat of its whole directory tree,
  // too expensive to redo every frame) and updated in place on delete, rather
  // than re-read. Delete taps also resolve against this same list rather than
  // a fresh directory read: readdir order is not guaranteed stable between
  // calls, so a second read could put a different session at the tapped index
  // and delete the wrong one.
  std::vector<sensor_logger::SessionItem> cached_sessions;

  // Set by the PRO panel's EXTRINSIC action — see
  // docs/adr/0011-pro-panel-extrinsic-capture-button.md. `extrinsic_prev_retention`
  // is what `config.retention` gets put back to when the recording it started
  // stops; unlike the RETENTION toolbar button, this never reaches
  // capture.conf, so the file reads the same after the take as before it.
  bool extrinsic_mode_active = false;
  Retention extrinsic_prev_retention = Retention::kSharpest;
};

std::string FilesRoot(android_app* app) {
  return app->activity->externalDataPath != nullptr
             ? app->activity->externalDataPath
             : app->activity->internalDataPath;
}

std::string SessionRoot(android_app* app) {
  return FilesRoot(app) + "/sessions";
}

bool InitDisplay(AppState* state) {
  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (state->display == EGL_NO_DISPLAY) return false;
  eglInitialize(state->display, nullptr, nullptr);

  const EGLint config_attribs[] = {EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES3_BIT,
                                   EGL_SURFACE_TYPE,
                                   EGL_WINDOW_BIT,
                                   EGL_BLUE_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_NONE};

  EGLConfig config;
  EGLint num_configs = 0;
  eglChooseConfig(state->display, config_attribs, &config, 1, &num_configs);
  if (num_configs < 1) return false;

  state->surface =
      eglCreateWindowSurface(state->display, config, state->app->window, nullptr);
  if (state->surface == EGL_NO_SURFACE) return false;

  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  state->context =
      eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attribs);
  if (state->context == EGL_NO_CONTEXT) return false;

  if (eglMakeCurrent(state->display, state->surface, state->surface,
                     state->context) == EGL_FALSE) {
    return false;
  }

  eglQuerySurface(state->display, state->surface, EGL_WIDTH, &state->width);
  eglQuerySurface(state->display, state->surface, EGL_HEIGHT, &state->height);

  if (!state->preview.Init()) {
    LogError("could not build the preview shaders");
    return false;
  }
  state->preview.SetViewport(state->width, state->height);

  return true;
}

void TerminateDisplay(AppState* state) {
  // Before the context goes away, since the objects belong to it.
  state->preview.Destroy();

  if (state->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (state->context != EGL_NO_CONTEXT) {
      eglDestroyContext(state->display, state->context);
    }
    if (state->surface != EGL_NO_SURFACE) {
      eglDestroySurface(state->display, state->surface);
    }
    eglTerminate(state->display);
  }

  state->display = EGL_NO_DISPLAY;
  state->context = EGL_NO_CONTEXT;
  state->surface = EGL_NO_SURFACE;
}

void StartCapture(AppState* state) {
  if (state->capturing) return;
  if (!state->permission_granted) return;

  if (!state->camera.Start(state->config)) {
    LogError("could not start the camera");
    return;
  }

  state->capturing = true;

  // Inertial capture starts with the camera and stops with it. Both run for the
  // whole session, and the samples covering a gap in the images are exactly the
  // ones that could bridge it later.
  //
  // Not started earlier: the sensor queue shares the looper, and until there is
  // a camera to wait on, a stream of sensor events would hold the main loop in
  // its drain loop, where the camera permission is never re-checked.
  state->imu.Start(state->app->looper, "com.sensor.logger");
}

void StopCapture(AppState* state) {
  if (!state->capturing) return;

  // Mirrors ToggleRecording's stop branch: a real end location and a proper
  // service teardown, not just whichever call site happens to remember to do
  // it. Previously only ToggleRecording and the disk-full path did this, so a
  // recording still in progress when the activity is destroyed (e.g. the
  // system reclaiming it under memory pressure) got an invalid end_location
  // and left the "background recording active" notification stuck with
  // nothing left running to clear it.
  if (state->recorder.is_recording()) {
    LocationData end_loc = GetLocationData(state->app);
    state->recorder.Stop(end_loc);
    StopRecordingService(state->app);
  } else {
    state->recorder.Stop();
  }
  state->imu.Stop();
  state->camera.Stop();
  state->capturing = false;
}

int64_t FreeBytes(const std::string& path) {
  struct statvfs stats{};
  if (statvfs(path.c_str(), &stats) != 0) return 0;
  return static_cast<int64_t>(stats.f_bavail) * stats.f_frsize;
}

// Bytes as something readable at arm's length. Two significant figures is as
// much as anyone acts on.
std::string Bytes(int64_t bytes) {
  char buffer[32];
  if (bytes >= 1000LL * 1000 * 1000) {
    std::snprintf(buffer, sizeof(buffer), "%.1FGB",
                  static_cast<double>(bytes) / (1000.0 * 1000 * 1000));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%lldMB",
                  static_cast<long long>(bytes / (1000 * 1000)));
  }
  return buffer;
}

// Below this, recording stops on its own.
//
// Not zero, and not small. A session that runs the disk to the last byte cannot
// write its own manifest — the settings, the counts, the calibration all go
// with it — so a capture that filled a phone ends up unusable anyway. This is
// enough room for a hundred frames at full resolution, which is plenty for
// everything that has to be written after the last one.
constexpr int64_t kMinimumFreeBytes = 2LL * 1000 * 1000 * 1000;

// Puts retention and the EXTRINSIC HUD line back to how they were before
// that mode's take started. Called from every place a recording can stop —
// not just ToggleRecording below, but the disk-full auto-stop too, which
// doesn't go through it.
void RevertExtrinsicModeIfActive(AppState* state) {
  if (!state->extrinsic_mode_active) return;
  state->config.retention = state->extrinsic_prev_retention;
  state->extrinsic_mode_active = false;
}

void PollPeriodicStatus(AppState* state) {
  const int64_t now_ns = sensor_logger::BoottimeNowNs();

  if (state->free_space_schedule.Due(now_ns)) {
    // The files directory rather than the sessions directory: the latter is
    // not created until the first session starts, and statvfs on a path that
    // does not exist reports no space at all.
    state->free_bytes = FreeBytes(FilesRoot(state->app));

    if (state->recorder.is_recording() &&
        state->free_bytes < kMinimumFreeBytes) {
      LocationData end_loc = GetLocationData(state->app);
      state->recorder.Stop(end_loc);
      StopRecordingService(state->app);
      state->camera.UnlockExposureAndFocus();
      RevertExtrinsicModeIfActive(state);
      state->stop_reason = "STOPPED - DISK FULL";
      __android_log_print(ANDROID_LOG_WARN, kTag,
                          "stopped: %lld bytes free, %lld written",
                          (long long)state->free_bytes,
                          (long long)state->recorder.written_frames());
    }
  }

  // Read for the HUD even outside a recording. A recording persists the
  // query's own CLOCK_BOOTTIME timestamp, not the last camera timestamp.
  if (state->thermal_schedule.Due(now_ns)) {
    state->last_thermal = ReadThermalSample(state->app);
    if (state->recorder.is_recording()) {
      state->recorder.RecordThermal(
          state->last_thermal.timestamp_ns,
          state->last_thermal.thermal_status,
          state->last_thermal.battery_temp_c);
    }
  }
}

void ToggleRecording(AppState* state) {
  if (state->recorder.is_recording()) {
    LocationData end_loc = GetLocationData(state->app);
    state->recorder.Stop(end_loc);
    StopRecordingService(state->app);
    state->camera.UnlockExposureAndFocus();
    RevertExtrinsicModeIfActive(state);
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "stopped: %lld written, %lld considered, %lld dropped, %lld lost "
        "upstream (of %lld camera captures)",
        (long long)state->recorder.written_frames(),
        (long long)state->recorder.considered_frames(),
        (long long)state->recorder.dropped_frames(),
        (long long)(state->recorder.camera_completed_captures() -
                    state->recorder.considered_frames()),
        (long long)state->recorder.camera_completed_captures());
    return;
  }

  if (state->last_timestamp_ns == 0) {
    LogError("no frame yet; not starting");
    return;
  }

  if (state->free_bytes < kMinimumFreeBytes) {
    state->stop_reason = "NOT ENOUGH SPACE TO START";
    LogError("not enough free space to start recording");
    return;
  }

  // Locked before the first frame is kept, not at startup: by now the camera
  // has been metering this room for as long as it took to point the phone at
  // it, so what it settled on is what gets held. Unless a metered result was
  // pinned ahead of time, in which case that is what gets held instead,
  // regardless of what the phone is pointed at right now.
  state->camera.LockExposureAndFocus(
      state->exposure_pinned ? state->pinned_result : state->last_result,
      state->config.max_exposure_ns, state->config.mains_hz);

  LocationData start_loc = GetLocationData(state->app);
  if (state->recorder.Start(SessionRoot(state->app), state->last_timestamp_ns,
                            state->camera.info(), state->config,
                            state->imu.info(), start_loc)) {
    StartRecordingService(state->app);
    state->stop_reason = nullptr;
    // So the first thermal sample lands promptly rather than waiting out
    // however much of the schedule was left over from a previous session.
    state->thermal_schedule.Reset();
    __android_log_print(ANDROID_LOG_INFO, kTag, "recording to %s",
                        state->recorder.session_path().c_str());
  } else {
    LogError("could not start recording");
  }
}

// Starts a camera-IMU extrinsic capture take: forces retention to `all` in
// memory only (RevertExtrinsicModeIfActive above puts it back, and neither
// side of that touches capture.conf), closes the PRO panel, and starts
// recording immediately — the same as pressing volume-down. See
// docs/adr/0011-pro-panel-extrinsic-capture-button.md.
void StartExtrinsicCapture(AppState* state) {
  if (state->recorder.is_recording()) return;

  state->extrinsic_prev_retention = state->config.retention;
  state->config.retention = Retention::kAll;
  state->extrinsic_mode_active = true;
  state->pro_panel_visible = false;

  ToggleRecording(state);
  if (!state->recorder.is_recording()) {
    // ToggleRecording refused to start (no frame yet, not enough space) —
    // nothing on that path knows to undo the switch to `all` above, so this
    // is the only place left that can.
    RevertExtrinsicModeIfActive(state);
  }
}

// Cycles through the rear cameras.
//
// Refused while recording. Changing lens changes the intrinsics, so a session
// cannot continue across one, and ending a capture because a key was brushed
// against a coat is a trip wasted. Stopping first makes it deliberate: down to
// stop, up to change, down to start again.
void NextLens(AppState* state) {
  if (state->camera.rear_camera_count() < 2) return;

  if (state->recorder.is_recording()) {
    LogInfo("not changing lens while recording; stop first");
    return;
  }

  const std::string current = state->camera.info().id;
  state->camera.Stop();

  // A pin holds whatever the old lens metered; the new lens has a different
  // sensor and aperture, so carrying it over into the next LockExposureAndFocus
  // would lock onto a baseline that was never measured on this lens.
  state->exposure_pinned = false;

  state->config.lens = sensor_logger::Lens::kExplicit;
  state->config.lens_id = state->camera.NextRearCameraId(current);

  if (!state->camera.Start(state->config)) {
    LogError("could not switch lens");
    // The camera is now stopped, not merely idle: without this, `capturing`
    // stays true from before this call and StartCapture's own guard
    // ("if (capturing) return") blocks every future attempt to restart it,
    // including the one the next resume/init-window event would trigger.
    state->capturing = false;
    return;
  }
  state->last_timestamp_ns = 0;
}

// Switches between keeping only the sharpest frame of each stretch and
// keeping every frame.
//
// Refused while recording for the same reason as the lens: SessionRecorder
// only reads `retention` when a session starts, so flipping it mid-session
// would silently keep doing whatever the session started with, and a button
// that looks like it did something but did not is worse than one that
// visibly refuses.
void ToggleRetention(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing retention while recording; stop first");
    return;
  }

  state->config.retention = state->config.retention == Retention::kAll
                                ? Retention::kSharpest
                                : Retention::kAll;
  state->config.Save(FilesRoot(state->app));
}

// Steps the shutter cap through a fixed list of speeds and back to auto.
// LockExposureAndFocus reads max_exposure_ns fresh at the start of every
// recording, so unlike fps this needs no camera restart to take effect.
void CycleShutter(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing shutter while recording; stop first");
    return;
  }

  // Denominators rather than nanoseconds directly, since that is how the
  // value reads on the panel and in capture.conf; 0 stands for auto.
  constexpr int64_t kSteps[] = {0, 1000, 500, 250, 125, 60};
  constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

  int64_t current_denominator =
      state->config.max_exposure_ns > 0
          ? 1000000000LL / state->config.max_exposure_ns
          : 0;
  size_t index = 0;
  for (size_t i = 0; i < kStepCount; ++i) {
    if (kSteps[i] == current_denominator) {
      index = i;
      break;
    }
  }
  const int64_t next = kSteps[(index + 1) % kStepCount];
  state->config.max_exposure_ns = next > 0 ? 1000000000LL / next : 0;
  state->config.Save(FilesRoot(state->app));
}

// Steps mains frequency through 60 / 50 / off.
void CycleMains(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing mains while recording; stop first");
    return;
  }

  if (state->config.mains_hz == 60) {
    state->config.mains_hz = 50;
  } else if (state->config.mains_hz == 50) {
    state->config.mains_hz = 0;
  } else {
    state->config.mains_hz = 60;
  }
  state->config.Save(FilesRoot(state->app));
}

// Steps the shift threshold through presets either side of the default.
// Lower closes a stretch on less movement, so the camera is kept to shorter,
// more frequent stretches and the frames written sit closer together —
// steadier overlap, spent faster against the free space budget.
void CycleShift(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing shift while recording; stop first");
    return;
  }

  constexpr auto& kSteps = FrameMotion::kShiftPresets;
  constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

  size_t index = 2;  // 0.12, the default, if nothing close enough matches
  for (size_t i = 0; i < kStepCount; ++i) {
    if (std::fabs(kSteps[i] - state->config.min_shift) < 0.005f) {
      index = i;
      break;
    }
  }
  state->config.min_shift = kSteps[(index + 1) % kStepCount];
  state->config.shift_rejected = false;
  state->config.Save(FilesRoot(state->app));
}

// Same for residual, which sits at about half of shift by convention (0.06
// beside a shift default of 0.12).
void CycleResidual(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing residual while recording; stop first");
    return;
  }

  constexpr float kSteps[] = {0.03f, 0.045f, 0.06f, 0.09f};
  constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

  size_t index = 2;  // 0.06, the default, if nothing close enough matches
  for (size_t i = 0; i < kStepCount; ++i) {
    if (std::fabs(kSteps[i] - state->config.min_residual) < 0.0025f) {
      index = i;
      break;
    }
  }
  state->config.min_residual = kSteps[(index + 1) % kStepCount];
  state->config.residual_rejected = false;
  state->config.Save(FilesRoot(state->app));
}

// Steps the fps pin through auto / 30 / 24 / 15.
//
// Unlike shutter and mains, this one takes a camera restart to actually take
// effect: fixed_fps is only read once, in CameraSource::Start, and baked
// into the repeating request StartSession builds. Saving the new value
// without restarting would leave the panel and capture.conf agreeing with
// each other and both disagreeing with the camera still running under the
// old one.
void CycleFps(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing fps while recording; stop first");
    return;
  }

  constexpr int32_t kSteps[] = {0, 30, 24, 15};
  constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

  size_t index = 0;
  for (size_t i = 0; i < kStepCount; ++i) {
    if (kSteps[i] == state->config.fixed_fps) {
      index = i;
      break;
    }
  }
  state->config.fixed_fps = kSteps[(index + 1) % kStepCount];
  state->config.Save(FilesRoot(state->app));

  state->camera.Stop();
  if (!state->camera.Start(state->config)) {
    LogError("could not restart camera with new fps");
    // See NextLens: without this, `capturing` stays true over a camera that
    // is actually stopped, and nothing can restart it until the app is left
    // and reopened.
    state->capturing = false;
    return;
  }
  state->last_timestamp_ns = 0;
}

// Puts shutter, fps, mains, shift and residual back to their defaults in one
// tap, rather than cycling each one back around individually — shutter alone
// can be five taps from auto. What this is for is not stranding a test value
// (a shutter cap or a pinned fps tried out while figuring out a scene) in
// capture.conf, where it would silently carry into a take that never meant
// to use it.
void ResetProSettings(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not resetting pro settings while recording; stop first");
    return;
  }

  const bool fps_changed = state->config.fixed_fps != 0;

  state->config.max_exposure_ns = 0;
  state->config.fixed_fps = 0;
  state->config.mains_hz = 60;
  state->config.min_shift = 0.12f;
  state->config.min_residual = 0.06f;
  state->config.shift_rejected = false;
  state->config.residual_rejected = false;
  state->config.Save(FilesRoot(state->app));

  if (fps_changed) {
    state->camera.Stop();
    if (!state->camera.Start(state->config)) {
      LogError("could not restart camera after pro reset");
      // See NextLens: without this, `capturing` stays true over a camera
      // that is actually stopped, and nothing can restart it until the app
      // is left and reopened.
      state->capturing = false;
      return;
    }
    state->last_timestamp_ns = 0;
  }
}

// Pins the current metered result so a recording started later locks onto it
// rather than onto whatever the phone happens to be pointed at when the
// record key is pressed — tapping again clears the pin.
//
// Refused while recording for the same reason as the others: the pin only
// takes effect at the next recording start, so changing it mid-session would
// look like it did something and would not.
void ToggleExposurePin(AppState* state) {
  if (state->recorder.is_recording()) {
    LogInfo("not changing the exposure pin while recording; stop first");
    return;
  }

  if (state->exposure_pinned) {
    state->exposure_pinned = false;
    return;
  }

  state->pinned_result = state->last_result;
  state->exposure_pinned = true;
}

// Shutter speed the way it is written on a camera, since 41621860 nanoseconds
// is not a number anyone reads.
std::string Shutter(int64_t exposure_ns) {
  char buffer[24];
  if (exposure_ns <= 0) {
    std::snprintf(buffer, sizeof(buffer), "-");
  } else if (exposure_ns < 500000000) {
    std::snprintf(buffer, sizeof(buffer), "1/%lld",
                  (long long)(1000000000LL / exposure_ns));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1FS",
                  static_cast<double>(exposure_ns) / 1e9);
  }
  return buffer;
}

// The readout drawn over the preview.
//
// These are the numbers that decide whether a capture is worth keeping, and
// without them on screen they exist only in logcat, which cannot be read while
// walking around with the phone — the only time they could change anything.
std::vector<std::string> StatusLines(const AppState& state) {
  const SessionRecorder& recorder = state.recorder;
  char buffer[64];
  std::vector<std::string> lines;

  // Written to fit the narrower of the two layouts, so one set serves both: the
  // compact one spreads them across the screen and the expanded one shrinks
  // them out of the way of the picture.
  const int64_t elapsed_s = recorder.elapsed_ns() / 1000000000;

  // How long the free space lasts at the rate this capture is actually filling
  // it. The rate follows the resolution, how much of the scene is moving and
  // how many frames survive selection, so a figure worked out beforehand would
  // be wrong for the capture in hand.
  long long minutes = -1;
  if (elapsed_s > 2 && recorder.written_bytes() > 0) {
    const double per_second =
        static_cast<double>(recorder.written_bytes()) / elapsed_s;
    minutes = static_cast<long long>(state.free_bytes / per_second / 60.0);
  }

  if (recorder.is_recording()) {
    if (minutes >= 0) {
      std::snprintf(buffer, sizeof(buffer), "REC %lld:%02lld   %lld MIN LEFT",
                    (long long)(elapsed_s / 60), (long long)(elapsed_s % 60),
                    minutes);
    } else {
      std::snprintf(buffer, sizeof(buffer), "REC %lld:%02lld",
                    (long long)(elapsed_s / 60), (long long)(elapsed_s % 60));
    }
  } else if (state.stop_reason != nullptr) {
    std::snprintf(buffer, sizeof(buffer), "%s", state.stop_reason);
  } else {
    std::snprintf(buffer, sizeof(buffer), "VOL DOWN");
  }
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "%lld KEPT / %lld SEEN",
                (long long)recorder.written_frames(),
                (long long)recorder.considered_frames());
  lines.emplace_back(buffer);

  std::snprintf(buffer, sizeof(buffer), "%s USED  %s FREE",
                Bytes(recorder.written_bytes()).c_str(),
                Bytes(state.free_bytes).c_str());
  lines.emplace_back(buffer);

  // Each number against the threshold that would end the stretch, so the
  // readout says how close the next frame is rather than just where things
  // stand.
  std::snprintf(buffer, sizeof(buffer), "SHIFT %.0F/%.0F  DIFF %.0F/%.0F",
                recorder.last_shift() * 100.0f,
                state.config.min_shift * 100.0f,
                recorder.last_residual() * 100.0f,
                state.config.min_residual * 100.0f);
  lines.emplace_back(buffer);

  // A capture.conf value outside what FrameMotion can measure is kept at
  // whatever it was before the file was read; this is the only place that
  // says so without checking logcat.
  if (state.config.shift_rejected) {
    lines.emplace_back("SHIFT CONFIG REJECTED");
  }
  if (state.config.residual_rejected) {
    lines.emplace_back("RESIDUAL CONFIG REJECTED");
  }

  // Lens and retention are already on their own buttons below; the resolution
  // is not shown anywhere else.
  std::snprintf(buffer, sizeof(buffer), "%dX%d",
                state.camera.capture_width(), state.camera.capture_height());
  lines.emplace_back(buffer);

  // What the camera is doing with the picture, locked or not. Watching these
  // settle is how the moment to start recording is chosen, so they are shown
  // whether or not anything is being recorded.
  const CaptureResult& result = state.last_result;
  char focus[16];
  if (result.focus_distance > 0.0f) {
    std::snprintf(focus, sizeof(focus), "%.2FM", 1.0f / result.focus_distance);
  } else {
    std::snprintf(focus, sizeof(focus), "INF");
  }

  std::snprintf(buffer, sizeof(buffer), "%s ISO%d %s%s%s",
                Shutter(result.exposure_ns).c_str(), result.sensitivity, focus,
                state.camera.is_locked() ? " LOCKED" : "",
                state.exposure_pinned ? " PIN" : "");
  lines.emplace_back(buffer);

  // Anything but zero in the first two means the capture is outrunning the
  // disk, which nothing else on screen would show.
  std::snprintf(buffer, sizeof(buffer), "DROP %lld NOIMG %lld IMU %lldK",
                (long long)recorder.dropped_frames(),
                (long long)recorder.frames_without_image(),
                (long long)(recorder.imu_samples() / 1000));
  lines.emplace_back(buffer);

  // Shown whether or not anything is recording — a long session (the
  // multi-hour static IMU-noise captures this app has been used for) is
  // exactly when battery is worth watching without leaving the app to check.
  // Either field can be a sentinel (unavailable on this device/build); each
  // is only printed when its own reading came back.
  if (state.last_thermal.battery_percent >= 0 ||
      state.last_thermal.battery_temp_c > -1000.0f) {
    char percent[8] = "--%";
    if (state.last_thermal.battery_percent >= 0) {
      std::snprintf(percent, sizeof(percent), "%d%%",
                    state.last_thermal.battery_percent);
    }
    char temp[12] = "--C";
    if (state.last_thermal.battery_temp_c > -1000.0f) {
      std::snprintf(temp, sizeof(temp), "%.1FC",
                    state.last_thermal.battery_temp_c);
    }
    std::snprintf(buffer, sizeof(buffer), "BATT %s %s", percent, temp);
    lines.emplace_back(buffer);
  }

  return lines;
}

void HandleCommand(android_app* app, int32_t cmd) {
  auto* state = static_cast<AppState*>(app->userData);

  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (app->window != nullptr && InitDisplay(state)) {
        StartCapture(state);
      }
      break;

    case APP_CMD_TERM_WINDOW:
      TerminateDisplay(state);
      break;

    case APP_CMD_PAUSE:
      // If we are currently recording, the Foreground Service keeps camera & IMU
      // capture active in the background. Only stop capture if not recording.
      if (!state->recorder.is_recording()) {
        StopCapture(state);
      } else {
        state->recorder.RecordLifecycleEvent(state->last_timestamp_ns,
                                             "background");
      }
      break;

    case APP_CMD_RESUME:
      if (state->display != EGL_NO_DISPLAY) StartCapture(state);
      if (state->recorder.is_recording()) {
        state->recorder.RecordLifecycleEvent(state->last_timestamp_ns,
                                             "foreground");
      }
      break;

    default:
      break;
  }
}

}  // namespace

extern "C" void android_main(android_app* app) {
  AppState state;
  state.app = app;
  app->userData = &state;
  app->onAppCmd = HandleCommand;

  state.config.Load(FilesRoot(app));
  state.input.Attach(app);

  state.permission_granted = HasCameraPermission(app);
  if (!state.permission_granted) {
    RequestCameraPermission(app);
  } else {
    LogOptionalPermissions(app);
  }

  FrameData frame;
  CameraImageView preview_image;
  std::vector<ImuSample> imu_samples;
  std::vector<sensor_logger::CaptureResult> capture_results;

  while (true) {
    int events = 0;
    android_poll_source* source = nullptr;

    // The timeout is zero once the camera is running, so the loop paces itself
    // on frames arriving rather than on the event queue, and 50ms before that
    // so it is not spinning while waiting for a window.
    int ident = 0;
    while ((ident = ALooper_pollOnce(state.capturing ? 0 : 50, nullptr, &events,
                                     reinterpret_cast<void**>(&source))) >= 0) {
      if (source != nullptr) source->process(app, source);

      // The sensor queue has no poll source; it reports itself by identifier,
      // and pollOnce keeps returning that identifier until something reads the
      // events. Draining has to happen here rather than beside the frame step
      // below, which this loop would otherwise never reach: four hundred
      // samples a second means there is always another event pending.
      if (ident == ImuSource::kLooperIdent) {
        state.imu.Drain(&imu_samples);
        state.recorder.RecordImu(imu_samples);
      }

      if (app->destroyRequested != 0) {
        StopCapture(&state);
        TerminateDisplay(&state);
        return;
      }
    }

    // Deliberately before the permission/camera early exits below. Status
    // polling is a time-based platform task and must continue even if no camera
    // frame arrives or the activity is backgrounded during a recording.
    PollPeriodicStatus(&state);

    if (!state.permission_granted) {
      state.permission_granted = HasCameraPermission(app);
      if (state.permission_granted) {
        LogOptionalPermissions(app);
        if (state.display != EGL_NO_DISPLAY) StartCapture(&state);
      }
      continue;
    }

    if (!state.capturing) continue;

    if (!state.camera.is_running()) {
      // The camera disconnected (another app took priority, the HAL crashed,
      // ...). Without this, the loop would keep polling a dead camera
      // forever: no new frames, no error, capturing still true, with no way
      // back short of leaving the app.
      if (state.recorder.is_recording()) {
        LocationData end_loc = GetLocationData(app);
        state.recorder.Stop(end_loc);
        StopRecordingService(app);
        state.stop_reason = "STOPPED - CAMERA DISCONNECTED";
      }
      state.camera.Stop();
      state.capturing = false;
      continue;
    }

    state.camera.DrainResults(&capture_results);
    state.recorder.RecordCaptureResults(capture_results);
    if (!capture_results.empty()) state.last_result = capture_results.back();

    if (state.camera.AcquireFrame(&frame)) {
      state.last_timestamp_ns = frame.timestamp_ns;
      if (state.recorder.is_recording()) state.recorder.Record(frame);
    }

    const sensor_logger::InputEvents input = state.input.Poll(app);
    switch (input.action) {
      case Action::kToggleRecording:
        ToggleRecording(&state);
        break;
      case Action::kTogglePreview:
        state.preview_visible = !state.preview_visible;
        break;
      case Action::kNone:
        break;
    }

    if (state.recorder.is_recording()) {
      state.sessions_overlay_visible = false;
      state.pending_delete_index = -1;
      state.pro_panel_visible = false;
    }

    if (input.touched) {
      if (state.pro_panel_visible) {
        if (state.preview.ProPanelCloseContains(input.x, input.y)) {
          state.pro_panel_visible = false;
        } else if (state.preview.ProPanelShutterContains(input.x, input.y)) {
          CycleShutter(&state);
        } else if (state.preview.ProPanelFpsContains(input.x, input.y)) {
          CycleFps(&state);
        } else if (state.preview.ProPanelMainsContains(input.x, input.y)) {
          CycleMains(&state);
        } else if (state.preview.ProPanelShiftContains(input.x, input.y)) {
          CycleShift(&state);
        } else if (state.preview.ProPanelResidualContains(input.x, input.y)) {
          CycleResidual(&state);
        } else if (state.preview.ProPanelExtrinsicContains(input.x, input.y)) {
          StartExtrinsicCapture(&state);
        } else if (state.preview.ProPanelResetContains(input.x, input.y)) {
          ResetProSettings(&state);
        }
      } else if (state.sessions_overlay_visible) {
        if (state.preview.CloseOverlayContains(input.x, input.y)) {
          state.sessions_overlay_visible = false;
          state.pending_delete_index = -1;
        } else if (state.preview.PrevPageOverlayContains(input.x, input.y)) {
          state.sessions_page -= 1;
          state.pending_delete_index = -1;
        } else if (state.preview.NextPageOverlayContains(input.x, input.y)) {
          state.sessions_page += 1;
          state.pending_delete_index = -1;
        } else {
          int touched_idx = state.preview.ItemDeleteOverlayTouched(input.x, input.y);
          if (touched_idx >= 0) {
            if (state.pending_delete_index == touched_idx) {
              if (touched_idx < static_cast<int>(state.cached_sessions.size())) {
                const std::string& path =
                    state.cached_sessions[static_cast<size_t>(touched_idx)].full_path;
                if (SessionRecorder::DeleteSessionPath(path)) {
                  state.cached_sessions.erase(
                      state.cached_sessions.begin() + touched_idx);
                } else {
                  __android_log_print(ANDROID_LOG_WARN, kTag,
                                      "failed to delete session: %s",
                                      path.c_str());
                }
              }
              state.pending_delete_index = -1;
            } else {
              state.pending_delete_index = touched_idx;
            }
          } else {
            state.pending_delete_index = -1;
          }
        }
      } else {
        if (state.preview.LensButtonContains(input.x, input.y)) {
          NextLens(&state);
        } else if (state.preview.SessionsButtonContains(input.x, input.y)) {
          if (!state.recorder.is_recording()) {
            state.sessions_overlay_visible = !state.sessions_overlay_visible;
            state.pending_delete_index = -1;
            state.sessions_page = 0;
            if (state.sessions_overlay_visible) {
              state.cached_sessions =
                  SessionRecorder::GetSessions(SessionRoot(state.app));
            }
          }
        } else if (state.preview.RetentionButtonContains(input.x, input.y)) {
          ToggleRetention(&state);
        } else if (state.preview.LockButtonContains(input.x, input.y)) {
          ToggleExposurePin(&state);
        } else if (state.preview.ProButtonContains(input.x, input.y)) {
          if (!state.recorder.is_recording()) {
            state.pro_panel_visible = true;
          }
        }
      }
    }

    if (state.display == EGL_NO_DISPLAY) {
      // The repeating request still targets the preview reader while an
      // active recording keeps the camera alive without a window. Consume
      // that output anyway: leaving its fixed buffer queue full can stall the
      // capture output on devices that synchronize their camera streams.
      state.camera.DrainPreview();
      continue;
    }

    if (state.preview_visible) {
      if (state.camera.AcquirePreviewFrame(&preview_image)) {
        state.preview.UploadCamera(preview_image);
      }
    } else {
      state.camera.DrainPreview();
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // With the picture up it fills the screen and the numbers shrink to a strip
    // over it. Without, they are the whole screen and can be twice the size.
    // Drawn only where there is something to switch to. A control that cannot
    // do anything is worse than no control: it invites a press and then does
    // nothing visible, which reads as the app being broken.
    const bool lens_choice = state.camera.rear_camera_count() > 1;

    char lens_label[32];
    std::snprintf(lens_label, sizeof(lens_label), "%s %.1FMM",
                  state.camera.LensName(), state.camera.info().focal_length_mm);

    const char* retention_label = state.config.retention == Retention::kAll
                                      ? "RETENTION ALL"
                                      : "RETENTION SHARP";

    const char* lock_label = state.exposure_pinned ? "PINNED" : "LOCK";

    char shutter_label[32];
    std::snprintf(shutter_label, sizeof(shutter_label), "SHUTTER: %s",
                  state.config.max_exposure_ns > 0
                      ? Shutter(state.config.max_exposure_ns).c_str()
                      : "AUTO");

    char fps_label[24];
    if (state.config.fixed_fps > 0 &&
        state.camera.info().applied_fps != state.config.fixed_fps) {
      std::snprintf(fps_label, sizeof(fps_label), "FPS: %d>AUTO",
                    state.config.fixed_fps);
    } else if (state.camera.info().applied_fps > 0) {
      std::snprintf(fps_label, sizeof(fps_label), "FPS: %d",
                    state.camera.info().applied_fps);
    } else {
      std::snprintf(fps_label, sizeof(fps_label), "FPS: AUTO");
    }

    char mains_label[24];
    if (state.config.mains_hz > 0) {
      std::snprintf(mains_label, sizeof(mains_label), "MAINS: %dHZ",
                    state.config.mains_hz);
    } else {
      std::snprintf(mains_label, sizeof(mains_label), "MAINS: OFF");
    }

    char shift_label[24];
    std::snprintf(shift_label, sizeof(shift_label), "SHIFT: %.3F",
                  state.config.min_shift);

    char residual_label[24];
    std::snprintf(residual_label, sizeof(residual_label), "RESIDUAL: %.3F",
                  state.config.min_residual);

    // "INTRINSIC", not "CALIB" — the EXTRINSIC row right below it in the same
    // panel made the older, vaguer label ambiguous between the two.
    const bool has_calibration = state.camera.info().has_calibration;
    const char* calib_label =
        has_calibration ? "INTRINSIC: OK" : "INTRINSIC: NONE";

    constexpr float kGap = 0.015f;
    constexpr float kBtnHeight = 0.040f;

    // Ordered by how often each gets touched around a recording rather than
    // alphabetically or by when it was added: the lock is checked before
    // nearly every take, retention and lens far less often, pro rarer still —
    // tuned once for a scene rather than every take — and sessions is an
    // occasional housekeeping visit, so it sits furthest from a thumb
    // reaching for the others in a hurry.
    if (state.preview_visible) {
      state.preview.DrawCamera(state.camera.sensor_orientation(), 0.62f, 0.03f);
      state.preview.DrawStatus(
          StatusLines(state), state.recorder.is_recording(), 0.04f, 40);

      constexpr float kOverlayGap = 0.010f;
      constexpr float kOverlayBtnH = 0.040f;
      float btn_pos = 0.680f;

      state.preview.DrawLockButton(lock_label, btn_pos,
                                   !state.recorder.is_recording(),
                                   state.exposure_pinned);
      btn_pos += kOverlayBtnH + kOverlayGap;
      state.preview.DrawRetentionButton(retention_label, btn_pos,
                                        !state.recorder.is_recording());
      btn_pos += kOverlayBtnH + kOverlayGap;
      if (lens_choice) {
        state.preview.DrawLensButton(lens_label, btn_pos,
                                     !state.recorder.is_recording());
        btn_pos += kOverlayBtnH + kOverlayGap;
      }
      state.preview.DrawProButton("PRO", btn_pos,
                                  !state.recorder.is_recording());
      btn_pos += kOverlayBtnH + kOverlayGap;
      state.preview.DrawSessionsButton("SESSIONS", btn_pos,
                                       !state.recorder.is_recording());
    } else {

      constexpr int kColumns = 32;
      const std::vector<std::string> lines = StatusLines(state);
      const float block =
          state.preview.StatusHeightFraction(kColumns, (int)lines.size()) +
          (lens_choice ? kGap + kBtnHeight : 0.0f) + 4 * (kGap + kBtnHeight);

      // Positioned independently rather than as one block: the status lines
      // stay where centring the whole block would have put them, and the
      // buttons sit at a fixed position lower down — a thumb reaching for
      // them does not care where the text above happens to end. Deriving
      // both from the same (1 - block) figure does not work here: text_top
      // is smaller than the 0.68 version was, so the text now ends short of
      // where that figure points, and a fixed fraction is needed instead of
      // one relative to a block height that no longer describes this layout.
      constexpr float kMinTextTop = 0.02f;
      float text_top = (1.0f - block) * 0.5f;
      // Same guard as kButtonsTop below, at the other end: if the status
      // block is ever taller than expected, this keeps the text starting on
      // screen instead of above the top edge.
      if (text_top < kMinTextTop) text_top = kMinTextTop;
      constexpr float kButtonsTop = 0.55f;

      float bottom = state.preview.DrawStatus(
          lines, state.recorder.is_recording(), text_top, kColumns);
      // Never lets the buttons ride up into the text if the status block is
      // ever taller than the gap between the two positions allows for.
      if (bottom < kButtonsTop) bottom = kButtonsTop;

      state.preview.DrawLockButton(lock_label, bottom + kGap,
                                   !state.recorder.is_recording(),
                                   state.exposure_pinned);
      bottom += kBtnHeight + kGap;
      state.preview.DrawRetentionButton(retention_label, bottom + kGap,
                                        !state.recorder.is_recording());
      bottom += kBtnHeight + kGap;
      if (lens_choice) {
        state.preview.DrawLensButton(lens_label, bottom + kGap,
                                     !state.recorder.is_recording());
        bottom += kBtnHeight + kGap;
      }
      state.preview.DrawProButton("PRO", bottom + kGap,
                                  !state.recorder.is_recording());
      bottom += kBtnHeight + kGap;
      state.preview.DrawSessionsButton("SESSIONS", bottom + kGap,
                                       !state.recorder.is_recording());
    }

    if (state.sessions_overlay_visible) {
      state.preview.DrawSessionsOverlay(state.cached_sessions, state.pending_delete_index,
                                        &state.sessions_page);
    }
    if (state.pro_panel_visible) {
      state.preview.DrawProPanel(shutter_label, fps_label, mains_label,
                                 shift_label, residual_label, calib_label,
                                 has_calibration);
    }

    eglSwapBuffers(state.display, state.surface);

    static int heartbeat = 0;
    if (++heartbeat % 90 == 0) {
      __android_log_print(ANDROID_LOG_INFO, kTag,
                          "recording=%d written=%lld dropped=%lld",
                          (int)state.recorder.is_recording(),
                          (long long)state.recorder.written_frames(),
                          (long long)state.recorder.dropped_frames());
    }
  }
}
