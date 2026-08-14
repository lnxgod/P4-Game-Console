#!/usr/bin/env python3

"""Hermetic adversarial checks for D1 authorization/state validation."""

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
STATE_PATH = ROOT / "scripts/gamepad-diag-one-shot-state.py"


def load_state():
    spec = importlib.util.spec_from_file_location("gamepad_diag_state_contract", STATE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load gamepad diagnostic state helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


STATE = load_state()


def valid_fixture() -> dict:
    observed = "2026-08-13T18:00:00Z"
    return {
        "schema": 1,
        "id": "fixture-reviewed-rev-a",
        "classification": STATE.FIXTURE_CLASSIFICATION,
        "result": "pass",
        "device_identity_sha256": STATE.EXPECTED_DEVICE_SHA256,
        "fixture": {
            "manufacturer": "Example Lab",
            "model": "Current-Limited Direct J16 Harness",
            "revision": "A",
        },
        "qualification": {
            "j16_vbus_physically_open": True,
            "controller_side_regulated_5v": True,
            "current_limit_measured": True,
            "backfeed_blocked": True,
            "common_ground": True,
            "data_pair_direct": True,
            "source_role_compliant": True,
            "overcurrent_fault_visible": True,
            "exact_board_usb_path_reviewed": True,
            "current_limit_ma": 250,
        },
        "measurements": {
            "controller_vbus_voltage_mv": {
                "value": 5000, "unit": "mV", "instrument": "meter-a",
                "observed_at_utc": observed,
            },
            "current_limit_trip_ma": {
                "value": 250, "unit": "mA", "instrument": "load-a",
                "observed_at_utc": observed,
            },
            "j16_vbus_to_controller_vbus_ohms": {
                "value": 2_000_000, "unit": "ohm", "instrument": "meter-a",
                "observed_at_utc": observed,
            },
            "backfeed_to_j16_vbus_mv": {
                "value": 0, "unit": "mV", "instrument": "meter-a",
                "observed_at_utc": observed,
            },
            "backfeed_to_j1_programmer_vbus_mv": {
                "value": 0, "unit": "mV", "instrument": "meter-a",
                "observed_at_utc": observed,
            },
            "fault_observed": {
                "asserted": True,
                "controller_vbus_removed": True,
                "observed_at_utc": observed,
            },
        },
        "provenance": {
            "exact_board_path_review": "review/board-path-a",
            "schematic_sha256": "1" * 64,
            "fixture_schematic_sha256": "2" * 64,
            "wiring_photo_sha256": "3" * 64,
        },
        "review": {
            "reviewer": "reviewer-a",
            "reviewed_at_utc": observed,
            "approved": True,
        },
    }


class StateContractTests(unittest.TestCase):
    def test_owner_token_requires_exact_private_64_hex_plus_lf(self) -> None:
        local_state = ROOT / "hardware/local-state"
        local_state.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=local_state) as directory:
            path = pathlib.Path(directory) / "owner-token"
            path.write_bytes(b"a" * 64 + b"\n")
            path.chmod(0o600)
            self.assertEqual(STATE.read_owner_token(path), "a" * 64)
            for payload in (b"a" * 64, b"a" * 64 + b"\n\n", b"A" * 64 + b"\n"):
                path.write_bytes(payload)
                path.chmod(0o600)
                with self.subTest(bytes=len(payload)), self.assertRaises(STATE.StateError):
                    STATE.read_owner_token(path)
            path.write_bytes(b"a" * 64 + b"\n")
            path.chmod(0o640)
            with self.assertRaises(STATE.StateError):
                STATE.read_owner_token(path)

    def test_canonical_utc_rejects_impossible_or_noncanonical_dates(self) -> None:
        self.assertTrue(STATE.canonical_utc("2026-08-13T18:00:00Z"))
        for value in (
            "2026-02-30T18:00:00Z",
            "2026-8-13T18:00:00Z",
            "2026-08-13T18:00:00+00:00",
            "2026-08-13T18:00:00.000Z",
        ):
            with self.subTest(value=value):
                self.assertFalse(STATE.canonical_utc(value))

    def test_physical_fixture_requires_exact_schema_and_measurements(self) -> None:
        fixture = valid_fixture()
        self.assertTrue(STATE.qualification_valid(fixture["qualification"]))
        self.assertTrue(STATE.physical_evidence_complete(fixture))

        mutations = []
        changed = valid_fixture()
        changed["measurements"]["current_limit_trip_ma"]["value"] = 251
        mutations.append(("published-limit-not-measured", changed))
        changed = valid_fixture()
        del changed["measurements"]["backfeed_to_j1_programmer_vbus_mv"]
        mutations.append(("programmer-backfeed-missing", changed))
        changed = valid_fixture()
        changed["measurements"]["backfeed_to_j16_vbus_mv"]["observed_at_utc"] = (
            "2026-02-30T18:00:00Z"
        )
        mutations.append(("impossible-timestamp", changed))
        changed = valid_fixture()
        changed["measurements"]["controller_vbus_voltage_mv"]["extra"] = True
        mutations.append(("self-asserted-extra-measurement-field", changed))
        changed = valid_fixture()
        changed["qualification"]["unreviewed"] = True
        mutations.append(("qualification-expanded", changed))
        for name, changed in mutations:
            with self.subTest(name=name):
                self.assertFalse(
                    STATE.qualification_valid(changed["qualification"])
                    and STATE.physical_evidence_complete(changed)
                )

    def test_compiled_fixture_contract_is_exact_and_hashable(self) -> None:
        fixture = valid_fixture()
        compiled = STATE.expected_compiled_fixture(
            fixture["id"], hashlib.sha256(b"fixture").hexdigest(),
            fixture["qualification"],
        )
        self.assertEqual(compiled["current_limit_ma"], 250)
        self.assertEqual(set(compiled), {
            "fixture_authorized", "fixture_evidence_id", "fixture_evidence_sha256",
            "current_limit_ma", "external_vbus", "current_limited",
            "backfeed_blocked", "common_ground", "data_pair_direct",
            "source_role_compliant", "fault_visible", "board_path_reviewed",
        })
        self.assertTrue(all(
            value is True for key, value in compiled.items()
            if key not in {"fixture_evidence_id", "fixture_evidence_sha256", "current_limit_ma"}
        ))

    def test_new_reservation_path_is_canonical(self) -> None:
        expected = ROOT / STATE.STATE_REL
        self.assertEqual(STATE.exact_new_state_path(expected), expected)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(STATE.StateError):
                STATE.exact_new_state_path(pathlib.Path(directory) / expected.name)


if __name__ == "__main__":
    unittest.main()
