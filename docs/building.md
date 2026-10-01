# Build and install

The app supports Android 8.0 (API 26) and later.

## Requirements

- Android SDK 35
- Android NDK `27.0.12077973`
- CMake
- JDK 17
- ADB for installing the APK on a connected device

## Build

Run from the repository root:

```bash
./gradlew assembleDebug
```

The debug APK is written to `app/build/outputs/apk/debug/app-debug.apk`.

## Install

Enable USB debugging on the device, connect it, approve the debugging prompt, and run:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```
