"""Host tests for scripts/export_camchain.py. Run with: python3 -m pytest tests"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "export_camchain.py"


def write_session(path, orientation=0, **fields):
    # Camera2 intrinsics for a 4000x3000 active array, principal point at the
    # geometric center (2000.0, 1500.0) in the pixel-corner convention.
    manifest = {
        "intrinsics": [3000.0, 3000.0, 2000.0, 1500.0, 0.0],
        "distortion": [0.0, 0.0, 0.0, 0.0, 0.0],
        "pre_correction_active_array": [0, 0, 4000, 3000],
        "sensor_orientation": orientation,
    }
    manifest.update(fields)
    (path / "session.json").write_text(json.dumps(manifest))
    (path / "frames.csv").write_text("width,height\n4000,3000\n")


def run(session):
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(session)], check=True, capture_output=True, text=True
    )


def intrinsics_of(session):
    out = session / "camchain.yaml"
    run(session)
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


# The warning comes from upright_intrinsics(), which export_colmap.py shares.
def test_warns_when_session_does_not_record_distortion_correction(tmp_path):
    write_session(tmp_path)
    assert "does not record whether distortion correction was off" in run(tmp_path).stderr


def test_warns_when_distortion_correction_could_not_be_turned_off(tmp_path):
    write_session(
        tmp_path,
        camera_distortion_correction_off_available=True,
        camera_distortion_correction_set_result=1,
    )
    assert "could not turn distortion correction off" in run(tmp_path).stderr


def test_warns_when_device_does_not_offer_distortion_correction_off(tmp_path):
    write_session(
        tmp_path,
        camera_distortion_correction_off_available=False,
        camera_distortion_correction_set_result=-1,
    )
    assert "does not offer distortion correction OFF" in run(tmp_path).stderr


def test_no_warning_when_distortion_correction_was_turned_off(tmp_path):
    write_session(
        tmp_path,
        camera_distortion_correction_off_available=True,
        camera_distortion_correction_set_result=0,
    )
    assert "distortion correction" not in run(tmp_path).stderr
