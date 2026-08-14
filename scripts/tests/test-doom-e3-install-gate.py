#!/usr/bin/env python3

"""Host-only adversarial checks for the inactive E3 install gate."""

from __future__ import annotations

import copy
import importlib.util
import json
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_module(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


verifier = load_module(
    "verify_doom_e3", ROOT / "scripts/verify-doom-embedded-gamepad.py"
)
state_tool = load_module(
    "doom_e3_state", ROOT / "scripts/doom-e3-one-shot-state.py"
)


def expect_failure(function, *args) -> None:
    try:
        function(*args)
    except (SystemExit, state_tool.StateError):
        return
    raise AssertionError("expected fail-closed rejection")


metadata = json.loads(
    (ROOT / "apps/doom_embedded_gamepad/app-metadata.json").read_text()
)
authorization = json.loads(
    (
        ROOT
        / "hardware/evidence/doom-embedded-gamepad-e3-one-shot-authorization.json"
    ).read_text()
)

flash_text = (ROOT / "scripts/flash.sh").read_text()
arm_position = flash_text.index("P4_E3_RESERVATION_ACTIVE=true")
reserve_position = flash_text.index(
    'python3 "$P4_E3_STATE_TOOL" reserve', arm_position
)
assert arm_position < reserve_position

# The committed state is exactly inactive and cannot pass the app-flash gate.
verifier.verify_policy("build-only", metadata, authorization)
expect_failure(verifier.verify_policy, "app-flash", metadata, authorization)

# Only the four documented activation fields produce the accepted active state.
active_metadata = copy.deepcopy(metadata)
active_authorization = copy.deepcopy(authorization)
active_authorization.pop("consumed_at_utc", None)
active_authorization.pop("terminal_outcome", None)
active_authorization.pop("runtime_evidence", None)
active_authorization.pop("durable_ledger", None)
active_metadata["flash_app_authorized"] = True
active_metadata["one_shot_authorization_active"] = True
active_authorization["classification"] = verifier.ACTIVE_CLASS
active_authorization["active"] = True
active_authorization["consumed"] = False
verifier.verify_policy("app-flash", active_metadata, active_authorization)
expect_failure(verifier.verify_policy, "build-only", active_metadata, active_authorization)

for mutate in (
    lambda m, a: m.update(flash_project_authorized=True),
    lambda m, a: m["runtime_features"].update(native_usb_gamepad=True),
    lambda m, a: m.update(fixture_authorized=True),
    lambda m, a: a["scope"].update(usb_runtime=True),
    lambda m, a: a["scope"].update(full_project_flash=True),
    lambda m, a: a["exact_artifact"].update(bytes=4927823),
    lambda m, a: a["exact_artifact"].update(sha256="0" * 64),
    lambda m, a: a["execution_contract"].update(rebuild_after_verification=True),
    lambda m, a: a.update(consumed=True),
):
    bad_metadata = copy.deepcopy(active_metadata)
    bad_authorization = copy.deepcopy(active_authorization)
    mutate(bad_metadata, bad_authorization)
    expect_failure(verifier.verify_policy, "app-flash", bad_metadata, bad_authorization)

with tempfile.TemporaryDirectory(prefix="p4-e3-gate-test.") as directory:
    temporary = pathlib.Path(directory)
    auth_path = temporary / "authorization.json"
    auth_path.write_text(json.dumps(active_authorization))

    state_path = temporary / "state.json"
    state_tool.check_available(state_path, auth_path)
    state_tool.reserve(
        state_path,
        auth_path,
        state_tool.EXPECTED_DEVICE_SHA256,
        state_tool.EXPECTED_OFFSET,
        state_tool.EXPECTED_BYTES,
        state_tool.EXPECTED_SHA256,
    )
    expect_failure(
        state_tool.reserve,
        state_path,
        auth_path,
        state_tool.EXPECTED_DEVICE_SHA256,
        state_tool.EXPECTED_OFFSET,
        state_tool.EXPECTED_BYTES,
        state_tool.EXPECTED_SHA256,
    )
    state_tool.terminal_update(state_path, auth_path, "completed", "test-complete")
    completed = json.loads(state_path.read_text())
    assert completed["status"] == "completed" and completed["terminal"] is True
    expect_failure(state_tool.check_available, state_path, auth_path)

    failed_path = temporary / "failed.json"
    state_tool.reserve(
        failed_path,
        auth_path,
        state_tool.EXPECTED_DEVICE_SHA256,
        state_tool.EXPECTED_OFFSET,
        state_tool.EXPECTED_BYTES,
        state_tool.EXPECTED_SHA256,
    )
    state_tool.terminal_update(failed_path, auth_path, "failed", "test-failure")
    failed = json.loads(failed_path.read_text())
    assert failed["status"] == "failed" and failed["terminal"] is True

    wrong_path = temporary / "wrong.json"
    expect_failure(
        state_tool.reserve,
        wrong_path,
        auth_path,
        state_tool.EXPECTED_DEVICE_SHA256,
        state_tool.EXPECTED_OFFSET,
        state_tool.EXPECTED_BYTES,
        "f" * 64,
    )
    assert not wrong_path.exists()

print("E3 install gate adversarial tests PASS")
