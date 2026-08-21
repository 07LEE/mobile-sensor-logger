# 3. Camera2 Capture Without ARCore

Date: 2026-08-21

## Status

Accepted.

Supersedes the tracking half of
[0002-cpp-arcore-capture-app](0002-cpp-arcore-capture-app.md). That ADR's
decision to keep application code free of Java stands, and is part of why this
one is possible.

## Context

The phone's job is to record what a reconstruction needs and get it off the
device. Poses are computed on a workstation, which has the compute for a global
optimisation and can be re-run against the same capture as many times as the
result needs. ARCore was chosen in 0002 when it was not established that
anything else on the device could produce a usable capture at all.

Two things have been established since.

A probe on the target device reports `ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE` as
`REALTIME` for every camera it has. Image and inertial timestamps are on the
same clock, which is the condition any offline visual-inertial reconstruction
depends on and the one thing no amount of software could have worked around.

And ARCore's cost has turned out to be the camera rather than the tracking. It
owns the camera outright: CPU images cap at 1920x1080 rather than sensor
resolution, focus cannot be locked, and exposure follows the scene. All three
bite hardest in close detail work, which is where the fewest frames are taken
and where each of them has to survive.

## Decision

Capture through the Camera2 NDK. Record images and inertial samples, and compute
no pose on the device.

## Consequences

The camera stops being something the app asks permission of. Sensor resolution,
focus that can be held, exposure that can be held.

Camera2 has a complete NDK surface, so 0002's decision to keep application code
free of Java survives intact. Keeping ARCore's poses *and* a controlled camera
was only ever possible through `SharedCamera`, which does not exist in the C API
at all — it hangs off the Java `Session`, so adopting it would have moved
session ownership out of native code entirely and left the native side a library
that Java calls.

Nothing in a camera pipeline is fully open; the ISP still decides what a pixel
means. The difference is that Camera2's decisions can be turned off and set by
hand, and ARCore's could be neither inspected nor overridden. A session came
back with 92% of its feature points as NaN, permanent once it started, and there
was nothing to do about it but drop them.

What is given up is knowing, while filming, that the capture is working. ARCore
reported when it lost tracking and why, and that reached the screen. Nothing on
the device will now answer whether a session will reconstruct. Sharpness,
exposure and frame-to-frame motion are all measurable without a pose and are
closer to what actually decides a capture's fate, but none of them answers that
question, and finding out at the workstation means going back to the location.

Keyframe selection loses its geometric basis and moves to frame-to-frame image
motion. That measures how much the view changed rather than inferring it from a
pose and an estimate of how far away the scene is — a more direct signal, and it
removes the dependence on the point cloud, which had not been reliable enough to
build on.

Metric scale is no longer recovered on the device. It has to come from the
inertial data offline, which needs the camera-to-IMU transform and the exposure
time offset calibrated per device model — work ARCore was doing invisibly and
well.

The preview has to be rebuilt. ARCore rendered the camera into a texture for
free; Camera2 hands over planes, so the preview becomes a second, smaller output
stream uploaded as textures and converted in a shader.
