# 11. PRO Panel Extrinsic Capture Button

Date: 2026-08-29

## Status

Accepted.

## Context

Camera-to-IMU extrinsic calibration is computed on a workstation with Kalibr,
not on the device — consistent with [ADR 3](0003-camera2-capture-without-arcore.md)'s
decision that poses and global optimisation stay off the phone. But a mistake
made during *capture* still costs the workstation run: a ~70s checkerboard
capture went through a full overnight `kalibr_calibrate_imu_camera` run and
came back with a mean reprojection error around 18px, unusable. The cause was
the checkerboard's rotational symmetry — a plain checkerboard's corner
ordering can flip depending on viewing angle, and Kalibr has no way to catch
that at capture time, only after the batch optimisation has already run on
bad correspondences. The fix was switching the physical target from a
checkerboard to an AprilGrid, which has no such ambiguity since every tag
carries a unique ID — but nothing on the device would have caught a
similarly bad take (target lost from frame, too few tags visible, motion blur
scrambling detection) before it reached the same multi-hour round trip.

The capture technique itself — moving the phone around a stationary target to
excite all six IMU axes while keeping the target in frame — was previously
undocumented anywhere reachable from a live capture; it only existed in prior
session notes. Retention also matters: this kind of capture needs
`retention = all` rather than the default `sharpest`, and the retention
toggle is a manual step that's easy to forget before recording and to forget
to undo after.

## Decision

Add an `EXTRINSIC` action button to the PRO panel — not a cycling settings
row like `shutter`/`fps`/`mains`/`shift`/`residual`, but an action alongside
`RESET`/`CLOSE`. Tapping it:

1. Switches `retention` to `all` in memory for this recording only, without
   calling `CaptureConfig::Save` — unlike the `RETENTION` toolbar button,
   which does persist to `capture.conf` (`ToggleRetention` in
   `native_app.cc`, despite `capture_config.h`'s stale comment claiming it
   doesn't). `EXTRINSIC` deliberately skips that: the file should read the
   same after this take as before it.
2. Turns on live AprilTag detection, adding a `TAGS N/M` line to the HUD.
   `M` comes from two new `capture.conf` keys, `apriltag_cols` /
   `apriltag_rows` (default `5` / `7`, matching the grid already generated
   at `data/kalibr_input/apriltag/apriltag_5x7.pdf`) — not hardcoded, since
   the physical target can change independently of the app.
3. Closes the PRO panel and starts recording immediately, the same as
   pressing volume-down.

Recording stops the normal way, with volume-down. On stop, retention and the
`TAGS` HUD line both revert automatically to whatever they were before
`EXTRINSIC` was pressed — this covers the "forgot to switch back" failure
mode symmetrically, not just "forgot to switch to `all`". Because opening the
PRO panel is itself refused while recording (existing rule, unchanged), there
is no way to press `EXTRINSIC` again mid-recording, so it needs no separate
stop/disabled state of its own.

AprilTag detection uses AprilRobotics' `apriltag` C library (MIT-licensed,
the same detector Kalibr itself uses), vendored into the build via CMake
rather than a prebuilt SDK — plain, readable C source, not a binary blob.

## Consequences

- This is the app's first third-party native dependency. Everything else in
  `app/src/main/cpp/` is hand-written, including `pipeline/Sharpness`'s own
  Laplacian variance rather than pulling in an image-processing library for
  it.
- `CMakeLists.txt` and the build files change to add the dependency.
- If `apriltag_cols`/`apriltag_rows` don't match the physical target in use,
  the `TAGS N/M` denominator is simply wrong — nothing validates it against
  the target actually in frame.
- This button does not compute or store any extrinsic value on the device.
  It only helps a capture survive contact with Kalibr; the transform itself
  is still Kalibr's output, on the workstation, per ADR 3.
- `apriltag` is vendored pinned to a specific upstream commit, not a floating
  branch — this path is only exercised during a calibration capture, rare
  enough that a build break from an unpinned upstream change could sit
  undiscovered for a long time before the next time this button is used.
