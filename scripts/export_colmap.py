#!/usr/bin/env python3
"""Print the COLMAP camera model and parameters for images from export_images.py.

Usage: export_colmap.py <session_dir>

The intrinsics in session.json describe the frame as the sensor reads it. The
exported images are rotated upright, so the values are rotated to match.
"""

import argparse
import csv
import json
import sys
from pathlib import Path


def rotate(fx, fy, cx, cy, p1, p2, width, height, degrees):
    # Rotating the image clockwise by 90 degrees swaps the axes, and the
    # tangential terms swap with it.
    if degrees == 0:
        return fx, fy, cx, cy, p1, p2, width, height
    if degrees == 90:
        return fy, fx, height - cy, cx, p2, -p1, height, width
    if degrees == 180:
        return fx, fy, width - cx, height - cy, -p1, -p2, width, height
    if degrees == 270:
        return fy, fx, cy, width - cx, -p2, p1, height, width
    raise ValueError(f"unsupported sensor_orientation: {degrees}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    args = parser.parse_args()

    manifest = json.loads((args.session / "session.json").read_text())
    if not manifest.get("intrinsics"):
        sys.exit("session.json has no intrinsics; calibrate this camera first")

    with open(args.session / "frames.csv", newline="") as f:
        first = next(csv.DictReader(f))
    width, height = int(first["width"]), int(first["height"])

    fx, fy, cx, cy, _skew = manifest["intrinsics"]
    k1, k2, k3, p1, p2 = manifest["distortion"]
    _, _, array_w, array_h = manifest["pre_correction_active_array"]

    # The values are in pixels of the pre-correction active array, which
    # matches the frame only when the two differ by a uniform scale.
    scale = width / array_w
    if abs(scale - height / array_h) > 1e-3:
        sys.exit(
            f"frame {width}x{height} is not a uniform scale of the active array "
            f"{array_w}x{array_h}; the intrinsics cannot be reused as they are"
        )
    fx, fy, cx, cy = (v * scale for v in (fx, fy, cx, cy))

    fx, fy, cx, cy, p1, p2, width, height = rotate(
        fx, fy, cx, cy, p1, p2, width, height, int(manifest.get("sensor_orientation", 0))
    )

    # FULL_OPENCV is fx, fy, cx, cy, k1, k2, p1, p2, k3, k4, k5, k6.
    params = [fx, fy, cx, cy, k1, k2, p1, p2, k3, 0, 0, 0]
    print(f"image size: {width}x{height}")
    print("camera_model: FULL_OPENCV")
    print("camera_params: " + ",".join(f"{v:.8g}" for v in params))
    print()
    print(
        "colmap feature_extractor --database_path database.db "
        f"--image_path {args.session}/images "
        "--ImageReader.single_camera 1 --ImageReader.camera_model FULL_OPENCV "
        f'--ImageReader.camera_params "{",".join(f"{v:.8g}" for v in params)}"'
    )


if __name__ == "__main__":
    main()
