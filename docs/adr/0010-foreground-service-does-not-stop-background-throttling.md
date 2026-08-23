# 10. Foreground Service Alone Does Not Stop Background Camera Throttling

Date: 2026-08-23

## Status

Accepted.

## Context

A recording was backgrounded for 45.3 seconds partway through, with `RecordingService` confirmed running via `dumpsys activity services` the whole time. `capture.csv` showed 0% stalled time in the foreground portions (30.6s combined) and 96.8% stalled while backgrounded — nearly the entire background window produced almost no new captures, in a regular ~1-second-every-~3-seconds pattern.

[ADR 7](0007-android-foreground-service-for-background-recording.md) added the foreground service specifically so backgrounding wouldn't disrupt capture, and assumed a running service with `foregroundServiceType="camera"` was sufficient. This device runs API 36, newer than the API 34 ADR 7 was written against. The service being alive is not, on this build, enough to keep the camera at its foreground rate.

## Decision

Log every background/foreground transition during a recording to `lifecycle.csv` (already added, alongside `thermal.csv`), so any future session can show directly whether a gap in `capture.csv` lines up with backgrounding, rather than relying on remembering it. No further mitigation is decided here — a way to keep capture rate up while backgrounded, or to warn the user it degraded, is open follow-up work.

## Consequences

- A recording that was ever backgrounded should not be treated as equivalent to a fully foregrounded one; its captured frame rate during those windows is unreliable.
- ADR 7's "camera & IMU capture active in the background" comment ([native_app.cc](../../app/src/main/cpp/native_app.cc)) is only partially true: the session keeps running and nothing crashes, but the camera itself is heavily throttled by the OS regardless.
- The actual fix, if one exists on this API level, is unresolved.
