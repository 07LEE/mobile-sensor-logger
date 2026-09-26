# 13. Extract the Platform Layer from native_app.cc

Date: 2026-09-27

## Status

Accepted.

## Context

`native_app.cc` had grown to 1,589 lines, about a quarter of the native code. Roughly 400 of them were JNI glue with no dependency on the app's state: the runtime permission check and request, the last-known location, starting and stopping `RecordingService`, and the battery and thermal readings. ADR 6 gave every other domain its own directory; these had none.

`LocationData` also lived in `session_recorder.h`, so the JNI code that produces it and the recorder that stores it were tied together through the heavy include graph ADR 5 set out to avoid.

## Decision

Move the JNI glue into `platform/`:

- `permissions`: `HasPermission`, `HasCameraPermission`, `LogOptionalPermissions`, `RequestCameraPermission`.
- `location`: `LocationData` and `GetLocationData`.
- `recording_service`: `StartRecordingService` and `StopRecordingService`.
- `device_status`: `ThermalSample` and `ReadThermalSample`.
- `log.h`: `LogInfo` and `LogError`.

`session_recorder.h` includes `location.h` instead of defining `LocationData`. The code moved as written; no behavior changed.

## Consequences

- `native_app.cc` drops to about 1,190 lines and keeps the app state, the command handlers and the main loop.
- The JNI glue can be read and changed without the rest of `native_app.cc` in view.
- The button handlers, `StatusLines` and the event loop are still in `native_app.cc`; splitting them needs the shared `AppState` broken up first, which this change does not attempt.
