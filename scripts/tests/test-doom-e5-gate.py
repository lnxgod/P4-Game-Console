#!/usr/bin/env python3
"""Host-only tests of the closed generic gate and historical E5 evidence."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FLASH = ROOT / "scripts/flash.sh"
VERIFIER = ROOT / "scripts/verify-doom-embedded-touch-audio.py"
METADATA = ROOT / "apps/doom_embedded_touch_audio/app-metadata.json"
POST_RUN = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-persistent.json"
RUNTIME_RAW = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-runtime.raw"


def load_verifier():
    spec = importlib.util.spec_from_file_location("doom_e5_post_run_test", VERIFIER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sealed_history(directory: pathlib.Path):
    verifier = load_verifier()
    # Git does not preserve 0444. Seal byte-identical temporary fixtures, not
    # repository records; verify_post_run still enforces every pinned digest.
    for name in ("POST_RUN", "RUNTIME_RAW", "RUNTIME_SUMMARY",
                 "EXECUTED_EVIDENCE", "EXECUTED_AUTH"):
        original = getattr(verifier, name)
        target = directory / original.name
        shutil.copyfile(original, target)
        target.chmod(0o444)
        setattr(verifier, name, target)
    return verifier


def expect_failure(function, message: str, *args) -> None:
    try:
        function(*args)
    except SystemExit as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError(f"expected rejection: {message}")


def main() -> None:
    metadata = json.loads(METADATA.read_text(encoding="utf-8"))
    # The working app is now an E6 successor. Historical E5 authorization
    # cannot authorize its different source, runtime gates or build output.
    assert metadata["stage"].startswith("E6-")
    for key in ("flash_app_authorized", "flash_project_authorized", "flash_authorized",
                "top_level_runtime_authorized", "audio_runtime_authorized"):
        assert metadata[key] is False
    assert metadata["execution_contract"]["build_only"] is True
    for key in ("app_flash_allowed", "project_flash_allowed", "hardware_access_allowed",
                "runtime_capture_allowed"):
        assert metadata["execution_contract"][key] is False

    # Invoke only the metadata policy helper. No flash script, port selection,
    # SDK activation, installer or serial module is executed by this test.
    for mode, key in (("app-flash", "flash_app_authorized"),
                      ("flash", "flash_project_authorized")):
        result = subprocess.run(
            ["sh", "-c", 'P4_SCRIPT_DIR=$1; . "$1/lib/project-env.sh"; '
             'p4_require_flash_authorized doom_embedded_touch_audio "$2"',
             "e5-policy-test", str(ROOT / "scripts"), mode],
            cwd=ROOT, capture_output=True, text=True,
        )
        assert result.returncode != 0
        assert f"{key} is false; this is a build-only app" in result.stderr

    verifier = load_verifier()
    for mode in ("build-only", "app-flash"):
        expect_failure(verifier.verify_policy, "wrong stage", mode, {})
    predecessor = metadata["installed_predecessor"]
    assert predecessor["mode"] == "E5-touch-only-1/1/0"
    assert predecessor["artifact_sha256"] == \
        "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8"
    for key, path, digest in (
        ("post_run", verifier.POST_RUN, verifier.EXPECTED_POST_RUN_SHA256),
        ("executed_preflash_authorization", verifier.EXECUTED_AUTH,
         verifier.EXPECTED_EXECUTED_AUTH_SHA256),
        ("executed_build_evidence", verifier.EXECUTED_EVIDENCE,
         verifier.EXPECTED_EXECUTED_EVIDENCE_SHA256),
    ):
        assert predecessor[key] == {"path": str(path.relative_to(ROOT)), "sha256": digest}
        assert verifier.sha256(path) == digest
    historical = json.loads(verifier.EXECUTED_AUTH.read_text(encoding="utf-8"))
    assert historical["gates"] == {"composite": 1, "touch": 1, "audio": 0}
    contract = historical["execution_contract"]
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
    policy = text.index('p4_require_flash_authorized "$P4_APP" "$P4_FLASH_TARGET"')
    assert policy < text.index('p4_require_port "$P4_PORT"')
    assert policy < first_verify < seal < prewrite_verify < helper < result_check < branch_exit < generic_write

    with tempfile.TemporaryDirectory(prefix="p4-e5-history.") as temp:
        directory = pathlib.Path(temp)
        verifier = sealed_history(directory)
        verifier.verify_post_run()

        # Claim drift is rejected without rewriting a historical source record.
        changed = directory / "mutated-post-run.json"
        record = json.loads(POST_RUN.read_text(encoding="utf-8"))
        record["claims_not_made"][1] = "touch was accepted"
        changed.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        changed.chmod(0o444)
        original = verifier.POST_RUN
        verifier.POST_RUN = changed
        expect_failure(verifier.verify_post_run, "post-run manifest changed")
        verifier.POST_RUN = original

        # Exact bytes do not bypass the verifier's read-only evidence contract.
        verifier.RUNTIME_RAW.chmod(0o644)
        expect_failure(verifier.verify_post_run, "runtime raw mode is not 0444")
        verifier.RUNTIME_RAW.chmod(0o444)

        # The build directory now belongs to E6. Test E5's exact-file primitive
        # with a small synthetic artifact rather than requalifying that build.
        artifact = directory / "synthetic-app.bin"
        payload = bytes(range(128))
        artifact.write_bytes(payload)
        binding = {"app_binary_bytes": len(payload),
                   "app_binary_sha256": hashlib.sha256(payload).hexdigest()}
        verifier.exact_file(artifact, binding, "app_binary")
        changed_payload = bytearray(payload)
        changed_payload[32] ^= 1
        artifact.write_bytes(changed_payload)
        expect_failure(verifier.exact_file, "wrong app_binary hash",
                       artifact, binding, "app_binary")
    print("E5 historical evidence/current closed flash gate tests: PASS")


if __name__ == "__main__":
    main()
