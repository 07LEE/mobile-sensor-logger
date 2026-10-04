# Output format

What a capture session leaves on disk, and what is needed to read it. For installation and controls, see the [README](../README.md).

## Files

One directory per session under `<external files>/sessions/`:

To copy them to a computer:

```bash
adb pull /sdcard/Android/data/com.sensor.logger/files/sessions data/
```

| File | Contents |
| --- | --- |
| frames/<timestamp_ns>.yuv | Raw `YUV_420_888`, luma then chroma |
| frames.csv | `timestamp_ns, filename, width, height, sharpness, chroma_layout, luma_row_stride, chroma_row_stride, chroma_pixel_stride, segment0_length, segment1_length, segment2_length` |
| imu.csv | `timestamp_ns, sensor, x, y, z` — `sensor` is `accel` or `gyro` |
| candidates.csv | `timestamp_ns, sharpness, shift, residual` — one row per frame scored, kept or not |
| motion_grid.bin | Binary, one downsampled luma grid per scored frame — the offline replay input; see below |
| capture.csv | `timestamp_ns, exposure_ns, sensitivity, focus_diopters, rolling_shutter_skew_ns, ae_state, awb_state, af_state, fps_range_min, fps_range_max, physical_id, distortion_correction_mode` — one row per frame the camera finished |
| thermal.csv | `timestamp_ns, thermal_status, battery_temp_c` — sampled on a two-second `CLOCK_BOOTTIME` schedule, not per frame; `timestamp_ns` is the query time and `thermal_status` is `-1` below API 29 |
| lifecycle.csv | `timestamp_ns, event` — `event` is `background` or `foreground`, written on each transition while recording |
| session.json | Which phone and camera it came from, counts, and units — written when recording starts and rewritten when it stops; `status` is `recording` until then, so a session that never reached a clean stop still has the start-time fields but no end time, duration, average frame rate or end location, and its counts are zero. A session without `status` was recorded by an earlier version and was stopped cleanly |

Timestamps are nanoseconds, but their producer depends on the file. `frames.csv`, `candidates.csv` and `motion_grid.bin` use camera image timestamps; `capture.csv` uses camera capture-result timestamps; `imu.csv` uses sensor-event timestamps; `thermal.csv` uses the `CLOCK_BOOTTIME` query time; and `lifecycle.csv` records the most recent camera timestamp at each transition. Camera and IMU timestamps are directly comparable only when the camera reports a `REALTIME` timestamp source, which the manifest explains in `imu_note`.

Every `frames.csv` timestamp has a matching row in `candidates.csv`, but most candidate rows do not have an image because only selected keyframes are written. `candidates.csv` and `motion_grid.bin` are one-to-one and ordered by timestamp.

### Reading the images

Images are the camera's planes concatenated, with no conversion applied.
frames.csv carries everything needed to decode them.

Read `chroma_layout` first. `YUV_420_888` allows chroma to be planar — U and V in separate buffers — or semi-planar, where they interleave into one and the two "planes" are the same memory a byte apart. Which one a device produces is a property of that device, so it is recorded per frame.

The file is the segments listed in frames.csv, concatenated:

- `planar` — luma, then U, then V
- `semi_planar_uv` / `semi_planar_vu` — luma, then one interleaved chroma segment, with `segment2_length` zero. The suffix is which of U or V comes first.

Row strides are not the same as `width`. On a Galaxy S25 Ultra a 4080-wide frame has a 4096-byte row stride, so 16 bytes of every row are padding.

Frames are written as the sensor reads them, which is not upright.
`sensor_orientation` in session.json is how many degrees clockwise to rotate them — 90 on a Galaxy S25 Ultra.

session.json opens with `app_version_name` and `app_version_code`, then `device`, `android_release` and `android_sdk`. Every measurement in a session is a property of the app build, the camera and the sensors that took it, and so are the format assumptions, so a capture that does not say what took it cannot be checked against another.

It also carries `camera_id`, `focal_length_mm`, `aperture` and `sensor_size_mm`. Frames from different lenses cannot be solved as one camera — an ultra-wide and a periscope disagree about focal length by a factor of eight — so this is what says which one a session is.

It also carries what capture.conf the session actually ran with — `retention`, `min_shift`, `min_residual`, `shutter`, `mains_hz`, `fps` — next to what was measured — `written_frames`, `considered_frames`, `dropped_frames`, `index_write_failures`, `camera_completed_captures`, `frames_lost_upstream`, `capture_results_outside_frame_range`, `average_fps` — so a setting and its effect can be told apart later instead of assumed. See [settings.md](settings.md#running-out-of-room) for what the counts mean.

For frame rate, `camera_fps_requested` is the numeric form of the configured `fps` (`0` means auto), while `camera_fps_applied` is the fixed rate actually placed in the capture request (`0` means the request remained auto). `camera_fps_request_supported` is true only when the camera advertises the exact fixed AE range and the chosen YUV output's `camera_min_frame_duration_ns` can sustain it; an unsupported request falls back to auto and is never reported as applied. `camera_fps_set_result` is the Camera2 result from applying a validated request, or `-1` when no fixed request was attempted. `camera_observed_fps`, the mean and approximate median period, maximum gap and non-monotonic timestamp count summarize capture-result timestamps delivered during the session. Per-frame `fps_range_min` and `fps_range_max` in capture.csv are the active AE target range, not the measured interval between that frame and the next one.

The capture request turns Camera2 distortion correction off when the device offers it, because `intrinsics` and `distortion` are pre-correction values and YUV output may otherwise be corrected. `camera_distortion_correction_off_available` says whether the device offers OFF, and `camera_distortion_correction_set_result` is the Camera2 result of requesting it, or `-1` when no request was made. When the device does not offer OFF, no request is sent, and whether its YUV frames are corrected is not known: the Galaxy Z Flip4 lists no standard modes and exposes a vendor tag for it instead. The per-frame `distortion_correction_mode` in capture.csv is what the platform reports it applied (`0` off, `1` fast, `2` high quality, `-1` not reported). The export scripts warn when a session does not confirm either case, which includes every session recorded before these fields existed.

`thermal_sample_target_interval_ns` and `free_space_check_target_interval_ns` record the intended two-second schedules. They use `CLOCK_BOOTTIME`, so camera FPS, preview visibility and time spent suspended do not redefine the interval. A busy or suspended process can observe a late sample; missed intervals are skipped rather than emitted as a burst.

`imu_requested_interval_us` records the requested IMU interval. The nested `accelerometer` and `gyroscope` objects record whether each sensor was available, its name, vendor and minimum delay, the results returned when enabling it and setting its rate, and the delivered sample count, mean period, approximate median period, maximum gap and non-monotonic timestamp count. A successful rate request is not proof that the platform delivered that rate; the timing summary is the observed result.

To get upright PNG or JPEG files instead, `./scripts/export_images.py <session_dir> [--out DIR] [--format png|jpg] [--limit N]` decodes every frame in frames.csv, applies `sensor_orientation`, and writes `<session_dir>/images/<timestamp_ns>.png` by default. It needs Python with `numpy` and `opencv-python`.

For COLMAP, `./scripts/export_colmap.py <session_dir>` prints the `FULL_OPENCV` camera parameters and a `feature_extractor` command for those images. It rotates the session.json intrinsics the same way the images are rotated, and exits with a message when the session has no intrinsics or the frame is not a uniform scale of the active array.

For Kalibr, `./scripts/export_rosbag.py <session_dir> [--out FILE.bag]` writes a ROS1 bag with upright mono8 images on `/cam0/image_raw` and accelerometer-interpolated gyroscope pairs on `/imu0`. It needs the `rosbags` Python package, and the bag is about the size of the luma planes, 12MB per frame at 4080x3060. The script refuses a session, before writing a bag, when `imu_note` says the camera timestamps are not on the IMU clock or when `imu.csv` lacks accelerometer or gyroscope samples.

`./scripts/export_camchain.py <session_dir> [--out FILE.yaml]` writes the matching Kalibr `camchain.yaml`, seeded with the session.json intrinsics rotated to the bag's upright images. Kalibr's pinhole-radtan model has no third radial term, so `k3` is dropped with a warning. The principal point is moved half a pixel on each axis, because Camera2 puts the center of pixel (x, y) at (x + 0.5, y + 0.5) while Kalibr counts integer coordinates as pixel centers.

### Replaying the selection rule

candidates.csv records what the on-device run actually measured against the reference chain its own thresholds produced — sharpness (never threshold-dependent) alongside shift/residual (specific to that run). To ask what a different `min_shift`/`min_residual` would have kept, without reshooting, motion_grid.bin carries what those thresholds would need: the same downsampled luma grid `FrameMotion` computes internally for every scored frame, small enough (`grid_width * grid_height` bytes — 3072 at this project's usual 4:3 capture) to record unconditionally alongside candidates.csv.

Format (defined by `app/src/main/cpp/pipeline/motion_grid_format.h`): a 16-byte header (`"SLMG"` magic, `format_version`, `grid_width`, `grid_height`), then one fixed-size record per scored frame (`timestamp_ns`, then `grid_width * grid_height` grid bytes, row-major, top-left origin). Little-endian only. Fixed record sizes mean a reader can tell a truncated file from a valid one by size alone.

`./scripts/replay_sampling.sh <session_dir> [--min-shift=F] [--min-residual=F]` drives the same production `FrameMotion`/`KeyframeSelector` code the app does, and prints the keyframe timestamps that would result. Run with the session's own `min_shift`/`min_residual` (from session.json), its output is exactly frames.csv's `timestamp_ns` column.

### Calibration

Where the device publishes it, session.json carries the calibration the manufacturer measured:

```json
"intrinsics": [2826.88, 2830.44, 2024.93, 1519.77, 0],
"distortion": [0.0602282, -0.0909693, 0.0396783, 0, 0],
"pre_correction_active_array": [0, 0, 4080, 3060],
```

`[fx, fy, cx, cy, skew]` and `[k1, k2, k3, p1, p2]`, in pixels of `pre_correction_active_array` — which is not necessarily the frame size, so check before using them. On a Galaxy S25 Ultra it is exactly the capture size and no scaling is needed.

Solving for intrinsics from the images alone wants wide coverage and many frames. Being handed them is worth having when a capture is sparse.

`lens_pose_translation_m` and `lens_pose_rotation_xyzw` place the lens relative to `lens_pose_reference`. That reference is the primary camera on the tested device rather than the gyroscope, so it gives the offset between lenses and not a camera-to-IMU transform.

Stabilisation is turned off, optical and digital both. Digital stabilisation crops and warps each frame on its own and optical stabilisation moves the lens; either leaves a camera whose geometry changes between frames, which is the one thing a reconstruction assumes does not happen. Sharpness selection is the answer to shake here instead.

### Inertial data

Accelerometer and gyroscope at a requested 200Hz, unfiltered, one row per sample. The two sensors are not paired into rows, since they deliver on their own schedules; interpolate as needed.

Whether the camera shares their clock is not a given.
`ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE` says, and it is logged at startup — when it is not `REALTIME` the two streams cannot be aligned at all.

### Exposure, white balance and focus are held

A reconstruction solves one camera across a whole session. Autofocus moves the effective focal length as it hunts, and auto exposure and white balance move the brightness and the colour, so all three break that assumption frame by frame.

They are locked when recording starts rather than at startup: by then the camera has been metering the room for as long as it took to point the phone at it, and what it settled on is better than any number chosen in advance. Focus is the exception — it goes to the hyperfocal distance, where everything from half of that to infinity is acceptably sharp, which on a short focal length covers a room without ever hunting. Stopping a session releases them, so the next one meters the room it is actually in.

capture.csv is how to check it held. On the tested device the lock takes 7 frames, a quarter of a second, after which exposure, sensitivity and focus each hold a single value for the rest of the session. `ae_state` and `awb_state` of `3` mean locked; `af_state` of `0` means focus is not being driven.

`physical_id` is which lens a logical camera was actually using, empty on a physical one. It is recorded because a logical camera can change lens on its own and take the intrinsics with it.

`rolling_shutter_skew_ns` is how long the sensor takes to read from its first row to its last — 8.6ms on the tested device. A rolling shutter skews a frame by whatever the camera moved during that, and correcting for it later needs the number. It is a property of the readout, not of the exposure, so a shorter exposure does not reduce it.

`fps_range_min`/`fps_range_max` are the AE target range reported for that capture result. They can change when auto exposure selects a different operating range, but they are not a direct measurement of the delivered FPS. Use consecutive capture timestamps or the manifest's observed timing summary for the actual cadence.

### Capping the exposure

`shutter` shortens the exposure below what the scene metered to. Motion blur is the exposure multiplied by how fast the camera is turning, and a blurred frame is worse for a reconstruction than a noisy one: noise averages out across views and blur does not. The sensitivity rises to keep the brightness, so this trades one for the other — a 1/120 cap on a 1/30 scene quadrupled the ISO on the tested device.

Shortening it means driving the sensor by hand, which gives up the platform's own flicker handling, so `mains` puts it back. Lighting on alternating current pulses at twice the mains frequency and a rolling shutter catches each row at a different point in that cycle, so an exposure that is not a whole number of half-cycles bands the frame. The cap is rounded down to a multiple of one: at 60Hz that is 8.333ms, and 1/120 lands on it exactly.

Left at `auto` the exposure is simply held wherever the scene metered, and the platform keeps doing this itself — the values it chose on the tested device, 1/30 and 1/24, are both exact multiples of 8.333ms already.

`fps` is a separate setting for a separate problem — see [settings.md](settings.md#pinning-the-frame-rate) — and pinning it does not cap the exposure on its own; `shutter` is still what fights motion blur.
