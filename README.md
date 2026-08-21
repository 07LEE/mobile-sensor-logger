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

| | |
| --- | --- |
| [docs/output-format.md](docs/output-format.md) | what a session leaves on disk, and how to read it |
| [docs/settings.md](docs/settings.md) | `capture.conf`, and what each choice costs |
| [docs/adr/](docs/adr/) | why it is built this way |

## Building and running

```bash
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Needs the Android SDK, NDK and CMake; versions are pinned in
`app/build.gradle.kts` and the wrapper. Built with JDK 21. There is no emulator
path — the capture path is a real camera.

| | |
| --- | --- |
| **Volume down** | start and stop recording |
| **Volume up** | show or hide the camera |
| **The button** | next rear lens; refused while recording |
| **Home** | leave, closing the session cleanly |

Nothing that could lose a capture sits behind a touch.

Pulling and clearing a device:

```bash
adb pull /sdcard/Android/data/com.sensor.logger/files/sessions data/
adb shell run-as com.sensor.logger \
  rm -rf /sdcard/Android/data/com.sensor.logger/files/sessions
```

The second needs `run-as`; a plain `rm` is refused under scoped storage.

## On screen

The camera is not drawn unless volume up asks for it. The numbers are what the
screen is for during a capture; the picture is for aiming, which happens between
captures more than during one.

```
REC 0:57   36 MIN LEFT      elapsed, and how long the free space lasts
153 KEPT / 1696 SEEN        frames written against frames scored
2.9GB USED 111.0GB FREE
SHIFT 2/12  DIFF 4/6        movement against the thresholds that keep a frame
4080X3060 ULTRAWIDE ALL
1/30 ISO247 0.91M LOCKED    shutter, sensitivity, focus — held or still moving
DROP 0 NOIMG 0 IMU 36K      drops should be zero

[ ULTRAWIDE 2.2MM - TAP ]
```

`MIN LEFT` is measured from the rate this capture is actually filling the disk,
not estimated in advance. `LOCKED` appears once exposure, white balance and
focus are held; watching them settle is how the moment to start is chosen.

## Tested on

Two devices. The numbers quoted elsewhere in this file come from the first.

| | Galaxy S25 Ultra | Galaxy Z Flip4 |
| --- | --- | --- |
| | SM-S938N, Android 16 | SM-F721N, Android 16 |
| Capture size | 4080x3060, 18.8MB | 4000x3000, 18.1MB |
| Luma row stride | 4096 | 4032 |
| Ultrawide | 2.2mm f/1.9 | 1.7mm f/2.2 |
| Main | 6.3mm f/1.7 (logical) | 5.0mm f/1.8 (logical) |
| Intrinsics fx, fy | 1648, 1650 | 1579, 1578 |
| Distortion k1 | +0.0159 | -0.0073 |
| Hyperfocal | 0.91m | 0.59m |
| Rolling shutter skew | 8.6ms | 32.0ms |
| Chroma layout | `semi_planar_vu` | `semi_planar_vu` |
| Timestamp source | `REALTIME` | `REALTIME` |

Every one of those is read from the device rather than assumed, which is what
the second one was needed to establish. The rolling shutter skew is the pair
worth looking at: nearly four times longer on the Flip4 and about as long as the
gap between frames, so the same movement skews its frames far more.

Both are Samsung and both report semi-planar chroma, so the planar path has
never run.


## Not implemented

- **Manual focus and exposure.** The capture request is the preview template, so
  both still follow the scene.
- **Cleaning up.** Old sessions are never removed; that is done over adb.
