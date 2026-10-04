#!/usr/bin/env python3
"""Convert the raw YUV frames of a session into upright PNG or JPEG files.

Usage: export_images.py <session_dir> [--out DIR] [--format png|jpg] [--limit N]
"""

import argparse
import csv
import json
import sys
from pathlib import Path

import cv2
import numpy as np

ROTATIONS = {
    90: cv2.ROTATE_90_CLOCKWISE,
    180: cv2.ROTATE_180,
    270: cv2.ROTATE_90_COUNTERCLOCKWISE,
}


def read_plane(data, offset, length, rows, row_stride, row_bytes):
    # The last row of a plane is not padded to the stride, so a segment can be
    # shorter than row_stride * rows.
    raw = np.zeros(rows * row_stride, dtype=np.uint8)
    chunk = np.frombuffer(data, dtype=np.uint8, count=length, offset=offset)
    raw[: len(chunk)] = chunk
    return raw.reshape(rows, row_stride)[:, :row_bytes]


def decode(data, row):
    width = int(row["width"])
    height = int(row["height"])
    layout = row["chroma_layout"]
    luma_stride = int(row["luma_row_stride"])
    chroma_stride = int(row["chroma_row_stride"])
    lengths = [int(row[f"segment{i}_length"]) for i in range(3)]

    y = read_plane(data, 0, lengths[0], height, luma_stride, width)
    chroma_rows = height // 2
    chroma_cols = width // 2

    if layout in ("semi_planar_uv", "semi_planar_vu"):
        chroma = read_plane(data, lengths[0], lengths[1], chroma_rows, chroma_stride, width)
        nv = np.vstack([y, chroma])
        code = cv2.COLOR_YUV2BGR_NV12 if layout == "semi_planar_uv" else cv2.COLOR_YUV2BGR_NV21
        return cv2.cvtColor(nv, code)

    if layout == "planar":
        # Planar chroma has a pixel stride of 1, so a row is chroma_cols bytes.
        u = read_plane(data, lengths[0], lengths[1], chroma_rows, chroma_stride, chroma_cols)
        v = read_plane(data, lengths[0] + lengths[1], lengths[2], chroma_rows, chroma_stride, chroma_cols)
        i420 = np.vstack([y, u.reshape(-1, width), v.reshape(-1, width)])
        return cv2.cvtColor(i420, cv2.COLOR_YUV2BGR_I420)

    raise ValueError(f"unknown chroma_layout: {layout}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--out", type=Path, help="default: <session>/images")
    parser.add_argument("--format", choices=("png", "jpg"), default="png")
    parser.add_argument("--limit", type=int, help="convert only the first N frames")
    args = parser.parse_args()

    manifest = json.loads((args.session / "session.json").read_text())
    rotation = ROTATIONS.get(int(manifest.get("sensor_orientation", 0)))
    out = args.out or args.session / "images"
    out.mkdir(parents=True, exist_ok=True)

    with open(args.session / "frames.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    if args.limit:
        rows = rows[: args.limit]

    for i, row in enumerate(rows, 1):
        data = (args.session / "frames" / row["filename"]).read_bytes()
        image = decode(data, row)
        if rotation is not None:
            image = cv2.rotate(image, rotation)
        stem = Path(row["filename"]).stem
        cv2.imwrite(str(out / f"{stem}.{args.format}"), image)
        print(f"\r{i}/{len(rows)}", end="", file=sys.stderr)
    print(f"\n{len(rows)} images in {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
