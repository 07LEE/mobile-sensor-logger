#!/usr/bin/env python3
"""Pack a session into a ROS1 bag for Kalibr camera-IMU calibration.

Usage: export_rosbag.py <session_dir> [--out FILE.bag]

Images go to /cam0/image_raw as upright mono8 (the luma plane, rotated the same
way export_images.py rotates). The accelerometer is interpolated onto each
gyroscope sample and the pairs go to /imu0 as sensor_msgs/Imu.

A session is refused, before any bag is written, when the camera and IMU
timestamps are not on one clock or when either IMU stream is missing.
"""

import argparse
import csv
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from rosbags.rosbag1 import Writer
from rosbags.typesys import Stores, get_typestore

from export_images import ROTATIONS, read_plane

# Marker the recorder writes into imu_note when the camera timestamp source is
# not REALTIME (session_recorder.cc); older sessions carry no separate field.
NOT_SAME_CLOCK = "NOT realtime"

store = get_typestore(Stores.ROS1_NOETIC)
Header = store.types["std_msgs/msg/Header"]
Time = store.types["builtin_interfaces/msg/Time"]
Image = store.types["sensor_msgs/msg/Image"]
Imu = store.types["sensor_msgs/msg/Imu"]
Vector3 = store.types["geometry_msgs/msg/Vector3"]
Quaternion = store.types["geometry_msgs/msg/Quaternion"]


def header(timestamp_ns, seq, frame_id):
    return Header(seq=seq, stamp=Time(sec=timestamp_ns // 10**9, nanosec=timestamp_ns % 10**9), frame_id=frame_id)


def paired_imu(path):
    """Return [(timestamp_ns, gyro, accel)] with accel interpolated onto each gyro sample.

    Exits with an explanation when the session cannot produce a Kalibr IMU stream.
    """
    gyro, accel = [], []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            sample = (int(row["timestamp_ns"]), float(row["x"]), float(row["y"]), float(row["z"]))
            (gyro if row["sensor"] == "gyro" else accel).append(sample)
    missing = [name for name, samples in (("gyroscope", gyro), ("accelerometer", accel)) if not samples]
    if missing:
        sys.exit(f"error: imu.csv has no {' or '.join(missing)} samples; Kalibr needs both streams")
    if len(accel) < 2:
        sys.exit("error: imu.csv has one accelerometer sample; at least two are needed to interpolate onto the gyroscope")
    accel = np.array(accel)
    pairs = []
    for t, *g in gyro:
        if t < accel[0, 0] or t > accel[-1, 0]:
            continue
        a = [np.interp(t, accel[:, 0], accel[:, axis]) for axis in (1, 2, 3)]
        pairs.append((t, g, a))
    if not pairs:
        sys.exit("error: no gyroscope sample falls inside the accelerometer time range, so nothing can be paired")
    return pairs


def require_shared_clock(manifest):
    """Exit when session.json says the frame and IMU timestamps are not on one clock."""
    note = manifest.get("imu_note") or ""
    if NOT_SAME_CLOCK in note:
        sys.exit(
            "error: this camera's timestamp source is not REALTIME, so frame and IMU timestamps are "
            "not on the same clock and the session cannot be used directly for Kalibr camera-IMU "
            f"calibration (imu_note: {note})"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--out", type=Path, help="default: <session>/<session name>.bag")
    args = parser.parse_args()

    manifest = json.loads((args.session / "session.json").read_text())
    rotation = ROTATIONS.get(int(manifest.get("sensor_orientation", 0)))
    require_shared_clock(manifest)
    pairs = paired_imu(args.session / "imu.csv")
    print(f"imu_note: {manifest.get('imu_note')}", file=sys.stderr)

    out = args.out or args.session / f"{args.session.name}.bag"
    if out.exists():
        out.unlink()

    with open(args.session / "frames.csv", newline="") as f:
        rows = list(csv.DictReader(f))

    zero3 = np.zeros(3)
    with Writer(out) as bag:
        image_conn = bag.add_connection("/cam0/image_raw", Image.__msgtype__, typestore=store)
        imu_conn = bag.add_connection("/imu0", Imu.__msgtype__, typestore=store)

        for seq, row in enumerate(rows):
            width, height = int(row["width"]), int(row["height"])
            data = (args.session / "frames" / row["filename"]).read_bytes()
            luma = read_plane(data, 0, int(row["segment0_length"]), height, int(row["luma_row_stride"]), width)
            if rotation is not None:
                luma = cv2.rotate(np.ascontiguousarray(luma), rotation)
            t = int(row["timestamp_ns"])
            msg = Image(
                header=header(t, seq, "cam0"),
                height=luma.shape[0],
                width=luma.shape[1],
                encoding="mono8",
                is_bigendian=0,
                step=luma.shape[1],
                data=np.ascontiguousarray(luma).reshape(-1),
            )
            bag.write(image_conn, t, store.serialize_ros1(msg, Image.__msgtype__))
            print(f"\rimages {seq + 1}/{len(rows)}", end="", file=sys.stderr)
        print(file=sys.stderr)

        for count, (t, g, a) in enumerate(pairs):
            msg = Imu(
                header=header(t, count, "imu0"),
                orientation=Quaternion(x=0.0, y=0.0, z=0.0, w=0.0),
                orientation_covariance=np.array([-1.0, 0, 0, 0, 0, 0, 0, 0, 0]),
                angular_velocity=Vector3(x=g[0], y=g[1], z=g[2]),
                angular_velocity_covariance=np.zeros(9),
                linear_acceleration=Vector3(x=float(a[0]), y=float(a[1]), z=float(a[2])),
                linear_acceleration_covariance=np.zeros(9),
            )
            bag.write(imu_conn, t, store.serialize_ros1(msg, Imu.__msgtype__))
    print(f"{len(rows)} images, {len(pairs)} imu samples in {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
