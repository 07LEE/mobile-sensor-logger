# Mobile Sensor Logger

Android app that records camera frames and motion sensor data for building 3D models on a computer.

- Records raw camera frames with accelerometer and gyroscope data
- Keeps selected sharp keyframes by default, or every frame when requested
- Continues an active recording when the app moves to the background

## Install

No release APK is published yet. See the [build and install guide](docs/building.md) to install it from source.

## Using it

| Button | Action |
| --- | --- |
| Volume down | start and stop recording |
| Volume up | show or hide the camera |
| Lock | pin the current exposure so recording starts on it; reads PINNED once tapped |
| Retention | switch between keeping the sharpest photos (sharpest) and every photo (all) |
| Lens | switch to the next rear camera; only on phones with more than one |
| PRO | open advanced settings, view calibration availability, or start an extrinsic-calibration capture |
| Sessions | view saved sessions and delete them |
| Home | send the app to the background; an active recording continues |

Lock, Retention, Lens, PRO and Sessions are not available while recording.

To stop a background recording, return to the app and press volume down. When notification permission is granted, the ongoing notification indicates that background recording is still active.

## On screen

The screen shows numbers rather than the camera picture unless you press volume up.

```text
REC 0:57   36 MIN LEFT      elapsed, and how long the free space lasts
153 KEPT / 1696 SEEN        photos saved out of photos checked
2.9GB USED 111.0GB FREE
SHIFT 2/12  DIFF 4/6        movement against the amount that keeps a photo
4080X3060
1/30 ISO247 0.91M           shutter, sensitivity, focus — held or still moving
DROP 0 NOIMG 0 IDX 0 IMU 36K  DROP, NOIMG and IDX should be zero
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
| [docs/building.md](docs/building.md) | build and install the app from source |
| [docs/output-format.md](docs/output-format.md) | what a session leaves on disk, and how to read it |
| [docs/settings.md](docs/settings.md) | the advanced settings, and what each choice costs |
| [docs/devices.md](docs/devices.md) | hardware profiles and parameters for tested devices |
| [docs/calibration-guide.md](docs/calibration-guide.md) | when to trust Camera2 calibration and when to calibrate a camera or camera-IMU pair yourself |

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
