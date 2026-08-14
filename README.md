# Mobile Sensor Logger

Mobile Sensor Logger is a standalone mobile application for logging smartphone camera frames, IMU sensor streams (accelerometer, gyroscope), GPS position telemetry, and battery state directly to device local storage.

## Core Features

- Camera Engine: Captures camera frames and records video or image files to local storage
- IMU Sensor Engine: High-rate (100Hz+) accelerometer and gyroscope logging synchronized with microsecond timestamps
- GPS and Battery Engine: Records geolocation latitude, longitude, altitude, and battery metrics
- Storage Manager: Manages per-session directories and logs sensor data to CSV/JSON files and video formats

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
