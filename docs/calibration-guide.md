# Camera Calibration Guide

How to decide whether Android's camera calibration is sufficient, when to calibrate a lens yourself, and why visual-inertial reconstruction needs more than camera intrinsics.

## Policy at a glance

| Use case | Required calibration | Project policy |
| --- | --- | --- |
| Camera-only reconstruction | Intrinsics and distortion for the selected lens and image geometry | Use the Camera2 values recorded in `session.json` when present and geometrically consistent; calibrate manually when they are absent or fail validation. |
| Camera-IMU reconstruction | Camera intrinsics and distortion, camera-to-IMU rotation and translation, and camera/IMU time offset | Camera2 intrinsics are still useful, but they do not normally replace a Kalibr camera-IMU calibration. Use an AprilGrid capture that excites all axes. |
| Multiple physical cameras | Intrinsics and distortion per lens, plus transforms between lenses if they are used together | Never reuse one lens's values for another. Check `camera_id` and `physical_id` before combining frames. |

Do not substitute a guessed per-device fallback when calibration is missing. A missing calibration is visible and fixable; a plausible but incorrect fallback can silently produce incorrect geometry.

## What Android can provide

Camera2 exposes [`LENS_INTRINSIC_CALIBRATION`](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LENS_INTRINSIC_CALIBRATION) as `[fx, fy, cx, cy, skew]` and [`LENS_DISTORTION`](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LENS_DISTORTION) as `[k1, k2, k3, p1, p2]`. These fields are manufacturer calibration for one camera device, but both are optional and may be absent on otherwise usable hardware.

The values use the coordinate system of `SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE`, not automatically the dimensions of the YUV file. If the active array and recorded frame differ, downstream code must account for crop, aspect ratio, and scale; multiplying every value by one scale factor is only valid when the geometries actually differ by that uniform scale.

`LENS_DISTORTION` describes the Android Brown-Conrady model. Do not substitute the deprecated `LENS_RADIAL_DISTORTION`, whose normalization was inconsistent, and do not pass the coefficients to a solver that assumes a different order or camera model without converting them.

Camera2 can also expose `LENS_POSE_ROTATION`, `LENS_POSE_TRANSLATION`, and [`LENS_POSE_REFERENCE`](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LENS_POSE_REFERENCE). The reference may be the primary camera, the gyroscope, undefined, or an automotive frame. A pose relative to the primary camera is an inter-camera pose, not a camera-to-IMU transform.

## What this app records

For the selected `camera_id`, `session.json` records the manufacturer values when `LENS_INTRINSIC_CALIBRATION` is available:

```json
"camera_id": "2",
"intrinsics": [fx, fy, cx, cy, skew],
"distortion": [k1, k2, k3, p1, p2],
"pre_correction_active_array": [x, y, width, height]
```

If Camera2 does not publish intrinsics, `intrinsics` is `null` and the app logs a warning at camera startup. The capture remains usable, but a reconstruction must obtain intrinsics another way.

The current manifest uses intrinsics as the gate for the calibration block. If intrinsics exist but `LENS_DISTORTION` is absent, the recorded distortion array remains all zeroes; an all-zero array therefore does not by itself prove that the physical lens has zero distortion. Validate that case on the target device before treating it as calibrated.

When Camera2 publishes a lens pose, the manifest also records:

```json
"lens_pose_translation_m": [x, y, z],
"lens_pose_rotation_xyzw": [x, y, z, w],
"lens_pose_reference": 0
```

Always interpret the pose using `lens_pose_reference`. When the reference is `PRIMARY_CAMERA`, these values locate one lens relative to another and cannot be used as the camera-to-IMU transform.

## Decision procedure for a new device

1. Record a short session with the exact lens and capture size intended for production.
2. Check `camera_id`, `intrinsics`, `distortion`, and `pre_correction_active_array` in `session.json`, then check `physical_id` in `capture.csv` when the selected camera is logical.
3. Confirm that the intrinsics refer to the recorded image geometry. Account for crop and scale when the active array differs from the frame dimensions.
4. For camera-only reconstruction, validate manufacturer values with a small checkerboard dataset before accepting them for that device and lens.
5. If intrinsics are absent, distortion is ambiguous, or validation fails, run a full intrinsic calibration and store the resulting model alongside the device/lens profile used by downstream tools.
6. For camera-IMU reconstruction, run a separate AprilGrid calibration even when camera intrinsics are present, unless a gyroscope-referenced factory pose has been explicitly validated for coordinate convention, accuracy, and the downstream solver. Temporal offset still requires validation.

## Validating or replacing camera intrinsics

A plain checkerboard is suitable for camera intrinsics. Keep the board flat, use a known square size, and capture it at varied distances and tilts while covering the center, all four edges, and all four corners of the image. A dataset concentrated near the center can estimate focal length while leaving edge distortion weakly constrained.

Solve with [OpenCV](https://docs.opencv.org/4.x/d9/d0c/group__calib3d.html) or another tool that supports the intended camera model. Review per-view reprojection error rather than accepting only the aggregate RMS, remove detections that are visibly wrong, and compare the result against the Camera2 values after mapping both into the same image coordinate system.

Self-calibration in tools such as [COLMAP](https://colmap.github.io/) or [OpenSfM](https://github.com/mapillary/OpenSfM) is a fallback for image sets with enough texture, viewpoint change, and overlap. It is less predictable than a controlled target capture and should not silently overwrite manufacturer values without a quality comparison.

Do not patch a shared device value directly into every session unless the lens, focus policy, resolution, crop, stabilisation, and camera model are the same. This app disables optical and digital stabilisation and holds focus during recording specifically to keep the geometry stable.

## Camera-IMU calibration is separate

Visual-inertial reconstruction additionally needs the rigid transform between the camera and IMU plus their time offset. Camera intrinsics describe rays inside one camera; they contain neither of those quantities.

The time offset is not zero even when the camera timestamp source is `REALTIME`. On the tested Galaxy S25 Ultra ultrawide it was about 13ms with a 16.67ms exposure, which at the angular rate of a handheld capture is a visible rotation between an image and the gyroscope. See [output-format.md](output-format.md#inertial-data) for how it relates to exposure and rolling shutter.

Use an AprilGrid target with Kalibr for the final camera-IMU calibration. A plain checkerboard remains useful for intrinsics, but its rotational symmetry can reverse corner ordering between views and invalidate a camera-IMU solve. AprilGrid tags carry unique IDs, removing that ambiguity.

During the AprilGrid capture, keep the target stationary and visible while moving the phone through rotations and translations that excite all six IMU axes. Use `retention=all`; sampling only the sharpest keyframes removes the dense camera timing that camera-IMU calibration needs.

## Acceptance checks

- The calibration belongs to the exact physical lens used by the session.
- Intrinsics, distortion, and image coordinates use the same crop and resolution.
- Reprojection errors are low and do not grow systematically near image edges.
- The target covers the image periphery rather than only its center.
- Camera-IMU calibration uses an unambiguous target and excites all axes.
- Camera and IMU timestamp compatibility is verified separately from geometric calibration.

## See also

- [output-format.md](output-format.md#calibration) — calibration fields written in each session
- [devices.md](devices.md) — parameters measured on tested devices
