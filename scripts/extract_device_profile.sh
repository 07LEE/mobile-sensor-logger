#!/usr/bin/env bash
#
# extract_device_profile.sh
# Self-diagnostic tool to inspect camera characteristics and sensor parameters of an ADB-connected Android device.
# Usage: ./scripts/extract_device_profile.sh [output_directory]


set -euo pipefail

echo "Checking ADB connection..."
if ! command -v adb &> /dev/null; then
    echo "Error: adb command not found. Please ensure Android SDK platform-tools are in your PATH." >&2
    exit 1
fi

DEVICE_COUNT=$(adb devices 2>/dev/null | grep -v "^List" | grep -c -w "device" || true)
if [ "${DEVICE_COUNT}" -eq 0 ]; then
    echo "Error: No ADB device connected or authorized. Please connect an Android device with USB debugging enabled." >&2
    exit 1
fi

MODEL=$(adb shell getprop ro.product.model | tr -d '\r' | tr ' ' '_')
BRAND=$(adb shell getprop ro.product.brand | tr -d '\r' | tr ' ' '_')
DEVICE_NAME=$(adb shell getprop ro.product.device | tr -d '\r' | tr ' ' '_')
ANDROID_VER=$(adb shell getprop ro.build.version.release | tr -d '\r')
SDK_VER=$(adb shell getprop ro.build.version.sdk | tr -d '\r')

DEFAULT_DIR="data/device_reports/${BRAND}_${MODEL}"
OUT_DIR="${1:-${DEFAULT_DIR}}"
mkdir -p "${OUT_DIR}"

RAW_LOG="${OUT_DIR}/raw_dumpsys.txt"
SUMMARY_MD="${OUT_DIR}/summary.md"

echo "Collecting dumpsys camera information..."
if ! adb shell dumpsys media.camera > "${RAW_LOG}" 2>/dev/null; then
    adb shell dumpsys camera > "${RAW_LOG}"
fi

echo "Parsing hardware profile..."

# Multi-line dumpsys key extraction helper
get_dumpsys_val() {
    local key="$1"
    awk -v k="$key" '
        $0 ~ k {
            getline;
            gsub(/^[ \t]+\[|\][ \t]*$/, "");
            print;
            exit;
        }
    ' "${RAW_LOG}"
}

PIXEL_ARRAY=$(get_dumpsys_val "android.sensor.info.pixelArraySize")
PIXEL_SIZE="N/A"
if [ -n "${PIXEL_ARRAY}" ]; then
    PIXEL_SIZE=$(echo "${PIXEL_ARRAY}" | awk '{print $1"x"$2}')
fi

INTRINSICS_RAW=$(get_dumpsys_val "android.lens.intrinsicCalibration")
INTRINSICS="N/A"
if [ -n "${INTRINSICS_RAW}" ]; then
    INTRINSICS=$(echo "${INTRINSICS_RAW}" | awk '{printf "%.0f, %.0f", $1, $2}')
fi

DISTORTION_RAW=$(get_dumpsys_val "android.lens.distortion")
DISTORTION_K1="N/A"
if [ -n "${DISTORTION_RAW}" ]; then
    DISTORTION_K1=$(echo "${DISTORTION_RAW}" | awk '{printf "%+.4f", $1}')
fi

SKEW_RAW=$(get_dumpsys_val "android.sensor.rollingShutterSkew")
SKEW="N/A"
if [ -n "${SKEW_RAW}" ]; then
    SKEW=$(echo "${SKEW_RAW}" | awk '{printf "%.1fms", $1 / 1000000}')
fi

FOCAL_RAW=$(get_dumpsys_val "android.lens.info.availableFocalLengths")
FOCAL_LENGTH="N/A"
if [ -n "${FOCAL_RAW}" ]; then
    FOCAL_LENGTH=$(echo "${FOCAL_RAW}" | awk '{printf "%.1fmm", $1}')
fi

TIMESTAMP_SRC="UNKNOWN"
if grep -q "android.sensor.info.timestampSource = 1" "${RAW_LOG}" 2>/dev/null || grep -q "REALTIME" "${RAW_LOG}" 2>/dev/null; then
    TIMESTAMP_SRC="REALTIME"
fi

cat << EOF > "${SUMMARY_MD}"
### Device Profile: ${BRAND} ${MODEL} (${DEVICE_NAME})

| Metric | Value |
| --- | --- |
| Device Model | ${BRAND} ${MODEL} (${DEVICE_NAME}) |
| Android Version | Android ${ANDROID_VER} (SDK ${SDK_VER}) |
| Capture size | ${PIXEL_SIZE} |
| Focal Length | ${FOCAL_LENGTH} |
| Intrinsics fx, fy | ${INTRINSICS} |
| Distortion k1 | ${DISTORTION_K1} |
| Rolling shutter skew | ${SKEW} |
| Timestamp source | ${TIMESTAMP_SRC} |

> Raw dumpsys output saved to \`${RAW_LOG}\`.
EOF

echo "Device extraction complete!"
echo "Raw dumpsys: ${RAW_LOG}"
echo "Summary markdown: ${SUMMARY_MD}"
