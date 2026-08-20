# Mobile Sensor Logger

> **Discontinued.** Flutter's camera plugin exposes no frame capture timestamp,
> so camera and IMU streams recorded here cannot be aligned to the precision the
> intended uses require. Capture moves to an existing synchronised logger, and
> this project's direction moves to converting captured sessions into rosbags and
> datasets. See
> [ADR 0002](docs/adr/0002-discontinue-standalone-flutter-logger.md) for the
> survey, the verified limitation, and the alternatives considered. The code
> below is kept as a record; the ROS-compatible schema it defines is still in use.

Mobile Sensor Logger is a standalone mobile application for logging smartphone camera frames, IMU sensor streams (accelerometer, gyroscope), GPS position telemetry, and battery state directly to device local storage.

## Core Features

- Camera Engine: Captures camera frames and records video or image files to local storage
- IMU Sensor Engine: High-rate (100Hz+) accelerometer and gyroscope logging synchronized with microsecond timestamps
- GPS and Battery Engine: Records geolocation latitude, longitude, altitude, and battery metrics
- Storage Manager: Manages per-session directories and logs sensor data to CSV/JSON files and video formats
- Session Export: Packs a recorded session into a ZIP and hands it to the system share sheet for transfer off the device

## Data Schema

Logged values follow the ROS conventions used by
[web-ros-collector](https://github.com/07LEE/web-ros-collector), so a session can be
replayed into the same topics this project was derived from. IMU vectors are stored
already converted to REP-103 axes (`ROS_X = phone_Y`, `ROS_Y = -phone_X`, `ROS_Z = phone_Z`)
in rad/s and m/s².

Each session is written to `sessions/session_YYYYMMDD_HHMMSS/`:

| File | ROS equivalent | Columns |
| --- | --- | --- |
| `imu.csv` | `sensor_msgs/Imu` (`phone_imu`) | `timestamp_us, ang_vel_x/y/z, lin_acc_x/y/z` |
| `gps.csv` | `sensor_msgs/NavSatFix` (`gps_link`) | `timestamp_us, latitude, longitude, altitude, horizontal_accuracy, vertical_accuracy, status` |
| `battery.csv` | `sensor_msgs/BatteryState` (`phone_link`) | `timestamp_us, percentage, power_supply_status` |
| `frames/frame_index.csv` | `sensor_msgs/CompressedImage` (`phone_camera`) | `timestamp_us, frame_seq, filename, format, exposure_time_us, iso` |
| `session.json` | — | Session metadata plus the unit/axis convention used |

## Setup & Getting Started

### 1. Flutter SDK Installation

Install the Flutter SDK via snap on Ubuntu Linux:

```bash
sudo snap install flutter --classic
flutter doctor
```

### 2. Project Initialization

Initialize the Flutter project template in this directory:

```bash
flutter create --org com.sensor.logger .
```

### 3. Dependencies Configuration

Add the required sensor packages in pubspec.yaml:

- camera: Camera image capture and video recording
- sensors_plus: IMU accelerometer and gyroscope data acquisition
- geolocator: GPS geolocation positioning
- path_provider: Local device filesystem path resolution
