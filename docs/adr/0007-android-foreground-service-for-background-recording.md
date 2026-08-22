# 7. Android Foreground Service for Background Recording

Date: 2026-08-22

## Status

Accepted.

Amends the application scope aspect of [0002-cpp-arcore-capture-app](0002-cpp-arcore-capture-app.md) and [0003-camera2-capture-without-arcore](0003-camera2-capture-without-arcore.md). The decision to keep all domain logic, camera/IMU processing, rendering, and disk I/O strictly in C++ native code stands.

## Context

The application is a dedicated sensor logger. A critical operational requirement is continuous background capture: when a user presses the Home button, turns off the screen, or switches to another app during a recording session, camera image acquisition, IMU logging, and disk writing must continue uninterrupted.

Starting in Android 9 (API 28) and enforced strictly in Android 14 (API 34), the Android OS automatically disconnects camera access (`camera error 4`) and throttles background threads unless the app holds an active Foreground Service with an explicit `camera` foreground service type (`android:foregroundServiceType="camera"`) and an ongoing status bar notification.

However, the Android NDK provides no C-API counterparts for `android.app.Service`, `NotificationManager`, or `NotificationChannel`. These APIs are strictly exposed through the Android Java Framework.

## Decision

Introduce a single, minimal Java wrapper class (`com.sensor.logger.RecordingService`) extending `android.app.Service` to host the mandatory Android Foreground Service and status bar notification.

Strict scope constraints for Java code:

1. **Zero Domain Logic**: `RecordingService` contains no capture, file I/O, or sensor logic. Its sole responsibility is invoking `startForeground()` with a status bar notification when instructed by C++.
2. **Native Control**: The C++ native engine (`native_app.cc`) controls the lifecycle of the service via JNI (`StartRecordingService` / `StopRecordingService`) when recording sessions start and stop.
3. **Architecture Preservation**: All camera management (Camera2 NDK), IMU logging (ASensorManager NDK), frame filtering, and file writing remain 100% in C++ native code.

## Consequences

- Continuous background recording is achieved without camera disconnects or frame drops when the screen is off or the app is backgrounded.
- Complies with Android 14+ background camera security and power management policies.
- Keeps the C++ core engine strictly isolated from Java application code, maintaining the project's native architecture principle.
