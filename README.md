# Mobile Sensor Logger

Android capture app for 3D reconstruction data. Writes camera images together
with the ARCore pose and intrinsics each was taken with, so the session can be
processed on a workstation afterwards. Frames are chosen by sharpness rather
than kept wholesale — see below.

Written against the ARCore **C API** — the app is C++ with no application Java
or Kotlin, hosted by `GameActivity`.

## Output

One directory per session under `<external files>/sessions/`:

| File | Contents |
| --- | --- |
| `frames/<timestamp_ns>.yuv` | Raw `YUV_420_888`, luma then chroma |
| `frames.csv` | `timestamp_ns, filename, width, height, sharpness, chroma_layout, luma_row_stride, chroma_row_stride, chroma_pixel_stride, segment0_length, segment1_length, segment2_length` |
| `poses.csv` | `timestamp_ns, tx, ty, tz, qx, qy, qz, qw, fx, fy, cx, cy, image_width, image_height` |
| `points.csv` | `timestamp_ns, x, y, z, confidence` |
| `imu.csv` | `timestamp_ns, sensor, x, y, z` — `sensor` is `accel` or `gyro` |
| `candidates.csv` | `timestamp_ns, tx, ty, tz, qx, qy, qz, qw, sharpness, point_count, median_point_distance_m, translation_threshold_m` |
| `session.json` | Frame counts and the pose convention |

Poses are in ARCore's right-handed world frame, rotation as a quaternion in
`(x, y, z, w)` order. Timestamps are nanoseconds on the system clock, taken from
ARCore rather than read on arrival, so they refer to capture time, and they are
the key joining every file.

Images are written exactly as ARCore hands them over, without conversion. The
NDK has no JPEG encoder, and converting on the phone would spend the capture's
frame budget on work the workstation can do later.

`frames.csv` carries what is needed to decode them, and `chroma_layout` is the
part to read first. Android's `YUV_420_888` allows chroma to be planar — U and V
in separate buffers — or semi-planar, where they interleave into one and the two
"planes" are the same memory a byte apart. A Galaxy S25 Ultra reports
`semi_planar_vu`, meaning V comes first. Treating that as two planes would both
duplicate a megabyte per frame and swap the colours.

The file is the segments listed in `frames.csv`, concatenated: luma, then either
one interleaved chroma segment or a U and a V segment. Row strides are not the
same as `width`.

**Inertial data is recorded alongside the images.** Accelerometer and
gyroscope are read at a requested 200Hz and written unfiltered, as separate rows
rather than fused pairs — the two sensors deliver on their own schedules, and
pairing them here would mean inventing a timestamp for one of them. Unlike
frames they are not selected: the gaps are what would make the stream unusable
for integration, and a few kilobytes a second is nothing against megabytes a
frame.

ARCore's poses are not the only thing a capture might be used for. Raw inertial
data is what a different VIO implementation would need, what gives bundle
adjustment an inertial constraint, and what carries the metric scale that
monocular structure-from-motion cannot recover on its own.

The two streams are comparable because they share a clock. Android lets a camera
report its timestamps against either the boot-time clock the sensors use or one
of its own, and `ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE` is what says which; when
it is `UNKNOWN` there is nothing software can do to align them. Every camera on
the tested device reports `REALTIME`, and the value is logged at startup so a
device that does not can be recognised rather than producing quietly unusable
captures.

The app selects the camera configuration with the largest CPU-accessible image,
since that resolution caps the detail any later processing can recover. ARCore
defaults to VGA and only some devices offer a 1920x1080 CPU stream, so every
config a device reports is logged at startup — that log is the first thing to
check if captures come out smaller than expected. Full sensor resolution is not
reachable this way; it would need a separate Camera2 stream through
`SharedCamera`.

**Sharpness decides which frames survive.** Handheld capture produces defocused
and motion-smeared frames continuously, and which ones are bad cannot be
predicted from the pose. So every tracked frame is scored — variance of the
Laplacian over the luma plane, subsampled — and the sharpest frame of each
stretch of movement is the one written.

Keeping every frame is the other way to be sure of getting a sharp one, but a
frame is megabytes at capture resolution and the camera produces thirty a
second, so the disk runs out long before a useful capture is finished. Scoring
on the phone keeps the choice while writing one frame per viewpoint.

**A stretch ends on angle, not on distance.** Sideways movement is measured as
the angle it turns the scene through: five centimetres beside a desk is a
genuinely different view of the subject, and five centimetres beside a far wall
is the same photograph. A threshold in metres cannot tell those apart, so it
over-samples across a room and under-samples up close — backwards on both
counts, since close-up work is where detail has to survive and walking across a
room is where the disk fills up. Walking a large space at a fixed 5cm would have
written about twenty frames a second.

How far away the scene is comes from the median distance to the feature points
ARCore is tracking, clamped to a sane range so a few points on a blank wall
cannot move the threshold by an order of magnitude. The current value is on
screen during a capture, since it is how far to move for the next viewpoint and
it is no longer a number to memorise.

Turning in place is a separate criterion at about 6 degrees, because it changes
what is in view while producing no parallax at all.

The defaults are in `keyframe_selector.h` and the subsampling step is in
`session_recorder.cc`. They remain guesses: wider baselines reconstruct from
fewer frames but eventually stop matching, and where that line falls has not
been tested against a real capture. `candidates.csv` is what makes testing it
possible without re-shooting.

The score is in `frames.csv`. It has no absolute meaning — it moves with scene
content and exposure — so it is only comparable between frames of the same scene
taken moments apart.

**Every frame that was scored is logged, even when its image is not kept.**
Selection discards most of what the camera produced — a few dozen images out of
a couple of thousand scored — and the thresholds it discards on were guessed
rather than measured. `candidates.csv` is what keeps that reviewable: a row is
about a hundred bytes, so the pose, the sharpness score, and how far away the
scene was all survive even though the image does not. What a different threshold
would have selected can be worked out from a capture already taken, rather than
from another trip to the same place. Rows join to `frames.csv` on
`timestamp_ns`; the ones that appear in both are the frames that were kept.

`median_point_distance_m` is there because distance is what a threshold in
metres is missing. Five centimetres beside a desk is a genuinely different angle
on the subject; five centimetres beside a far wall is the same photograph. It is
the median distance from the camera to the feature points ARCore was tracking
that frame — the median rather than the mean, because the cloud carries stray
points far behind the subject. It is `0` when the cloud was empty, which
`point_count` distinguishes from a real zero.

**Images are written on a thread of their own.** A frame is three megabytes,
and writing one from the capture loop stalls that loop for as long as the write
takes; ARCore does not hold frames while nobody is asking for them, so whatever
arrived during the stall is gone. At the current rate, a frame every few
seconds, that is invisible — but it is what would make keeping every frame
impossible, since the loop would then spend most of its time in the filesystem.

The queue between the two is bounded, because an unbounded one facing a disk
that cannot keep up grows until the process is killed. When it is full the frame
is dropped and counted rather than blocking the loop: dropping one frame costs
one viewpoint, blocking costs whatever else the camera produced meanwhile, and a
run of drops is the honest signal that the capture is asking for more than the
device can write. `dropped_frames` in `session.json` is where that shows up, and
it should be zero.

A frame is only logged in `frames.csv` if it was tracking **and** its image was written. Poses
without images cannot be processed, so a partial frame is counted rather than
half-written. `session.json` carries the counts — `written_frames`,
`considered_frames`, `untracked_frames`, `frames_without_image` — which are the
quickest signal that a capture went badly. A session that ran for a while with
`written_frames` near zero was usually not tracking.

## Building

Requires the Android SDK, NDK, and CMake. ARCore needs API 24+, an
ARCore-supported device, and arm64/armv7 — there is no emulator path.

```bash
./gradlew assembleDebug
```

The first build needs network access: ARCore's C API header ships only in the
SDK repository, not on Maven, so Gradle downloads it rather than keeping a copy
of Google's file in this repository. The native library is unpacked from the
ARCore AAR at the same time.

## ARCore terms

This app uses [ARCore](https://developers.google.com/ar), which carries
[additional terms of service](https://developers.google.com/ar/develop/terms)
beyond the licence of this repository. They permit commercial use and require no
source disclosure, but they do place obligations on anything shipped: users must
be told the app includes ARCore, and be given Google's terms and privacy policy.
Read them before distributing a build.

**The point cloud has not been reliable, and the parallax threshold rests on
it.** Across every session captured so far, 92% of feature points came back as
NaN, and six sessions out of sixteen produced no points at all — including the
most recent, which wrote thirty-seven frames and one point. Where nothing
measured the distance the threshold falls back to an assumption, which is safe
but degenerates to a fixed step in metres again, so the readout says `ASSUMED`
rather than printing a number that looks measured. Fixing this is what the next
capture on a device has to establish; ARCore's depth API is the obvious
alternative source if the sparse cloud stays this thin.

Feature points that are not finite are dropped before anything sees them.
Captures have come back with most of the point cloud as NaN — scattered through
it rather than at one end, and permanent once it starts. Whether ARCore produces
those or this reads them wrong is still open, so the ratio is logged rather than
passed over. A NaN reaching the keyframe rule would be worse than a lost point:
every comparison against NaN is false, so the movement criterion would stop
firing without any sign of it.

## Status

Runs on a Galaxy S25 Ultra (Android 16): the session starts, poses and 1920x1080
frames are written, and the frames decode to correct colour off-device.

IMU recording and the fixed capture loop have been built but not yet run on a
device: attaching the sensor queue to the main looper originally stopped frames
being captured at all, because the loop polling that looper never read the
sensor events and so never reached the frame step. Sensor capture now starts
with the AR session and is drained inside that poll loop. The counts in
`session.json` — `imu_samples` against `written_frames` — are what will confirm
it.

The screen shows the camera image with a readout over it: whether recording is
running, frames kept against frames seen, tracking state and why it is lost when
it is, feature point count, inertial samples, and dropped frames. Those numbers
decide whether a capture is worth keeping, and they used to exist only in
logcat, which cannot be read while walking around with the phone — which is the
only time they could change what someone does.

Drawing the preview costs almost no GPU time: the camera runs at full rate for
ARCore regardless, ARCore renders into the texture whether or not anything
samples it, and the frame was already being presented. On an OLED panel it is
not free in power terms, though, since a black screen leaves its pixels off.

Recording currently starts by itself once ARCore is tracking, because
`GameActivity` is not delivering touch events to the native input buffer and
there is no UI to start it from.

If nothing is captured, check logcat: ARCore reports why it is not tracking, and
"not enough light" is the usual answer indoors at night.

## Not implemented

- **Touch input.** `GameActivity` is not delivering motion events to the native
  buffer, so the tap-to-toggle path never fires. Recording auto-starts instead.
- **Storage management.** Sessions are written until the device runs out of
  space; nothing warns, caps, or cleans up. A 1920x1080 frame is 3MB.
