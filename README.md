# Mobile Sensor Logger

Android app that records camera photos and motion sensor data for building 3D models on a computer.

- Takes photos at the selected camera's largest CPU-readable YUV resolution
- Keeps the sharpest photo of each stretch of movement, not every frame
- Records the accelerometer and gyroscope on the same clock as the photos

## Install

Download the latest APK from the [Releases](https://github.com/07LEE/mobile-sensor-logger/releases) page and open it on the phone, allowing installs from unknown sources when asked.

## Using it

| Button | Action |
| --- | --- |
| Volume down | start and stop recording |
| Volume up | show or hide the camera |
| Lock | pin the current exposure so recording starts on it; reads PINNED once tapped |
| Retention | switch between keeping the sharpest photos (sharpest) and every photo (all) |
| Lens | switch to the next rear camera; only on phones with more than one |
| PRO | open the advanced settings |
| Sessions | view saved sessions and delete them |
| Home | leave the app, closing the session cleanly |

Lock, Retention, Lens, PRO and Sessions are not available while recording.

## On screen

The screen shows numbers rather than the camera picture unless you press volume up.

```text
REC 0:57   36 MIN LEFT      elapsed, and how long the free space lasts
153 KEPT / 1696 SEEN        photos saved out of photos checked
2.9GB USED 111.0GB FREE
SHIFT 2/12  DIFF 4/6        movement against the amount that keeps a photo
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

## Documentation

| Document | Description |
| --- | --- |
| [docs/output-format.md](docs/output-format.md) | what a session leaves on disk, and how to read it |
| [docs/settings.md](docs/settings.md) | the advanced settings, and what each choice costs |
| [docs/devices.md](docs/devices.md) | hardware profiles and parameters for tested devices |
| [docs/calibration-guide.md](docs/calibration-guide.md) | when to trust Camera2 calibration and when to calibrate a camera or camera-IMU pair yourself |

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
