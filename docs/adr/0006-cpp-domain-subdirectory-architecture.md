# 6. C++ Domain Subdirectory Architecture

Date: 2026-08-22

## Status

Accepted.

## Context

As the C++ codebase expanded, all 25 header and source files resided flatly under `app/src/main/cpp/`. A flat directory layout obscured domain boundaries between UI rendering, camera capture, IMU sensors, storage I/O, processing pipeline, and input handling, making code navigation and maintenance difficult.

## Decision

Reorganize all native source files into domain-driven subdirectories under `app/src/main/cpp/`:
- `ui/`: OpenGL ES 3.0 rendering, HUD widgets, and dialog overlays.
- `camera/`: NDK Camera2 capture session management and image buffer wrappers.
- `sensors/`: NDK ASensorManager accelerometer and gyroscope event logging.
- `storage/`: File system I/O, directory scanning, and frame binary writers.
- `pipeline/`: Sharpness scoring, frame motion shift estimation, and candidate filters.
- `input/`: Android volume key and touch gesture input propagation.

Update `CMakeLists.txt` and IDE compiler properties to include the new domain directories.

## Consequences

- Establishes a clean Separation of Concerns (SoC) across C++ module boundaries.
- Improves project readability and maintainability for future feature development.
- Encapsulates domain dependencies cleanly within CMake target definitions.
