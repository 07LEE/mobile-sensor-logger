# Mobile Sensor Logger

Android capture app for 3D reconstruction data. Records camera images and
inertial data and gets them off the phone. Poses are not computed here — that
happens on a workstation, which has the compute for a global optimisation and
can be re-run against the same capture as many times as the result needs.

Written against the **Camera2 NDK** — the app is C++ with no application Java or
Kotlin, hosted by `GameActivity`. See
[ADR 0003](docs/adr/0003-camera2-capture-without-arcore.md) for why ARCore is
not in it.

## Output

One directory per session under `<external files>/sessions/`:

| File | Contents |
| --- | --- |
| `frames/<timestamp_ns>.yuv` | Raw `YUV_420_888`, luma then chroma |
| `frames.csv` | `timestamp_ns, filename, width, height, sharpness, chroma_layout, luma_row_stride, chroma_row_stride, chroma_pixel_stride, segment0_length, segment1_length, segment2_length` |
| `imu.csv` | `timestamp_ns, sensor, x, y, z` — `sensor` is `accel` or `gyro` |
| `candidates.csv` | `timestamp_ns, sharpness, shift, residual` |
| `session.json` | Counts, and what the numbers in them mean |

Timestamps are nanoseconds, taken from the image rather than read on arrival, so
they refer to capture time, and they are the key joining every file.

Whether they share a clock with the inertial samples is not a given:
`ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE` says, and when it is not `REALTIME`
nothing in software can line the two streams up. It is logged at startup so a
device that reports otherwise is recognised rather than left to produce quietly
unusable captures. That condition is what any offline visual-inertial
reconstruction depends on, and the one thing no amount of software could work
around.

### Images

Written exactly as the camera hands them over, without conversion. The NDK has
no JPEG encoder, and converting on the phone would spend the capture's frame
budget on work the workstation can do later.

`frames.csv` carries what is needed to decode them, and `chroma_layout` is the
part to read first. Android's `YUV_420_888` allows chroma to be planar — U and V
in separate buffers — or semi-planar, where they interleave into one and the two
"planes" are the same memory a byte apart. Treating the second case as two
planes would both duplicate a megabyte per frame and swap the colours. Which one
a device produces is a property of that device, so the file records it per frame
rather than assuming.

The file is the segments listed in `frames.csv`, concatenated: luma, then either
one interleaved chroma segment or a U and a V segment. Row strides are not the
same as `width`.

The capture stream is the largest `YUV_420_888` size the device offers, since
that resolution caps the detail any later processing can recover. Every size a
device reports is logged at startup — that log is the first thing to check if
captures come out smaller than expected.

### Inertial data

Accelerometer and gyroscope are read at a requested 200Hz and written
unfiltered, as separate rows rather than fused pairs: the two sensors deliver on
their own schedules, and pairing them here would mean inventing a timestamp for
one of them. Unlike frames they are not selected — the gaps are what would make
the stream unusable for integration, and a few kilobytes a second is nothing
against megabytes a frame.

They are what carries metric scale, which monocular structure-from-motion cannot
recover on its own, and what an offline estimator would need to constrain the
trajectory between frames.

## Which frames are kept

**Sharpness decides which frames survive.** Handheld capture produces defocused
and motion-smeared frames continuously, and which ones are bad cannot be
predicted from anything but the picture. So every frame is scored — variance of
the Laplacian over the luma plane, subsampled — and the sharpest frame of each
stretch of movement is the one written.

Keeping every frame is the other way to be sure of getting a sharp one, but a
frame is megabytes at capture resolution and the camera produces thirty a
second, so the disk runs out long before a useful capture is finished.

**A stretch ends when the picture has changed enough**, measured in the image
rather than derived from a pose. Overlap is what a reconstruction needs, and a
pose only implies it once the distance to the scene is known: five centimetres
beside a desk is a different view, and five centimetres beside a far wall is the
same photograph.

Two measurements, because either alone misses something common. A search for the
offset that best lines the frame up against the last kept one catches panning
and sideways movement. Walking forward changes scale rather than position, and
turning about the lens axis changes neither, so both leave the offset small;
what they leave instead is a picture that no offset lines up, which is what the
leftover difference measures. Either crossing its threshold ends the stretch.

Both are in `candidates.csv` as `shift` — a fraction of the frame width — and
`residual`, and both are on screen during a capture. The thresholds are in
`frame_motion.h` and the sharpness subsampling step is in `session_recorder.cc`.
They are guesses: wider baselines reconstruct from fewer frames but eventually
stop matching, and where that line falls has not been tested against a real
capture.

**Every frame that was scored is logged, even when its image is not kept.**
Selection discards most of what the camera produced, on thresholds that were
guessed rather than measured. `candidates.csv` is what keeps that reviewable: a
row is about fifty bytes, so the sharpness and the motion survive even though
the image does not, and what a different threshold would have selected can be
worked out from a capture already taken rather than from another trip to the
same place. Rows join to `frames.csv` on `timestamp_ns`; the ones in both are
the frames that were kept.

The sharpness score has no absolute meaning — it moves with scene content and
exposure — so it is only comparable between frames of the same scene taken
moments apart.

## Writing

**Images are written on a thread of their own.** A frame is megabytes, and
writing one from the capture loop stalls that loop for as long as the write
takes; the camera does not hold frames while nobody is taking them, so whatever
arrived during the stall is gone.

The queue between the two is bounded, because an unbounded one facing a disk
that cannot keep up grows until the process is killed. When it is full the frame
is dropped and counted rather than blocking the loop: dropping one frame costs
one viewpoint, blocking costs whatever else the camera produced meanwhile, and a
run of drops is the honest signal that the capture is asking for more than the
device can write. `dropped_frames` in `session.json` is where that shows up, and
it should be zero.

## On screen

The camera image with a readout over it: whether recording is running, frames
kept against frames seen, capture resolution, how far the picture has shifted
since the last kept frame, inertial samples, and dropped frames.

Those numbers decide whether a capture is worth keeping, and without them on
screen they exist only in logcat, which cannot be read while walking around with
the phone — the only time they could change what someone does.

The camera hands over YUV planes, so the conversion happens in a shader: luma
and chroma go up as two textures, the chroma packed on the way because the
planes arrive in whichever layout the device prefers. The preview is a second,
smaller capture stream; uploading a full-resolution frame every frame would cost
more than the capture does. It is rotated upright by the sensor orientation and
letterboxed rather than stretched, since a preview that lies about the shape of
the frame defeats the point of having one.

On an OLED panel this is not free in power terms, since a black screen leaves
its pixels off. The GPU cost is negligible next to the camera and the writes.

## Building

```bash
./gradlew assembleDebug
```

Requires the Android SDK, NDK, and CMake. The versions are pinned in
`app/build.gradle.kts` and the Gradle wrapper rather than repeated here, where
they would drift. Built with JDK 21.

No network access is needed beyond the usual dependency resolution, and no
emulator path exists: the capture path is a real camera.

## Tested on

A single device, and the numbers quoted in this file come from it:

| | |
| --- | --- |
| Galaxy S25 Ultra | SM-S938N, Android 16 (API 36) |
| Chroma layout | `semi_planar_vu` — V first, interleaved |
| Timestamp source | `REALTIME` on all four cameras |
| Inertial rate | ~637 samples/s combined, at a requested 200Hz each |

Everything above is a property of that device, not of Android. A second device
would be the first real test of whether the format assumptions hold, and there
has not been one.

## Status

Builds and installs. The Camera2 capture path has not yet been run on a device —
everything before it was written against ARCore, and this replaced it whole.

Recording starts by itself with the first frame, because `GameActivity` is not
delivering touch events to the native input buffer and there is no UI to start
it from.

## Not implemented

- **Touch input.** `GameActivity` is not delivering motion events to the native
  buffer, so the tap-to-toggle path never fires. Recording auto-starts instead.
- **Manual focus and exposure.** The capture request is the preview template,
  so both still follow the scene. Holding them is one of the reasons for owning
  the camera in the first place.
- **Storage management.** Sessions are written until the device runs out of
  space; nothing warns, caps, or cleans up.
