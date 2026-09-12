# 12. Remove On-Device AprilTag Detection

Date: 2026-09-12

## Status

Accepted.

Supersedes the AprilTag-detection half of
[0011-pro-panel-extrinsic-capture-button](0011-pro-panel-extrinsic-capture-button.md).
That ADR's other decision — `EXTRINSIC` as an action button that switches
`retention` to `all` in memory and starts recording immediately — still
holds.

## Context

`apriltag` was the app's only third-party native dependency: a git submodule
that has to be cloned and initialized separately from the rest of the repo,
pinned to a specific upstream commit, and wired into `CMakeLists.txt` as its
own static build. It existed for one narrow purpose — a live `TAGS N/M` HUD
line during an `EXTRINSIC` take, so a bad calibration capture (target lost
from frame, too few tags visible, motion blur) could be caught on the device
instead of only after a multi-hour Kalibr run on the workstation.

## Decision

Delete the submodule (`app/src/main/cpp/third_party/apriltag`), its
`.gitmodules` entry, the `AprilTagDetector` wrapper
(`calibration/apriltag_detector.cc`/`.h`), the `apriltag_cols`/`apriltag_rows`
`capture.conf` keys, and the `TAGS N/M` HUD line. `EXTRINSIC` still switches
`retention` to `all`, closes the PRO panel, and starts recording immediately
— that part of ADR 11 is unaffected, since none of it touches the tag
detector.

## Consequences

- The app is back to zero third-party native dependencies; nothing under
  `app/src/main/cpp/` needs a submodule init to build.
- A bad `EXTRINSIC` take (target out of frame, too few tags, motion blur) is
  no longer caught on the device — this is the same failure mode ADR 11 was
  written to catch, and it is reintroduced. Confirming a take is usable is
  back to eyeballing the picture during capture and finding out for certain
  only after the workstation Kalibr run.
- `data/kalibr_input/apriltag/target.yaml`'s AprilGrid target (5x7, tag36h11)
  is still the physical target to use — only the app's live count of it is
  gone, not the Kalibr-side pipeline.
