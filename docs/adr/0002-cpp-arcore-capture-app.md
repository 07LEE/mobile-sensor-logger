# 2. C++ ARCore Capture App

Date: 2026-08-20

## Status

Accepted.

Supersedes [0001-standalone-mobile-sensor-logger](0001-standalone-mobile-sensor-logger.md),
which is cancelled.

## Context

This project is a mobile capture app. Its job is to get camera images and the
pose each was taken from off the phone, accurately enough that the data can be
processed on a workstation afterwards. How that processing works is out of scope
here.

ADR 0001 framed the work as sensor logging — IMU, GPS, and camera streams
written to CSV — and chose Flutter to implement it. Both were wrong.

Flutter cannot supply what the capture needs. Its camera plugin exposes no
capture timestamp: `CameraImageData` carries format, dimensions, planes,
aperture, exposure, and sensitivity, and nothing else. A frame can therefore
only be stamped when it reaches the Dart isolate, behind platform channel
transit and GC pauses, which is not when it was taken. IMU samples do carry
hardware timestamps, but that does not help a camera stream with none.

Logging raw sensor streams was also the wrong layer. Turning camera and IMU into
poses is the hard part and is device-dependent, and ARCore already does it,
reporting a pose together with the timestamp that pose belongs to. Consuming
poses removes the problem instead of solving it.

## Decision

1. **ARCore C API with C++.** No application Java or Kotlin; `GameActivity`
   hosts the native entry point.
2. **Record the camera image for every recorded frame,** alongside its pose,
   intrinsics, and timestamp. Poses without images are not processable, so the
   images are the point of the capture, not an extra.
3. **Select the camera configuration with the largest CPU-accessible image,**
   since that resolution bounds the detail any later reconstruction can recover.
4. **Drop frames captured while tracking is lost** rather than writing them with
   a stale pose, and record how many were dropped so a bad capture is visible
   afterwards.
5. **Treat ARCore's tracking as a black box.** It cannot be modified, only
   replaced wholesale.

## Consequences

Positive:

- Camera-to-IMU synchronisation stops being this project's problem.
- Each image carries the pose and intrinsics it was taken with, which is the
  input a reconstruction needs.
- Failed captures are identifiable from the manifest rather than discovered
  later during processing.

Negative:

- ARCore poses drift over long sessions, so large scenes will need refinement
  off-device.
- CPU-accessible image resolution is bounded by ARCore's camera configurations
  and is lower than the camera's full capability.
- Writing an image per frame makes sessions large, and sustaining that write
  rate may itself limit the capture rate.
- Optical image stabilisation varies intrinsics per frame and cannot be disabled
  through the standard API on many devices. Its effect on the captured data is
  unmeasured.
- Android only.

Unverified:

- Nothing here has been built or run; there is no Android SDK in the development
  environment.
- Whether ARCore's pose accuracy is sufficient for the intended processing.
