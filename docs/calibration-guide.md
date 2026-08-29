# Camera Calibration Guide

What to do when a device doesn't publish camera calibration.

## What calibration is

`session.json`'s `intrinsics` (`[fx, fy, cx, cy, skew]`) and `distortion`
(`[k1, k2, k3, p1, p2]`) describe how the camera maps 3D points to pixels —
see [output-format.md](output-format.md#calibration) for the exact fields and
units. 3D reconstruction needs them to turn pixel coordinates back into
geometry.

## When it's missing

`ACAMERA_LENS_INTRINSIC_CALIBRATION` is an optional Camera2 characteristic —
plenty of devices don't publish it. When that happens, `intrinsics` in
`session.json` is `null` and the app logs a warning at recording start; the
capture itself is unaffected, only reconstruction from it needs a workaround.

There's no built-in fallback table of per-device values in this app: a wrong
guess is worse than an honest `null`, since a bad calibration produces
plausible-looking but incorrect 3D geometry instead of an obvious failure.

## Getting calibration yourself

**Checkerboard calibration** — photograph a printed or displayed checkerboard
from many angles/distances, then solve with [OpenCV](https://docs.opencv.org/4.x/d9/d0c/group__calib3d.html)
or [Kalibr](https://github.com/ethz-asl/kalibr). Most reliable option, and the
same tool already used for camera-IMU extrinsic calibration in this project.

**Self-calibration during reconstruction** — tools like
[COLMAP](https://colmap.github.io/) or [OpenSfM](https://github.com/mapillary/OpenSfM)
can solve for intrinsics alongside pose and structure from a session with
enough camera motion, no checkerboard needed.

Either way, once you have real numbers for a device, patch them into that
session's `session.json` before reconstruction:

```json
"intrinsics": [fx, fy, cx, cy, skew],
"distortion": [k1, k2, k3, p1, p2],
"pre_correction_active_array": [x, y, width, height]
```

## See also

- [output-format.md](output-format.md#calibration) — the field shapes this app writes
- [devices.md](devices.md) — parameters measured on tested devices
