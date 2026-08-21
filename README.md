# Mobile Sensor Logger

Android app that records camera images and inertial data for 3D reconstruction
on a workstation.

- Captures at the device's largest `YUV_420_888` size — 4080x3060 on a Galaxy
  S25 Ultra
- Writes the sharpest frame of each stretch of movement, not every frame
- Records accelerometer and gyroscope on the same clock as the images
- Computes no pose on the device

C++ throughout, against the Camera2 NDK, with no application Java or Kotlin.
`GameActivity` hosts it.

Why it is built this way: [docs/adr/](docs/adr/).

## Output

One directory per session under `<external files>/sessions/`:

| File | Contents |
| --- | --- |
| `frames/<timestamp_ns>.yuv` | Raw `YUV_420_888`, luma then chroma |
| `frames.csv` | `timestamp_ns, filename, width, height, sharpness, chroma_layout, luma_row_stride, chroma_row_stride, chroma_pixel_stride, segment0_length, segment1_length, segment2_length` |
| `imu.csv` | `timestamp_ns, sensor, x, y, z` — `sensor` is `accel` or `gyro` |
| `candidates.csv` | `timestamp_ns, sharpness, shift, residual` — one row per frame scored, kept or not |
| `capture.csv` | `timestamp_ns, exposure_ns, sensitivity, focus_diopters, ae_state, awb_state, af_state, physical_id` — one row per frame the camera finished |
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

## Which frames are kept

Every frame is scored for sharpness — variance of the Laplacian over the luma
plane, subsampled — and the sharpest frame of each stretch of movement is
written. The score is in `candidates.csv` and `frames.csv`. It has no absolute
meaning: it moves with scene content and exposure, so it only compares frames of
the same scene taken moments apart.

A stretch ends when the picture has changed enough, measured against the last
frame kept:

| Column | Meaning | Default threshold |
| --- | --- | --- |
| `shift` | Best alignment offset, as a fraction of frame width | `0.12` |
| `residual` | Mean difference remaining at that offset, `0`–`1` | `0.06` |

Either one crossing its threshold ends the stretch. `shift` catches panning and
sideways movement; `residual` catches walking forward and rotating about the lens
axis, which barely move the offset.

Both are settings (see below); the sharpness subsampling step is in
`session_recorder.cc`. Neither has been tuned against a reconstruction.

Halving the thresholds roughly doubles the frames kept, and they only bite while
the camera moves slowly enough for them to — sweep fast enough and every frame
crosses them, at which point the capture is keeping everything and the frame rate
is the only limit left.

The defaults leave about 88% of the picture shared between consecutive kept
frames, which is generous next to the 70 to 80% reconstruction usually asks for.
They sit there because at full resolution the free space is the budget: 105GB
holds roughly 5,500 frames, which these values spend in forty minutes and half
these values spend in six. The `ROOM FOR` line on screen is what says whether a
capture is spending it faster than intended.

`candidates.csv` holds a row for every frame scored, including those discarded —
about fifty bytes each. What a different threshold would have selected can be
worked out from a capture already taken.

## Settings

Read once at startup from `capture.conf` in the app's external files directory,
which is where `adb push` reaches. Missing file or missing key means the
default.

```
capture   = max | 1920x1080     # largest the camera offers, or an exact size
retention = sharpest | all      # selected frames, or every frame
lens      = ultrawide | main | <camera id>
shift     = 0.12                # how far the picture may slide before a frame
residual  = 0.06                # how much of it may stop matching
```

```bash
adb push capture.conf /sdcard/Android/data/com.sensor.logger/files/
```

Every rear camera is logged at startup with its focal length, so `lens` can name
one by id. On a Galaxy S25 Ultra two are offered: `0` at 6.3mm and `2` at 2.2mm.

The default is `ultrawide`, the shortest focal length the device offers. It
wins at both ends of the range this is used at: a frame covers far more of a
room, which matters when free space is the budget, and it focuses closer than
the main lens — 5cm against 10cm on the tested device, enough to beat it on
close detail despite the wider field, since detail goes as the reciprocal of
distance. The main lens is ahead only in between, by about 1.4x per degree.

`main` is whichever the system lists first. That is usually a **logical**
camera, which chooses a physical lens by zoom ratio and can change it during a
capture — and the focal length changes with it, which nothing downstream will
expect. `session.json` records `logical_multi_camera` so a session that came
back inconsistent has somewhere to start. `ultrawide` picks the shortest focal
length, which on the tested device is a physical camera and cannot change.

A requested size is used only if the chosen camera offers it exactly; otherwise
it takes the largest and says so in the log.

## Running out of room

A frame at full resolution is around 19MB, and a session writes two or three a
second, so plan on **2 to 3GB per minute**. The readout carries the measured
rate and what is left at it.

`dropped_frames` in `session.json` counts frames the writer could not keep up
with. **It should be zero.** A run of drops means the capture is asking for more
than the device can write.

Recording stops on its own with 2GB left, and the readout says
`STOPPED - DISK FULL`. That margin exists because a session that runs the disk
to the last byte cannot write its own manifest — the settings, the counts and
the calibration go with it — so a capture that filled a phone ends up unusable
anyway. Sessions are never deleted; clearing them is a manual job.

## On screen

The camera is not drawn unless asked for. The numbers are what the screen is for
during a capture; the picture is for aiming, which happens between captures more
than during one.

```
     +--------------------------+
     |                          |
     |                          |
     |  REC 0:57   36 MIN LEFT  |
     |  153 KEPT / 1696 SEEN    |
     |  2.9GB USED 111.0GB FREE |
     |  SHIFT 2/12  DIFF 4/6    |
     |  4080X3060 ULTRAWIDE ALL |
     |  DROP 0 NOIMG 0 IMU 36K  |
     |                          |
     |  [ ULTRAWIDE 2.2MM-TAP ] |
     |                          |
     |                          |
     +--------------------------+
```

**Volume up** puts the camera on screen, filling it, with the numbers shrunk to
a strip along the top. Volume up again takes it away.

The button changes lens, and is only drawn where there is more than one rear
camera to change between. It is the only thing a touch does, and it is refused
while recording, so the rule that nothing behind a touch can lose a capture
still holds. Touches count on release rather than on press, so a finger that
lands and slides off is not a tap.

Not drawing the camera also skips uploading a frame as two textures every frame,
which was what capped the frame rate, and leaves nearly the whole OLED panel
switched off — the screen is the second largest draw on the battery after the
camera itself.

`SHIFT` and `DIFF` are the two selection thresholds, as percentages. A red marker
in the corner means recording.

`MIN LEFT` is the free space divided by the rate this capture is actually
filling it. That rate depends on the resolution, on how much of the scene is
moving, and on how many frames survive selection, so a figure worked out
beforehand would be wrong for the capture in hand.

## Building

```bash
./gradlew assembleDebug
```

Needs the Android SDK, NDK, and CMake; versions are pinned in
`app/build.gradle.kts` and the Gradle wrapper. Built with JDK 21. There is no
emulator path — the capture path is a real camera.

**Volume down** starts and stops recording. **Volume up** switches to the next
rear lens, and is refused while recording: the intrinsics change with the lens
so a session cannot continue across one, and ending a capture because a key was
brushed against a coat is a trip wasted. Stop, change, start again. Press
**Home** to leave; that also closes the session cleanly.

Touches are read but bound to nothing on purpose. A palm across the screen while
the phone is pointed at something is not a decision, and a capture that stops
because of one is a trip wasted.

Pulling and clearing a device:

```bash
adb pull /sdcard/Android/data/com.sensor.logger/files/sessions data/
adb shell run-as com.sensor.logger \
  rm -rf /sdcard/Android/data/com.sensor.logger/files/sessions
```

The second command needs `run-as`; a plain `rm` is refused under scoped storage.

## Tested on

One device, and the numbers in this file come from it:

| | |
| --- | --- |
| Galaxy S25 Ultra | SM-S938N, Android 16 (API 36) |
| Rear cameras offered | `0` at 6.3mm f/1.7 (logical), `2` at 2.2mm f/1.9 |
| Capture size | 4080x3060 on both, 18.8MB per frame |
| Chroma layout | `semi_planar_vu`, luma row stride 4096 |
| Timestamp source | `REALTIME` |
| Inertial rate | ~637 samples/s combined |

Those are properties of that device, not of Android.

## Not implemented

- **Manual focus and exposure.** The capture request is the preview template, so
  both still follow the scene.
- **Cleaning up.** Old sessions are never removed; that is done over adb.
