# 9. Log Thermal Status and Battery Temperature Per Session

Date: 2026-08-23

## Status

Proposed.

## Context

Two back-to-back sessions showed camera frame delivery stalling for up to ~1 second at a time — 34.8% of the first session's duration, 90% of the second's, which started only 19 seconds after the first ended. `capture.csv` timestamps come from the sensor's own capture time, and IMU sampling never gapped in the same windows, so the camera pipeline itself stalled — not the app.

The pattern (worse in the hotter, later session) points to thermal throttling, but the app has no signal for device thermal state to confirm it. `PowerManager.getCurrentThermalStatus()` (API 29+) and `BatteryManager`'s battery temperature are both Java-only, unavailable through the NDK.

## Decision

Add a minimal JNI call, following the same pattern as `RecordingService`, that reads both values and returns them to native code. Sample periodically during a recording — not just once — and log to a new `thermal.csv` (`timestamp_ns, thermal_status, battery_temp_c`) so it can be lined up against gaps in `capture.csv`.

## Consequences

- Confirms or refutes thermal throttling on the next capture, instead of inferring it from timestamp forensics.
- Does not fix the stalls — that's outside the app's control either way.
- `thermal_status` is unavailable below API 29; `thermal.csv` still gets a battery-temperature column on every supported device.
