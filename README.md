# Mobile Sensor Logger

Android capture app for 3D reconstruction data. Records ARCore's tracked camera
poses, intrinsics, and feature point cloud to files on the device, so a session
can be processed off-device afterwards.

Written against the ARCore **C API** — the app is C++ with no application Java
or Kotlin, hosted by `GameActivity`.

## Output

One directory per session under `<external files>/sessions/`:

| File | Contents |
| --- | --- |
| `poses.csv` | `timestamp_ns, tx, ty, tz, qx, qy, qz, qw, fx, fy, cx, cy, image_width, image_height` |
| `points.csv` | `timestamp_ns, x, y, z, confidence` |
| `session.json` | Frame counts and the pose convention |
| `frames/` | Reserved for image data |

Poses are in ARCore's right-handed world frame, rotation as a quaternion in
`(x, y, z, w)` order. Timestamps are nanoseconds on the system clock, taken from
ARCore rather than read at arrival, so they refer to capture time.

Frames captured while tracking is lost are dropped rather than written with a
stale pose. `session.json` reports how many, which is the quickest signal that a
capture went badly.

## Building

Requires the Android SDK and NDK. ARCore needs API 24+, an ARCore-supported
device, and arm64/armv7 — there is no emulator path.

```bash
./gradlew assembleDebug
```

## Status

Not yet built or run — no Android SDK is installed in the development
environment. See [Not implemented](#not-implemented) for what is missing beyond
that.

## Not implemented

- **Camera preview.** ARCore renders into an external OES texture that nothing
  currently draws, so the screen stays black while tracking runs.
- **Image capture.** `frames/` is created but no image data is written yet.
- **UI.** Any tap toggles recording; state is visible only through logcat.
- **Raw frame recording via `SharedCamera`,** which would keep the option of
  running a different SLAM over the same capture.
