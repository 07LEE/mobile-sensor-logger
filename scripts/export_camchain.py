#!/usr/bin/env python3
"""Write a Kalibr camchain.yaml seeded with a session's intrinsics.

Usage: export_camchain.py <session_dir> [--out FILE.yaml]

The values match the upright images that export_rosbag.py writes. Kalibr's
pinhole-radtan model has no third radial term, so k3 is dropped.

Camera2 puts the center of pixel (x, y) at (x + 0.5, y + 0.5), as COLMAP does.
Kalibr follows OpenCV, where integer coordinates are pixel centers, so the
principal point is moved half a pixel on each axis.
"""

import argparse
import sys
from pathlib import Path

from export_colmap import upright_intrinsics


def kalibr_principal_point(cx, cy):
    """Convert a Camera2 principal point to Kalibr's pixel-center convention."""
    return cx - 0.5, cy - 0.5


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--out", type=Path, help="default: <session>/camchain.yaml")
    args = parser.parse_args()

    width, height, fx, fy, cx, cy, k1, k2, k3, p1, p2 = upright_intrinsics(args.session)
    cx, cy = kalibr_principal_point(cx, cy)
    if k3:
        print(f"warning: k3={k3:.8g} is dropped, Kalibr radtan has no slot for it", file=sys.stderr)

    out = args.out or args.session / "camchain.yaml"
    out.write_text(
        "cam0:\n"
        "  camera_model: pinhole\n"
        f"  intrinsics: [{fx:.8g}, {fy:.8g}, {cx:.8g}, {cy:.8g}]\n"
        "  distortion_model: radtan\n"
        f"  distortion_coeffs: [{k1:.8g}, {k2:.8g}, {p1:.8g}, {p2:.8g}]\n"
        f"  resolution: [{width}, {height}]\n"
        "  rostopic: /cam0/image_raw\n"
    )
    print(out, file=sys.stderr)


if __name__ == "__main__":
    main()
