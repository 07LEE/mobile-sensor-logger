# 2. Discontinue the Standalone Flutter Logger

Date: 2026-08-20

## Status
Accepted

Supersedes the implementation path chosen in
[0001-standalone-mobile-sensor-logger](0001-standalone-mobile-sensor-logger.md).
The architectural goal of that ADR — offline sensor logging on the phone — still
holds; only the decision to build the capture app ourselves in Flutter is
withdrawn.

## Context

The logger was implemented in Flutter through to a working pipeline: IMU, GPS,
battery, and camera engines, per-session CSV/JSON output following the ROS
conventions of web-ros-collector, and ZIP export. No survey of existing work was
carried out before or during that effort. Running one afterwards changed the
picture in two ways.

**A prior-art survey found the problem already solved.** Several open-source
projects capture the same sensor set for the same purpose, and treat
camera-to-IMU time synchronisation as the central difficulty rather than an
afterthought:

- [VIRec](https://github.com/A3DV/VIRec) — camera, IMU, and GPS on Android,
  synced to one clock source
- [mobile-ar-sensor-logger](https://github.com/OSUPCVLab/mobile-ar-sensor-logger)
  — Android and iOS, with a published analysis of achievable sync quality
  ([arXiv:2001.00470](https://arxiv.org/pdf/2001.00470))
- [OpenCamera Sensors](https://github.com/MobileRoboticsSkoltech/OpenCamera-Sensors)
  — synchronised video and IMU, from a mobile robotics lab
- [VideoIMUCapture](https://github.com/DavidGillsjo/VideoIMUCapture-Android) —
  additionally records optical image stabilisation state
- [sensors_for_ros](https://github.com/sloretz/sensors_for_ros) — publishes
  Android sensors directly to ROS 2 topics

**Flutter cannot supply the timestamps this data needs.** The camera plugin
exposes no capture time: `CameraImageData` carries `format`, `width`, `height`,
`planes`, `lensAperture`, `sensorExposureTime`, and `sensorSensitivity`, and
nothing else. Our `FrameWriter` therefore stamped frames with `DateTime.now()`
at the moment they arrived on the Dart isolate — a value that includes platform
channel transit, event queueing, and GC pauses, and that bears no fixed relation
to when the frame was actually captured. IMU samples are not affected in the
same way, since `sensors_plus` does surface the hardware timestamp, but a
correctly stamped IMU stream cannot be aligned to an incorrectly stamped camera
stream.

The intended uses — visual-inertial odometry, ROS replay, and machine learning
datasets — are governed by the strictest of the three. VIO requires millisecond
alignment, so the defect is disqualifying rather than merely undesirable.

Reaching the underlying `ACAMERA_SENSOR_TIMESTAMP` requires Camera2 or the NDK
camera API through a native platform channel. Writing one would mean replacing
the entire capture path in Kotlin, at which point Flutter contributes only the
user interface, and the projects listed above have already done that work.

Two hardware constraints apply regardless of implementation and were also
missed initially:

- Camera and IMU timestamps are only comparable when the device reports
  `SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME`. Devices reporting `UNKNOWN` place
  camera timestamps on a separate monotonic clock, and no software can align
  them. This is per-device and undocumented by vendors.
- Optical image stabilisation varies the camera intrinsics per frame, breaking
  the fixed-intrinsics assumption most VIO and SLAM pipelines make. It cannot be
  disabled through the standard API on many devices.

## Decision

Stop developing the Flutter capture application. Split the problem in two and
own only the half where customisation is actually needed.

1. **Capture** is delegated to an existing research-grade tool — OpenCamera
   Sensors or VIRec, selected after confirming the target device reports a
   realtime timestamp source.
2. **Conversion and tooling** becomes the project: turning captured sessions
   into rosbags, ML-ready datasets, and analysis CSVs. No surveyed tool covers
   this, and web-ros-collector's existing bridges already perform the phone-to-
   ROS message conversion, including the REP-103 axis remap. Repointing them
   from an HTTP source to a file source reuses that logic largely intact.

The Flutter code is left in version control rather than deleted. The ROS-
compatible schema defined for it remains valid and informs the converter's
output format.

## Consequences

Positive:
- Camera-IMU synchronisation is handled by implementations built and published
  for that purpose, instead of being approximated.
- Effort moves to the layer that is genuinely unserved by existing tools.
- Existing web-ros-collector bridge code is reused rather than reimplemented.
- The conversion layer is platform-independent, so it is unaffected by having no
  Mac or iOS device.

Negative:
- The Flutter implementation is discarded as a product, retaining value only as
  a record and as schema design.
- Capture behaviour is now bounded by a third-party tool; changing it means
  working in that codebase rather than our own.
- The project depends on the target device supporting a realtime timestamp
  source, which is still unverified.

Process:
- A prior-art survey belongs before an architecture decision, not after an
  implementation. ADR 0001 framed this project as a pivot away from an earlier
  approach, which was precisely the moment to check what already existed.
- Framework capability limits should be verified against the specific data the
  project needs — here, whether a capture timestamp is exposed at all — before
  committing to the framework.
