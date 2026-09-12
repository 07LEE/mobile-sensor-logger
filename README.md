# Mobile Sensor Logger

Android app that records camera images and inertial data for 3D reconstruction on a workstation.

- Captures at the device's largest `YUV_420_888` size — 4080x3060 on a Galaxy S25 Ultra
- Writes the sharpest frame of each stretch of movement, not every frame
- Records accelerometer and gyroscope on the same clock as the images
- Computes no pose on the device

C++ throughout, against the Camera2 NDK, with no application Java or Kotlin. `GameActivity` hosts it.

## Building and running

```bash
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Needs the Android SDK, NDK and CMake; versions are pinned in `app/build.gradle.kts` and the wrapper. Built with JDK 21. There is no emulator path — the capture path is a real camera.

| Button | Action |
| --- | --- |
| **Volume down** | start and stop recording |
| **Volume up** | show or hide the camera |
| **The lock button** | pin the current metered exposure so recording starts on it, wherever the phone is pointed when the key is pressed; refused while recording |
| **The retention button** | toggle `sharpest` / `all`; refused while recording |
| **The lens button** | next rear lens; refused while recording |
| **The PRO button** | open the panel for `shutter`, `fps`, `mains`, `shift` and `residual` — see below; refused while recording |
| **The sessions button** | view saved sessions, and delete them individually; refused while recording |
| **Home** | leave, closing the session cleanly |

The PRO panel has one row per setting that cycles its value on tap, the same way retention and lens already do, plus a **RESET** button beside **CLOSE** that puts all five back to their defaults at once. Unlike retention and lens, a change here is also written back to `capture.conf` (see [docs/settings.md](docs/settings.md)), so it survives the app being killed rather than reverting to whatever the file said when this session opened.

Nothing that could lose a capture sits behind a touch. Deleting a session is: tap **DEL** next to it, then tap the same button again to confirm — one tap never deletes anything, and there is no delete-all.

Pulling and clearing a device:

```bash
adb pull /sdcard/Android/data/com.sensor.logger/files/sessions data/
adb shell run-as com.sensor.logger \
  rm -rf /sdcard/Android/data/com.sensor.logger/files/sessions
```

The second needs `run-as`; a plain `rm` is refused under scoped storage. For removing one session rather than all of them, the sessions button is usually easier than pulling and re-pushing.

## On screen

The camera is not drawn unless volume up asks for it. The numbers are what the screen is for during a capture; the picture is for aiming, which happens between captures more than during one.

```text
REC 0:57   36 MIN LEFT      elapsed, and how long the free space lasts
153 KEPT / 1696 SEEN        frames written against frames scored
2.9GB USED 111.0GB FREE
SHIFT 2/12  DIFF 4/6        movement against the thresholds that keep a frame
4080X3060
1/30 ISO247 0.91M           shutter, sensitivity, focus — held or still moving
DROP 0 NOIMG 0 IMU 36K      drops should be zero
BATT 78% 32.1C              shown whether or not anything is recording

[ LOCK ]
[ RETENTION SHARP ]
[ ULTRAWIDE 2.2MM ]
[ PRO ]
[ SESSIONS ]
```

`MIN LEFT` is measured from the rate this capture is actually filling the disk, not estimated in advance. The lock button reads **PINNED** once tapped, and brighter amber, so a stale pin is obvious rather than only readable from the label. The lens button only appears on a phone with more than one rear camera; the rest are always there, ordered by how often each gets touched around a recording — lock nearly every take, retention and lens less often, PRO rarer still since it is tuned once for a scene rather than every take, and sessions is an occasional housekeeping visit that sits furthest from a thumb reaching for the others in a hurry.

## Tested on

Hardware profiles and camera parameters for tested devices (Galaxy S25 Ultra, Galaxy Z Flip4) are documented in [docs/devices.md](docs/devices.md).

## Documentation

| Document | Description |
| --- | --- |
| [docs/output-format.md](docs/output-format.md) | what a session leaves on disk, and how to read it |
| [docs/settings.md](docs/settings.md) | `capture.conf`, and what each choice costs |
| [docs/devices.md](docs/devices.md) | hardware profiles and parameters for tested devices |
| [docs/calibration-guide.md](docs/calibration-guide.md) | what to do when a device doesn't publish camera calibration |
| [docs/adr/](docs/adr/) | why it is built this way |
