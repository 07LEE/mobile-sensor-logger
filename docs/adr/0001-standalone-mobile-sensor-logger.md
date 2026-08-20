# 1. Standalone Mobile Sensor Data Logger Architecture

Date: 2026-08-14

## Status
Cancelled by [0002-cpp-arcore-capture-app](0002-cpp-arcore-capture-app.md).

## Context
Initially, a web-based ROS 2 sensor collector (web-ros-collector) was considered to stream smartphone camera frames, IMU sensor data, and GPS telemetry over local HTTPS connections.
However, web browser environments impose severe background execution constraints (throttling or pausing browser threads when screen turns off), restricted IMU sampling rates, and network transmission overheads.
Furthermore, use cases requiring offline sensor data logging directly on mobile device local storage without requiring PC streaming were identified.

## Decision
We adopt a Standalone Mobile Sensor Data Logger architecture built as a dedicated mobile application that captures and stores sensor streams directly on smartphone local storage.

Key Decision Items:
1. Mobile Framework: Flutter (Dart)
2. Data Logging Strategy: Store sensor streams (CSV/JSON) and video/image files (JPEG/MP4) on mobile device filesystem without network dependencies.
3. Network Communication: Exclude live network streaming (HTTP/WebSocket) in initial releases to prioritize standalone offline logging.

## Consequences
Positive:
- Reliable high-frequency sensor collection in offline environments without network connectivity.
- Continuous background logging capability via mobile foreground services even during screen-off state.
- Elimination of network protocol overhead and complex certificate setup.

Negative:
- Real-time monitoring on PC host unavailable without explicit file export or streaming module.
- Local storage capacity management required on mobile device.
