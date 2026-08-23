# Output format

What a capture session leaves on disk, and what is needed to read it. For the
app itself — controls, settings, building — see the [README](../README.md).

## Files

One directory per session under `<external files>/sessions/`:

| File | Contents |
| --- | --- |
| `frames/<timestamp_ns>.yuv` | Raw `YUV_420_888`, luma then chroma |
| `frames.csv` | `timestamp_ns, filename, width, height, sharpness, chroma_layout, luma_row_stride, chroma_row_stride, chroma_pixel_stride, segment0_length, segment1_length, segment2_length` |
| `imu.csv` | `timestamp_ns, sensor, x, y, z` — `sensor` is `accel` or `gyro` |
| `candidates.csv` | `timestamp_ns, sharpness, shift, residual` — one row per frame scored, kept or not |
| `capture.csv` | `timestamp_ns, exposure_ns, sensitivity, focus_diopters, rolling_shutter_skew_ns, ae_state, awb_state, af_state, fps_range_min, fps_range_max, physical_id` — one row per frame the camera finished |
| `thermal.csv` | `timestamp_ns, thermal_status, battery_temp_c` — sampled periodically, not per frame; `thermal_status` is `-1` below API 29 (see [ADR 9](adr/0009-log-thermal-status-and-battery-temperature.md)) |
| `session.json` | Which phone and camera it came from, counts, and units |

Timestamps are nanoseconds, taken from the image rather than read on arrival, and
they are the key joining every file. `candidates.csv` joins to `frames.csv` on
`timestamp_ns`; rows in both are the frames that were kept.

### Reading the images

Images are the camera's planes concatenated, with no conversion applied.
`frames.csv` carries everything needed to decode them.

Read `chroma_layout` first. `YUV_420_888` allows chroma to be planar — U and V
in separate buffers — or semi-planar, where they interleave into one and the two
"planes" are the same memory a byte apart. Which one a device produces is a
property of that device, so it is recorded per frame.

The file is the segments listed in `frames.csv`, concatenated:

- `planar` — luma, then U, then V
- `semi_planar_uv` / `semi_planar_vu` — luma, then one interleaved chroma
  segment, with `segment2_length` zero. The suffix is which of U or V comes
  first.

**Row strides are not the same as `width`.** On a Galaxy S25 Ultra a 4080-wide
frame has a 4096-byte row stride, so 16 bytes of every row are padding.

Frames are written as the sensor reads them, which is not upright.
`sensor_orientation` in `session.json` is how many degrees clockwise to rotate
them — 90 on a Galaxy S25 Ultra.

`session.json` opens with `device`, `android_release` and `android_sdk`. Every
measurement in a session is a property of the camera and the sensors that took
it, and so are the format assumptions, so a capture that does not say what took
it cannot be checked against another.

It also carries `camera_id`, `focal_length_mm`, `aperture` and
`sensor_size_mm`. Frames from different lenses cannot be solved as one camera —
an ultra-wide and a periscope disagree about focal length by a factor of
eight — so this is what says which one a session is.

It also carries what `capture.conf` the session actually ran with —
`retention`, `min_shift`, `min_residual`, `shutter`, `mains_hz`, `fps` — next
to what was measured — `written_frames`, `considered_frames`,
`dropped_frames`, `camera_completed_captures`, `frames_lost_upstream`,
`average_fps` — so a setting and its effect can be told apart later instead
of assumed. See [settings.md](settings.md#running-out-of-room) for what the
count fields mean and which should be zero.

### Calibration

Where the device publishes it, `session.json` carries the calibration the
manufacturer measured:

```json
"intrinsics": [2826.88, 2830.44, 2024.93, 1519.77, 0],
"distortion": [0.0602282, -0.0909693, 0.0396783, 0, 0],
"pre_correction_active_array": [0, 0, 4080, 3060],
```

`[fx, fy, cx, cy, skew]` and `[k1, k2, k3, p1, p2]`, in pixels of
`pre_correction_active_array` — which is not necessarily the frame size, so
check before using them. On a Galaxy S25 Ultra it is exactly the capture size and
no scaling is needed.

Solving for intrinsics from the images alone wants wide coverage and many
frames. Being handed them is worth having when a capture is sparse.

`lens_pose_translation_m` and `lens_pose_rotation_xyzw` place the lens relative
to `lens_pose_reference`. That reference is the primary camera on the tested
device rather than the gyroscope, so it gives the offset between lenses and not
a camera-to-IMU transform.

**Stabilisation is turned off**, optical and digital both. Digital stabilisation
crops and warps each frame on its own and optical stabilisation moves the lens;
either leaves a camera whose geometry changes between frames, which is the one
thing a reconstruction assumes does not happen. Sharpness selection is the
answer to shake here instead.

### Inertial data

Accelerometer and gyroscope at a requested 200Hz, unfiltered, one row per
sample. The two sensors are not paired into rows, since they deliver on their
own schedules; interpolate as needed.

Whether the camera shares their clock is not a given.
`ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE` says, and it is logged at startup — when
it is not `REALTIME` the two streams cannot be aligned at all.

### Exposure, white balance and focus are held

A reconstruction solves one camera across a whole session. Autofocus moves the
effective focal length as it hunts, and auto exposure and white balance move the
brightness and the colour, so all three break that assumption frame by frame.

They are locked when recording starts rather than at startup: by then the camera
has been metering the room for as long as it took to point the phone at it, and
what it settled on is better than any number chosen in advance. Focus is the
exception — it goes to the hyperfocal distance, where everything from half of
that to infinity is acceptably sharp, which on a short focal length covers a
room without ever hunting. Stopping a session releases them, so the next one
meters the room it is actually in.

`capture.csv` is how to check it held. On the tested device the lock takes 7
frames, a quarter of a second, after which exposure, sensitivity and focus each
hold a single value for the rest of the session. `ae_state` and `awb_state` of
`3` mean locked; `af_state` of `0` means focus is not being driven.

`physical_id` is which lens a logical camera was actually using, empty on a
physical one. It is recorded because a logical camera can change lens on its own
and take the intrinsics with it.

`rolling_shutter_skew_ns` is how long the sensor takes to read from its first
row to its last — 8.6ms on the tested device. A rolling shutter skews a frame by
whatever the camera moved during that, and correcting for it later needs the
number. It is a property of the readout, **not** of the exposure, so a shorter
exposure does not reduce it.

`fps_range_min`/`fps_range_max` are what the platform actually ran the frame
rate at for that frame — the default when `fps` in `capture.conf` is left at
`auto`, since nothing else here asks for a rate. It is not necessarily
constant across a session: the default is a range flexible enough for auto
exposure to trade frame rate for a longer exposure in a dim room, so a scene
that got darker partway through can show a lower rate for the rest of the
session than it started at.

### Capping the exposure

`shutter` shortens the exposure below what the scene metered to. Motion blur is
the exposure multiplied by how fast the camera is turning, and a blurred frame
is worse for a reconstruction than a noisy one: noise averages out across views
and blur does not. The sensitivity rises to keep the brightness, so this trades
one for the other — a 1/120 cap on a 1/30 scene quadrupled the ISO on the tested
device.

Shortening it means driving the sensor by hand, which gives up the platform's
own flicker handling, so `mains` puts it back. Lighting on alternating current
pulses at twice the mains frequency and a rolling shutter catches each row at a
different point in that cycle, so an exposure that is not a whole number of
half-cycles bands the frame. The cap is rounded down to a multiple of one: at
60Hz that is 8.333ms, and 1/120 lands on it exactly.

Left at `auto` the exposure is simply held wherever the scene metered, and the
platform keeps doing this itself — the values it chose on the tested device,
1/30 and 1/24, are both exact multiples of 8.333ms already.

`fps` is a separate setting for a separate problem — see
[settings.md](settings.md#pinning-the-frame-rate) — and pinning it does not
cap the exposure on its own; `shutter` is still what fights motion blur.
