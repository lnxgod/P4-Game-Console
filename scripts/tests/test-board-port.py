#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/board-port.py"


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["python3", str(SCRIPT), *arguments], cwd=ROOT, check=False,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main() -> None:
    check = run("check")
    assert check.returncode == 0, check.stderr
    assert json.loads(check.stdout)["result"] == "board-port-contract-valid"
    matrix = run("matrix")
    assert matrix.returncode == 0, matrix.stderr
    matrix_report = json.loads(matrix.stdout)
    assert matrix_report["result"] == "console-os-board-parity-valid"
    assert matrix_report["targets"]["olimex-esp32-p4-pc"]["touch"] is False
    assert matrix_report["targets"]["olimex-esp32-p4-pc"]["feature_count"] == 11
    assert matrix_report["targets"]["elecrow-crowpanel-advanced-10"][
        "exact_target_seen_on_hardware"] is True
    assert matrix_report["targets"]["elecrow-crowpanel-advanced-10"][
        "full_feature_set_hardware_tested"] is False
    assert matrix_report["targets"]["olimex-esp32-p4-pc"][
        "exact_target_seen_on_hardware"] is False

    with tempfile.TemporaryDirectory() as temporary:
        temp = pathlib.Path(temporary)
        evidence = ROOT / "hardware/evidence/olimex-esp32-p4-pc-rev-b-source-review.json"
        spec = {
            "schema": 1,
            "id": "example-p4-board-rev-a",
            "vendor": "Example",
            "product": "P4 Board",
            "pcb_revision": "A",
            "soc": "ESP32-P4NRW32",
            "memory": {
                "flash_bytes": 16 * 1024 * 1024,
                "psram_bytes": 32 * 1024 * 1024,
            },
            "official_source": {
                "repository": "https://example.invalid/p4-board",
                "commit": "1" * 40,
                "manual_sha256": "2" * 64,
                "schematic_sha256": "3" * 64,
            },
            "adapter_template": "olimex-esp32-p4-pc",
            "backend_compatibility": {
                "proven": True,
                "evidence": str(evidence.relative_to(ROOT)),
                "evidence_sha256": hashlib.sha256(evidence.read_bytes()).hexdigest(),
            },
            "interfaces": {"display": {"type": "HDMI"}},
            "inputs": ["gamepad", "keyboard", "mouse"],
            "touch": False,
            "laptop_content_transfer": "powered-off-removable-media",
        }
        spec_path = temp / "spec.json"
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        plan = run("plan", "--spec", str(spec_path))
        assert plan.returncode == 0, plan.stderr
        assert json.loads(plan.stdout)["build_ready"] is True
        scaffold = run(
            "scaffold", "--spec", str(spec_path),
            "--output-root", str(temp / "ports"))
        assert scaffold.returncode == 0, scaffold.stderr
        report = json.loads(scaffold.stdout)
        destination = pathlib.Path(report["path"])
        assert set(report["files"]) == {
            "PORTING.md", "adapter-plan.json", "board-profile.json",
            "sdkconfig.defaults"}
        profile = json.loads((destination / "board-profile.json").read_text())
        assert profile["flash_authorized"] is False
        assert profile["device_identity"] is None
        assert profile["exact_target_seen_on_hardware"] is False
        assert profile["full_feature_set_hardware_tested"] is False
        duplicate = run(
            "scaffold", "--spec", str(spec_path),
            "--output-root", str(temp / "ports"))
        assert duplicate.returncode != 0

        spec["backend_compatibility"]["evidence_sha256"] = "f" * 64
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        invalid = run("plan", "--spec", str(spec_path))
        assert invalid.returncode != 0

    print("board port tests passed")


if __name__ == "__main__":
    main()
