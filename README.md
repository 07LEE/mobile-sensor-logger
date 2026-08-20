# Mobile Sensor Logger

Android capture app for 3D reconstruction data. Records the camera image for
every frame together with the ARCore pose and intrinsics it was taken with, so
the session can be processed on a workstation afterwards.

Written against the ARCore **C API** — the app is C++ with no application Java
or Kotlin, hosted by `GameActivity`.

## Output

One directory per session under `<external files>/sessions/`:

| File | Contents |
| --- | --- |
| `frames/<timestamp_ns>.yuv` | Raw `YUV_420_888` planes, concatenated |
| `frames.csv` | `timestamp_ns, filename, width, height, num_planes,` then row stride, pixel stride and length per plane |
| `poses.csv` | `timestamp_ns, tx, ty, tz, qx, qy, qz, qw, fx, fy, cx, cy, image_width, image_height` |
| `points.csv` | `timestamp_ns, x, y, z, confidence` |
| `session.json` | Frame counts and the pose convention |

Poses are in ARCore's right-handed world frame, rotation as a quaternion in
`(x, y, z, w)` order. Timestamps are nanoseconds on the system clock, taken from
ARCore rather than read on arrival, so they refer to capture time, and they are
the key joining every file.

Images are written exactly as ARCore hands them over, without conversion. The
NDK has no JPEG encoder, and converting on the phone would spend the capture's
frame budget on work the workstation can do later. The strides in `frames.csv`
are what make the planes decodable — they are not the same as `width`, and
`u`/`v` may be interleaved depending on the device.

The app selects the camera configuration with the largest CPU-accessible image,
since that resolution caps the detail any later processing can recover. It is
lower than the camera's full capability.

A frame is only logged if it was tracking **and** its image was written. Poses
without images cannot be processed, so a partial frame is counted rather than
half-written; `session.json` reports both counts, which is the quickest signal
that a capture went badly.

## Building

Requires the Android SDK, NDK, and CMake. ARCore needs API 24+, an
ARCore-supported device, and arm64/armv7 — there is no emulator path.

There is no Gradle wrapper in the repository yet; open the project in Android
Studio, or generate one with `gradle wrapper`.

## Status

Never built or run — there is no Android SDK in the development environment.
`native_app.cc` is the least certain part: the `GameActivity` input handling,
the JNI permission request, and the EGL setup are all unverified. The ARCore
dependency version in `app/build.gradle.kts` should be checked against the
current release before building.

## Not implemented

- **Camera preview.** ARCore renders into an external OES texture that nothing
  draws, so the screen stays black while tracking runs.
- **UI.** Any tap toggles recording; state is visible only through logcat.
- **Storage management.** Sessions are written until the device runs out of
  space; nothing warns, caps, or cleans up.
