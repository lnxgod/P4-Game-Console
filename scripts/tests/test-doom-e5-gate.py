#!/usr/bin/env python3
"""Hermetic contract tests for the E5 verifier and app-only flash route."""

from __future__ import annotations

import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FLASH = ROOT / "scripts/flash.sh"
VERIFIER = ROOT / "scripts/verify-doom-embedded-touch-audio.py"
BUILD = ROOT / "apps/doom_embedded_touch_audio/build"
METADATA = ROOT / "apps/doom_embedded_touch_audio/app-metadata.json"
POST_RUN = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-persistent.json"
RUNTIME_RAW = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-runtime.raw"


def load_verifier():
    spec = importlib.util.spec_from_file_location("doom_e5_post_run_test", VERIFIER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(mode: str) -> None:
    subprocess.run(
        [sys.executable, str(VERIFIER), str(BUILD), mode],
        cwd=ROOT,
        check=True,
    )


def main() -> None:
    metadata = json.loads(METADATA.read_text(encoding="utf-8"))
    assert metadata["flash_app_authorized"] is True
    assert metadata["flash_project_authorized"] is False
    assert metadata["flash_authorized"] is False
    assert metadata["audio_runtime_authorized"] is False
    assert metadata["runtime_mode"]["required_exact_gates"] == {
        "composite": 1,
        "touch": 1,
        "audio": 0,
    }
    contract = metadata["execution_contract"]
    assert contract["shell_live_probe_count"] == 0
    assert contract["exclusive_uart_open_count"] == 1
    assert contract["exclusive_uart_reopen_count"] == 0
    assert contract["write_span_bytes"] == 4_898_816
    assert contract["flash_jedec_low24"] == "0x1840c8"
    assert contract["allowed_raw_flash_ids"] == ["0x001840c8", "0xff1840c8"]
    assert contract["canonical_flash_id"] == "0x1840c8"
    assert contract["application_launch_count"] == 1
    assert contract["soft_reset_count"] == 0
    assert contract["esptool_run_count"] == 0

    text = FLASH.read_text(encoding="utf-8")
    verifier_call = (
        'python3 "$P4_SCRIPT_DIR/verify-doom-embedded-touch-audio.py"'
    )
    assert text.count(verifier_call) == 2
    assert 'P4_E5_INSTALL_TOOL="$P4_SCRIPT_DIR/doom-e5-install.py"' in text
    assert 'P4_E5_EXPECTED_OFFSET=0x10000' in text
    assert 'P4_E5_EXPECTED_BYTES=4898400' in text
    assert (
        'P4_E5_EXPECTED_HASH='
        '68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8'
    ) in text
    assert 'python3 "$P4_E5_INSTALL_TOOL"' in text
    assert 'value.get("mutation_span_bytes") == 4898816' in text
    assert 'value.get("exclusive_transaction_loader_entry_reset_count") == 1' in text
    assert 'value.get("exclusive_transaction_application_launch_reset_count") == 1' in text
    assert 'value.get("application_launch_count") == 1' in text
    assert 'value.get("esptool_run_count") == 0' in text
    assert text.count('[ "$P4_APP" = doom_embedded_touch_audio ] ||') == 0
    assert '[ "$P4_APP" = gamepad_diag ] || \\\n   [ "$P4_APP" = doom_embedded_touch_audio ]' in text

    first_verify = text.index(verifier_call)
    seal = text.index('Exact E5 touch-only application snapshot sealed')
    prewrite_verify = text.index(verifier_call, first_verify + 1)
    helper = text.index('python3 "$P4_E5_INSTALL_TOOL"')
    result_check = text.index('exclusive E5 install result is not exact')
    branch_exit = text.index('trap - EXIT HUP INT TERM', result_check)
    generic_write = text.index('P4_FLASH_OUTPUT=$(esptool.py', branch_exit)
    assert first_verify < seal < prewrite_verify < helper < result_check < branch_exit < generic_write

    run("build-only")
    run("app-flash")
    run("post-run")

    # A semantic post-run claim drift is rejected by the exact immutable manifest binding.
    verifier = load_verifier()
    with tempfile.TemporaryDirectory(prefix="p4-e5-post-run-drift.") as temp:
        changed = pathlib.Path(temp) / POST_RUN.name
        record = json.loads(POST_RUN.read_text(encoding="utf-8"))
        record["claims_not_made"][1] = "touch was accepted"
        changed.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        changed.chmod(0o444)
        verifier.POST_RUN = changed
        try:
            verifier.verify_post_run()
        except SystemExit as error:
            assert "post-run manifest changed" in str(error)
        else:
            raise AssertionError("mutated post-run manifest passed")

    # Exact bytes are insufficient when an evidence file remains writable.
    verifier = load_verifier()
    with tempfile.TemporaryDirectory(prefix="p4-e5-runtime-mode.") as temp:
        writable = pathlib.Path(temp) / RUNTIME_RAW.name
        shutil.copyfile(RUNTIME_RAW, writable)
        writable.chmod(0o644)
        verifier.RUNTIME_RAW = writable
        try:
            verifier.verify_post_run()
        except SystemExit as error:
            assert "runtime raw mode is not 0444" in str(error)
        else:
            raise AssertionError("writable runtime raw passed")

    # A changed binary in an otherwise complete build directory must fail.
    with tempfile.TemporaryDirectory(prefix="p4-e5-mutated-build.") as temp:
        mutated = pathlib.Path(temp)
        shutil.copytree(BUILD, mutated, dirs_exist_ok=True)
        app = mutated / "p4_doom_embedded_touch_audio.bin"
        descriptor = os.open(app, os.O_RDWR)
        try:
            os.lseek(descriptor, 32, os.SEEK_SET)
            original = os.read(descriptor, 1)
            os.lseek(descriptor, 32, os.SEEK_SET)
            os.write(descriptor, bytes((original[0] ^ 0x01,)))
        finally:
            os.close(descriptor)
        result = subprocess.run(
            [sys.executable, str(VERIFIER), str(mutated), "build-only"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        assert result.returncode != 0
        assert "wrong app_binary hash" in (result.stdout + result.stderr)
    print("E5 verifier/flash-route tests: PASS")


if __name__ == "__main__":
    main()
