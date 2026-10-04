"""Host tests for scripts/export_colmap.py. Run with: python3 -m pytest tests"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "export_colmap.py"


def write_session(path, **fields):
    manifest = {
        "intrinsics": [3000.0, 3000.0, 2000.0, 1500.0, 0.0],
        "distortion": [0.0, 0.0, 0.0, 0.0, 0.0],
        "pre_correction_active_array": [0, 0, 4000, 3000],
        "sensor_orientation": 0,
    }
    manifest.update(fields)
    (path / "session.json").write_text(json.dumps(manifest))
    (path / "frames.csv").write_text("width,height\n4000,3000\n")


def stderr_of(session):
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(session)], check=True, capture_output=True, text=True
    ).stderr


def test_warns_when_session_does_not_record_distortion_correction(tmp_path):
    write_session(tmp_path)
    assert "does not record whether distortion correction was off" in stderr_of(tmp_path)


def test_warns_when_distortion_correction_could_not_be_turned_off(tmp_path):
    write_session(
        tmp_path,
        camera_distortion_correction_off_available=True,
        camera_distortion_correction_set_result=1,
    )
    assert "could not turn distortion correction off" in stderr_of(tmp_path)


@pytest.mark.parametrize("off_available, set_result", [(True, 0), (False, -1)])
def test_no_warning_when_geometry_matches_the_intrinsics(tmp_path, off_available, set_result):
    write_session(
        tmp_path,
        camera_distortion_correction_off_available=off_available,
        camera_distortion_correction_set_result=set_result,
    )
    assert "distortion correction" not in stderr_of(tmp_path)
