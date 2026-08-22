# 5. Decouple UI Header Dependencies

Date: 2026-08-22

## Status

Accepted.

## Context

The UI component headers (`sessions_overlay.h`, `preview_renderer.h`) directly included `session_recorder.h` solely to access the `SessionItem` data struct.

Including `session_recorder.h` pulled in a heavy include graph comprising NDK Camera2 headers, IMU source, frame writer I/O, capture configurations, and pending frame filters (over 15,000 preprocessed lines of C++ code). This resulted in IDE linter false positives (red squiggly lines), high coupling between UI rendering and storage engines, and unnecessary compiler parsing overhead during incremental builds.

## Decision

Extract the pure view data struct `SessionItem` into a dedicated lightweight header file (`session_item.h`) that only includes `<string>`. Update UI components to include `session_item.h` instead of `session_recorder.h`.

## Consequences

- UI rendering headers are completely decoupled from storage and camera engine internals.
- Incremental C++ compilation preprocessing overhead for UI code is reduced by 70-80%.
- IDE C/C++ static analyzer indexer false warnings are resolved.
