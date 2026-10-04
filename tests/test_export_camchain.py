"""Host tests for scripts/export_camchain.py. Run with: python3 -m pytest tests"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "export_camchain.py"


def write_session(path, orientation):
    # Camera2 intrinsics for a 4000x3000 active array, principal point at the
    # geometric center (2000.0, 1500.0) in the pixel-corner convention.
    (path / "session.json").write_text(
        json.dumps(
            {
                "intrinsics": [3000.0, 3000.0, 2000.0, 1500.0, 0.0],
                "distortion": [0.0, 0.0, 0.0, 0.0, 0.0],
                "pre_correction_active_array": [0, 0, 4000, 3000],
                "sensor_orientation": orientation,
            }
        )
    )
    (path / "frames.csv").write_text("width,height\n4000,3000\n")


def intrinsics_of(session):
    out = session / "camchain.yaml"
    subprocess.run([sys.executable, str(SCRIPT), str(session)], check=True, capture_output=True)
    line = next(l for l in out.read_text().splitlines() if "intrinsics:" in l)
    return [float(v) for v in line.split("[")[1].rstrip("]").split(",")]


@pytest.mark.parametrize(
    "orientation, expected_cx, expected_cy",
    [
        (0, 1999.5, 1499.5),
        (90, 1499.5, 1999.5),
        (180, 1999.5, 1499.5),
        (270, 1499.5, 1999.5),
    ],
)
def test_principal_point_moves_half_a_pixel_to_kalibr_convention(tmp_path, orientation, expected_cx, expected_cy):
    write_session(tmp_path, orientation)
    _, _, cx, cy = intrinsics_of(tmp_path)
    assert (cx, cy) == (expected_cx, expected_cy)
