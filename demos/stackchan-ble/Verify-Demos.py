"""Check real demo output without opening Bluetooth or moving the robot."""
import json
from pathlib import Path
import re
import subprocess

executable = Path(__file__).resolve().parents[2] / "build/stackchan-demos/Release/stackchan_demo.exe"

for demo in ("showcase", "timer", "both"):
    result = subprocess.run(
        [str(executable), "--demo", demo, "--seconds", "3", "--dry-run"],
        capture_output=True, text=True, timeout=5, check=True,
    )
    assert "GATT_OK" not in result.stdout
    assert "DRY_RUN_COMPLETE" in result.stdout
    last = {}
    for line in result.stdout.splitlines():
        if not line.startswith("DRY_RUN "):
            continue
        _, channel, payload = line.split(" ", 2)
        assert len(payload.encode("utf-8")) <= 512
        data = json.loads(payload)
        if channel == "avatar":
            assert set(data) == {"leftEye", "rightEye", "mouth"}
            for feature in data.values():
                assert type(feature["weight"]) is int
                assert 0 <= feature["weight"] <= 100
                assert abs(feature.get("rotation", 0)) <= 1800
        elif channel == "motion":
            assert set(data) == {"yawServo", "pitchServo"}
            assert -80 <= data["yawServo"]["angle"] <= 80
            assert 410 <= data["pitchServo"]["angle"] <= 490
            assert all("rotate" not in servo for servo in data.values())
            assert all(0 < servo["speed"] <= 1000 for servo in data.values())
        elif channel == "rgb":
            for side in ("left", "right"):
                assert re.fullmatch(r"#[0-9A-F]{6}", data[f"{side}RgbColor"])
                assert data[f"{side}RgbDuration"] >= 0
        else:
            raise AssertionError(f"Unexpected channel: {channel}")
        last[channel] = data
    assert last["motion"]["yawServo"]["angle"] == 0
    assert last["motion"]["pitchServo"]["angle"] == 450
    assert all(feature["rotation"] == 0 for feature in
               (last["avatar"]["leftEye"], last["avatar"]["rightEye"]))
    assert last["rgb"]["leftRgbColor"] == last["rgb"]["rightRgbColor"] == "#000000"
    print(f"PASS {demo}: JSON, movement limits, eye ranges, final pose and no hardware access")

for arguments in (
    ["--demo", "invalid"], ["--seconds", "-1"], ["--seconds", "3601"],
    ["--seconds", "3junk"], ["--address", "123"], ["--address", "ZZZZZZZZZZZZ"],
    ["--address", "000000000000"], ["--unknown"], ["--seconds"],
):
    result = subprocess.run(
        [str(executable), *arguments, "--dry-run"], capture_output=True, text=True, timeout=5,
    )
    assert result.returncode != 0, arguments
    assert "GATT_OK" not in result.stdout
print("PASS invalid arguments: rejected before Bluetooth access")
