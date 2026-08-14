#!/usr/bin/env python3

"""Hermetic adversarial tests for the inactive D1 gamepad diagnostic gate."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {relative}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CAPTURE = load("gamepad_diag_capture_test", "scripts/gamepad-diag-capture.py")
STATE = load("gamepad_diag_state_test", "scripts/gamepad-diag-one-shot-state.py")
FIXTURE_ID = "fixture-test"
FIXTURE_SHA = "1" * 64
ARM_SHA = "2" * 64
EXPECTED = {
    "fixture_id": FIXTURE_ID,
    "fixture_sha256": FIXTURE_SHA,
    "current_limit_ma": 250,
    "arm_token_sha256": ARM_SHA,
}


def line(message: str) -> str:
    return f"I (1234) gamepad_diag: {message}"


def valid_lines() -> list[str]:
    return [
        line("GAMEPAD_D1_SERIAL_ATTACH wait_ms=2500 complete=1 root_data_port_enabled=0"),
        line("GAMEPAD_D1_BOOT usb_component=1.5.0 hid_component=1.2.0 controller=p4-hs serial_attach_delay_ms=2500 root_data_port_enabled=0"),
        line(f"GAMEPAD_D1_WAIT_ARM auth={CAPTURE.AUTH_ID} timeout_ms=15000 root_data_port_enabled=0"),
        line(f"GAMEPAD_D1_ARM_ACCEPTED auth={CAPTURE.AUTH_ID} token_sha256={ARM_SHA} tx_count=1 root_data_port_enabled=0"),
        line(f"USB_HOST_READY controller=p4-hs peripheral=0 root_port_enabled=0 fixture={FIXTURE_ID} limit_ma=250"),
        line("GAMEPAD_USB_READY tier=generic-hid report_max=1024"),
        line("USB_HOST_ROOT_PORT_ENABLED class_leases_present=1"),
        line(f"GAMEPAD_D1_READY tier=1-generic-hid fixture={FIXTURE_ID} evidence_sha256={FIXTURE_SHA} external_vbus_fixture_owned=1 root_data_port_enabled=1 window_ms=120000"),
        line(f"GAMEPAD_CONNECTED session=1 vid=0079 pid=0011 interface=0 protocol=0 descriptor_bytes=101 descriptor_sha256={CAPTURE.DESCRIPTOR_SHA256} profile=usb-gamepad-0079-0011 capabilities=0x00000003"),
        line(f"GAMEPAD_D1_CONNECTED session=1 vid=0079 pid=0011 interface=0 transport=usb-hid descriptor_sha256={CAPTURE.DESCRIPTOR_SHA256} profile=usb-gamepad-0079-0011 capabilities=0x00000003 reconnect_after_disconnect=0"),
        line("GAMEPAD_D1_STATE session=1 sequence=5 connected=1 buttons=0000000000000001 dpad=0 lx=0 ly=0 rx=0 ry=0 lt=0 rt=0"),
        line("GAMEPAD_D1_ACTIVE_INPUT session=1 sequence=5 buttons=0000000000000001 dpad=0"),
        line("USB_HOST_QUIESCING root_port_enabled=0"),
        line("GAMEPAD_DISCONNECTED session=1 reason=shutdown neutral=1"),
        line("USB_HOST_STOPPED"),
        line("GAMEPAD_D1_CLEANUP quiesce=ESP_OK gamepad_stop=ESP_OK gamepad_stop_attempted=1 final_snapshot=ESP_OK final_neutral=1 host_stop=ESP_OK host_stop_attempted=1 resources_retained=0"),
        line("GAMEPAD_D1_DISCONNECTED session=1 sequence=6 source=controlled-cleanup"),
        line("GAMEPAD_D1_NEUTRAL session=1 source=controlled-cleanup neutral=1 held_input_before=1"),
        line("GAMEPAD_D1_STATS interfaces_seen=1 interfaces_rejected=0 connections=1 disconnections=1 reports_committed=4 reports_dropped=0 malformed_reports=0 callback_faults=0"),
        line("GAMEPAD_D1_RESULT result=PASS reason=none exact_identity_profile=1 report_seen=1 active_input_seen=1 initial_enumeration=1 physical_disconnect=0 disconnect_neutral=0 held_disconnect_neutral=0 reconnect=0 cleanup_neutral=1 cleanup_complete=1"),
        line("GAMEPAD_D1_TERMINAL state=halted root_data_port_enabled=0 resources_retained=0 automatic_retry=0"),
    ]


def transcript(lines: list[str]) -> bytes:
    return ("\n".join(lines) + "\n").encode("ascii")


def line_index(lines: list[str], *tokens: str) -> int:
    return next(
        index
        for index, value in enumerate(lines)
        if all(token in value for token in tokens)
    )


def valid_reconnect_lines() -> list[str]:
    lines = valid_lines()
    events = [
        line("GAMEPAD_DISCONNECTED session=1 reason=removed neutral=1"),
        line("GAMEPAD_D1_DISCONNECTED session=1 sequence=6 source=device-event"),
        line("GAMEPAD_D1_NEUTRAL session=1 source=device-event neutral=1 held_input_before=1"),
        line(f"GAMEPAD_CONNECTED session=2 vid=0079 pid=0011 interface=0 protocol=0 descriptor_bytes=101 descriptor_sha256={CAPTURE.DESCRIPTOR_SHA256} profile=usb-gamepad-0079-0011 capabilities=0x00000003"),
        line(f"GAMEPAD_D1_CONNECTED session=2 vid=0079 pid=0011 interface=0 transport=usb-hid descriptor_sha256={CAPTURE.DESCRIPTOR_SHA256} profile=usb-gamepad-0079-0011 capabilities=0x00000003 reconnect_after_disconnect=1"),
        line("GAMEPAD_D1_STATE session=2 sequence=9 connected=1 buttons=0000000000000000 dpad=0 lx=0 ly=0 rx=0 ry=0 lt=0 rt=0"),
    ]
    lines[12:12] = events
    cleanup_disconnect = next(
        index for index, value in enumerate(lines)
        if "source=controlled-cleanup" in value and "DISCONNECTED" in value
    )
    cleanup_neutral = next(
        index for index, value in enumerate(lines)
        if "source=controlled-cleanup" in value and "NEUTRAL" in value
    )
    stats = next(index for index, value in enumerate(lines) if "GAMEPAD_D1_STATS" in value)
    result = next(index for index, value in enumerate(lines) if "GAMEPAD_D1_RESULT" in value)
    platform_shutdown = next(
        index for index, value in enumerate(lines)
        if "GAMEPAD_DISCONNECTED" in value and "reason=shutdown" in value
    )
    lines[platform_shutdown] = lines[platform_shutdown].replace(
        "session=1", "session=2"
    )
    lines[cleanup_disconnect] = lines[cleanup_disconnect].replace(
        "session=1 sequence=6", "session=2 sequence=12"
    )
    lines[cleanup_neutral] = lines[cleanup_neutral].replace("session=1", "session=2").replace(
        "held_input_before=1", "held_input_before=0"
    )
    lines[stats] = lines[stats].replace("interfaces_seen=1", "interfaces_seen=2").replace(
        "connections=1 disconnections=1 reports_committed=4",
        "connections=2 disconnections=2 reports_committed=8",
    )
    lines[result] = lines[result].replace(
        "physical_disconnect=0 disconnect_neutral=0 held_disconnect_neutral=0 reconnect=0",
        "physical_disconnect=1 disconnect_neutral=1 held_disconnect_neutral=1 reconnect=1",
    )
    return lines


class TranscriptContractTests(unittest.TestCase):
    def assert_passes(self, lines: list[str]) -> None:
        self.assertEqual(CAPTURE.summarize(transcript(lines), EXPECTED)["result"], "pass")

    def assert_fails(self, lines: list[str]) -> None:
        self.assertEqual(CAPTURE.summarize(transcript(lines), EXPECTED)["result"], "fail")

    def test_exact_smoke_transcript_passes(self) -> None:
        self.assert_passes(valid_lines())

    def test_ready_transport_race_both_orders_pass(self) -> None:
        lines = valid_lines()
        ready = lines.pop(7)
        lines.insert(8, ready)
        self.assert_passes(lines)

    def test_trailing_leading_concatenated_and_ansi_fail(self) -> None:
        for mutation in (
            lambda value: value + " trailing=1",
            lambda value: "junk " + value,
            lambda value: value + value,
            lambda value: "\x1b[31m" + value,
        ):
            lines = valid_lines()
            lines[3] = mutation(lines[3])
            with self.subTest(line=lines[3]):
                self.assert_fails(lines)

    def test_extra_unknown_or_malformed_protected_line_fails(self) -> None:
        extras = (
            "GAMEPAD_D1_STATE session=1 sequence=8 connected=1 "
            "buttons=0000000000000000 dpad=0 lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 trailing=1",
            f"GAMEPAD_D1_CONNECTED session=2 vid=0079 pid=0011 interface=0 "
            f"transport=usb-hid descriptor_sha256={CAPTURE.DESCRIPTOR_SHA256} "
            "profile=usb-gamepad-0079-0011 capabilities=0x00000003 "
            "reconnect_after_disconnect=0 trailing=1",
            "GAMEPAD_D1_RESULT result=PASS reason=none bogus=1",
            "GAMEPAD_D1_FOOBAR result=PASS",
            "USB_HOST_ROOT_PORT_ENABLED class_leases_present=1 trailing=1",
        )
        for extra in extras:
            lines = valid_lines()
            lines.insert(12, line(extra))
            with self.subTest(extra=extra):
                self.assert_fails(lines)

    def test_unprotected_fatal_marker_fails(self) -> None:
        lines = valid_lines()
        lines.insert(12, "Guru Meditation Error: Core 0 panic'ed")
        self.assert_fails(lines)

    def test_final_protected_event_requires_terminating_lf(self) -> None:
        payload = transcript(valid_lines()).rstrip(b"\n")
        self.assertEqual(CAPTURE.summarize(payload, EXPECTED)["result"], "fail")

    def test_exact_optional_progress_and_cleanup_transport_lines_pass(self) -> None:
        lines = valid_lines()
        lines.insert(12, line(
            "GAMEPAD_D1_PROGRESS elapsed_ms=10000 connected=1 exact=1 "
            "reports=4 disconnect=0 reconnect=0"
        ))
        self.assert_passes(lines)

    def test_hotplug_progress_without_unbound_window_claim_passes(self) -> None:
        lines = valid_reconnect_lines()
        quiesce = line_index(lines, "USB_HOST_QUIESCING")
        lines[quiesce:quiesce] = [
            line(
                "GAMEPAD_D1_PROGRESS elapsed_ms=10000 connected=1 exact=1 "
                "reports=8 disconnect=1 reconnect=1"
            ),
        ]
        self.assert_passes(lines)

    def test_active_requires_matching_immediately_prior_state(self) -> None:
        lines = valid_lines()
        active = line_index(lines, "GAMEPAD_D1_ACTIVE_INPUT")
        lines.insert(active, line(
            "GAMEPAD_D1_PROGRESS elapsed_ms=5000 connected=1 exact=1 "
            "reports=4 disconnect=0 reconnect=0"
        ))
        self.assert_fails(lines)

        lines = valid_lines()
        state = line_index(lines, "GAMEPAD_D1_STATE")
        lines.insert(state + 1, lines[state].replace("sequence=5", "sequence=4"))
        self.assert_fails(lines)

    def test_physical_disconnect_requires_platform_removed_before_pair(self) -> None:
        variants = []
        for reason in (
            None, "transfer-error", "queue-overflow", "decode-error", "unknown",
        ):
            lines = valid_reconnect_lines()
            platform = line_index(lines, "GAMEPAD_DISCONNECTED", "reason=removed")
            if reason is None:
                lines.pop(platform)
            else:
                lines[platform] = lines[platform].replace("reason=removed", f"reason={reason}")
            variants.append((reason, lines))
        lines = valid_reconnect_lines()
        platform = lines.pop(line_index(lines, "GAMEPAD_DISCONNECTED", "reason=removed"))
        diag = line_index(lines, "GAMEPAD_D1_DISCONNECTED", "device-event")
        lines.insert(diag + 2, platform)
        variants.append(("late-removed", lines))
        for reason, lines in variants:
            with self.subTest(reason=reason):
                self.assert_fails(lines)

    def test_reconnect_requires_physical_pair_before_transport_and_diag(self) -> None:
        for token in ("GAMEPAD_D1_DISCONNECTED", "GAMEPAD_D1_NEUTRAL"):
            lines = valid_reconnect_lines()
            event = lines.pop(line_index(lines, token, "device-event"))
            reconnect_diag = line_index(lines, "GAMEPAD_D1_CONNECTED", "session=2")
            lines.insert(reconnect_diag + 1, event)
            with self.subTest(token=token):
                self.assert_fails(lines)

    def test_cleanup_transport_sequence_is_exact_and_correlated(self) -> None:
        mutations = []
        for token in ("USB_HOST_QUIESCING", "USB_HOST_STOPPED"):
            lines = valid_lines()
            lines.insert(line_index(lines, token), lines[line_index(lines, token)])
            mutations.append((f"duplicate-{token}", lines))
        lines = valid_lines()
        shutdown = line_index(lines, "GAMEPAD_DISCONNECTED", "reason=shutdown")
        lines[shutdown] = lines[shutdown].replace("session=1", "session=2")
        mutations.append(("wrong-shutdown-session", lines))
        lines = valid_lines()
        stopped = lines.pop(line_index(lines, "USB_HOST_STOPPED"))
        cleanup = line_index(lines, "GAMEPAD_D1_CLEANUP")
        lines.insert(cleanup + 1, stopped)
        mutations.append(("stopped-after-cleanup", lines))
        lines = valid_lines()
        cleanup = line_index(lines, "GAMEPAD_D1_CLEANUP")
        lines.insert(cleanup, line("GAMEPAD_DISCONNECTED session=1 reason=removed neutral=1"))
        mutations.append(("extra-removed", lines))
        for name, lines in mutations:
            with self.subTest(name=name):
                self.assert_fails(lines)

    def test_uint32_and_interface_uint8_bounds_are_enforced(self) -> None:
        mutations = (
            ("GAMEPAD_CONNECTED", "session=1", "session=4294967296"),
            ("GAMEPAD_D1_CONNECTED", "session=1", "session=4294967296"),
            ("GAMEPAD_D1_STATE", "sequence=5", "sequence=4294967296"),
            ("GAMEPAD_D1_ACTIVE_INPUT", "sequence=5", "sequence=4294967296"),
            ("GAMEPAD_D1_DISCONNECTED", "sequence=6", "sequence=4294967296"),
            ("GAMEPAD_D1_STATS", "reports_committed=4", "reports_committed=4294967296"),
            ("GAMEPAD_D1_PROGRESS", "elapsed_ms=10000", "elapsed_ms=4294967296"),
            ("GAMEPAD_CONNECTED", "interface=0", "interface=256"),
            ("GAMEPAD_D1_CONNECTED", "interface=0", "interface=256"),
        )
        for token, old, new in mutations:
            lines = valid_lines()
            if token == "GAMEPAD_D1_PROGRESS":
                lines.insert(line_index(lines, "USB_HOST_QUIESCING"), line(
                    "GAMEPAD_D1_PROGRESS elapsed_ms=10000 connected=1 exact=1 "
                    "reports=4 disconnect=0 reconnect=0"
                ))
            index = line_index(lines, token)
            lines[index] = lines[index].replace(old, new)
            with self.subTest(token=token, field=old):
                self.assert_fails(lines)

        lines = valid_lines()
        for token in ("GAMEPAD_CONNECTED", "GAMEPAD_D1_CONNECTED"):
            index = line_index(lines, token)
            lines[index] = lines[index].replace("interface=0", "interface=255")
        self.assert_passes(lines)

    def test_one_sided_reconnect_records_fail_closed_without_exception(self) -> None:
        for token in ("GAMEPAD_CONNECTED", "GAMEPAD_D1_CONNECTED"):
            lines = valid_reconnect_lines()
            lines.pop(line_index(lines, token, "session=2"))
            with self.subTest(token=token):
                self.assert_fails(lines)

    def test_progress_is_monotonic_and_truth_bound(self) -> None:
        base = valid_lines()
        quiesce = line_index(base, "USB_HOST_QUIESCING")
        base[quiesce:quiesce] = [
            line(
                "GAMEPAD_D1_PROGRESS elapsed_ms=5000 connected=1 exact=1 "
                "reports=2 disconnect=0 reconnect=0"
            ),
            line(
                "GAMEPAD_D1_PROGRESS elapsed_ms=10000 connected=1 exact=1 "
                "reports=4 disconnect=0 reconnect=0"
            ),
        ]
        self.assert_passes(base)
        mutations = (
            ("elapsed_ms=10000", "elapsed_ms=5000"),
            ("reports=4", "reports=1"),
            ("reports=4", "reports=5"),
            ("connected=1", "connected=0"),
            ("exact=1", "exact=0"),
            ("disconnect=0", "disconnect=1"),
            ("reconnect=0", "reconnect=1"),
        )
        for old, new in mutations:
            lines = list(base)
            progress = [i for i, value in enumerate(lines) if "GAMEPAD_D1_PROGRESS" in value][-1]
            lines[progress] = lines[progress].replace(old, new)
            with self.subTest(field=old):
                self.assert_fails(lines)

        lines = valid_lines()
        ready = line_index(lines, "GAMEPAD_D1_READY")
        lines.insert(ready, line(
            "GAMEPAD_D1_PROGRESS elapsed_ms=5000 connected=0 exact=0 "
            "reports=0 disconnect=0 reconnect=0"
        ))
        self.assert_fails(lines)

    def test_unbound_window_complete_claim_is_never_accepted(self) -> None:
        lines = valid_lines()
        lines.insert(
            line_index(lines, "USB_HOST_QUIESCING"),
            line("GAMEPAD_D1_WINDOW_COMPLETE reason=disconnect-reconnect-report"),
        )
        self.assert_fails(lines)

        lines = valid_reconnect_lines()
        quiesce = line_index(lines, "USB_HOST_QUIESCING")
        marker = line("GAMEPAD_D1_WINDOW_COMPLETE reason=disconnect-reconnect-report")
        lines.insert(quiesce, marker)
        self.assert_fails(lines)

        lines = valid_reconnect_lines()
        transport = line_index(lines, "GAMEPAD_CONNECTED", "session=2")
        lines.insert(transport, marker)
        self.assert_fails(lines)

        lines = valid_reconnect_lines()
        stats = line_index(lines, "GAMEPAD_D1_STATS")
        lines[stats] = lines[stats].replace("reports_committed=8", "reports_committed=4")
        lines.insert(line_index(lines, "USB_HOST_QUIESCING"), marker)
        summary = CAPTURE.summarize(transcript(lines), EXPECTED)
        self.assertEqual(summary["result"], "fail")
        self.assertFalse(summary["window_complete_claim_accepted"])

    def test_duplicate_lifecycle_and_fault_marker_fail(self) -> None:
        lines = valid_lines()
        lines.insert(4, lines[3])
        self.assert_fails(lines)
        lines = valid_lines()
        lines.insert(12, line("USB_DAEMON_ERROR code=ESP_FAIL"))
        self.assert_fails(lines)

    def test_active_state_must_be_connected_and_canonical(self) -> None:
        lines = valid_lines()
        lines[10] = lines[10].replace("connected=1", "connected=0")
        self.assert_fails(lines)
        lines = valid_lines()
        lines[10] = lines[10].replace("dpad=0", "dpad=9")
        lines[11] = lines[11].replace("dpad=0", "dpad=9")
        self.assert_passes(lines)
        lines = valid_lines()
        lines[10] = lines[10].replace("dpad=0", "dpad=16")
        lines[11] = lines[11].replace("dpad=0", "dpad=16")
        self.assert_fails(lines)
        for source, replacement in (
            ("buttons=0000000000000001", "buttons=8000000000000001"),
            ("dpad=0", "dpad=5"),
            ("lx=0", "lx=1"),
            ("ry=0", "ry=-1"),
            ("lt=0", "lt=1"),
            ("rt=0", "rt=65535"),
        ):
            lines = valid_lines()
            lines[10] = lines[10].replace(source, replacement)
            if source.startswith("buttons="):
                lines[11] = lines[11].replace(source, replacement)
            if source == "dpad=0":
                lines[11] = lines[11].replace(source, replacement)
            with self.subTest(replacement=replacement):
                self.assert_fails(lines)

    def test_cleanup_and_stats_are_exact(self) -> None:
        lines = valid_lines()
        index = line_index(lines, "GAMEPAD_D1_CLEANUP")
        lines[index] = lines[index].replace("quiesce=ESP_OK", "quiesce=ESP_FAIL")
        self.assert_fails(lines)
        lines = valid_lines()
        index = line_index(lines, "GAMEPAD_D1_STATS")
        lines[index] = lines[index].replace("reports_dropped=0", "reports_dropped=1")
        self.assert_fails(lines)
        lines = valid_lines()
        index = line_index(lines, "GAMEPAD_D1_STATS")
        lines[index] = lines[index].replace("disconnections=1", "disconnections=0")
        self.assert_fails(lines)

    def test_hotplug_result_bits_require_exact_events(self) -> None:
        lines = valid_lines()
        index = line_index(lines, "GAMEPAD_D1_RESULT")
        lines[index] = lines[index].replace("physical_disconnect=0", "physical_disconnect=1")
        self.assert_fails(lines)

    def test_connected_smoke_requires_controlled_cleanup_pair(self) -> None:
        lines = [value for value in valid_lines() if "source=controlled-cleanup" not in value]
        self.assert_fails(lines)

    def test_reconnect_must_follow_same_session_physical_neutral(self) -> None:
        self.assert_passes(valid_reconnect_lines())
        lines = valid_lines()
        transport = lines[8].replace("session=1", "session=2")
        diag = lines[9].replace("session=1", "session=2").replace(
            "reconnect_after_disconnect=0", "reconnect_after_disconnect=1"
        )
        state = lines[10].replace("session=1", "session=2").replace("sequence=5", "sequence=9")
        lines[12:12] = [transport, diag, state]
        lines.insert(15, line("GAMEPAD_D1_DISCONNECTED session=1 sequence=6 source=device-event"))
        lines.insert(16, line("GAMEPAD_D1_NEUTRAL session=1 source=device-event neutral=1 held_input_before=1"))
        lines[-5] = lines[-5].replace("session=1", "session=2")
        lines[-4] = lines[-4].replace("session=1", "session=2")
        lines[-3] = lines[-3].replace("interfaces_seen=1", "interfaces_seen=2").replace(
            "connections=1 disconnections=1", "connections=2 disconnections=2"
        )
        lines[-2] = lines[-2].replace(
            "physical_disconnect=0 disconnect_neutral=0 held_disconnect_neutral=0 reconnect=0",
            "physical_disconnect=1 disconnect_neutral=1 held_disconnect_neutral=1 reconnect=1",
        )
        self.assert_fails(lines)

    def test_all_runtime_events_precede_cleanup_and_cleanup_pair_precedes_stats(self) -> None:
        lines = valid_reconnect_lines()
        physical = [lines.pop(12), lines.pop(12)]
        cleanup = next(index for index, value in enumerate(lines) if "GAMEPAD_D1_CLEANUP" in value)
        lines[cleanup + 1:cleanup + 1] = physical
        self.assert_fails(lines)

        lines = valid_lines()
        state = lines.pop(10)
        stats = next(index for index, value in enumerate(lines) if "GAMEPAD_D1_STATS" in value)
        lines.insert(stats + 1, state)
        self.assert_fails(lines)

        lines = valid_lines()
        disconnected = line_index(lines, "GAMEPAD_D1_DISCONNECTED", "controlled-cleanup")
        neutral = line_index(lines, "GAMEPAD_D1_NEUTRAL", "controlled-cleanup")
        result = line_index(lines, "GAMEPAD_D1_RESULT")
        lines[disconnected] = lines[disconnected].replace("controlled-cleanup", "device-event")
        lines[neutral] = lines[neutral].replace("controlled-cleanup", "device-event")
        lines[result] = lines[result].replace(
            "physical_disconnect=0 disconnect_neutral=0 held_disconnect_neutral=0",
            "physical_disconnect=1 disconnect_neutral=1 held_disconnect_neutral=1",
        )
        self.assert_fails(lines)

    def test_sessions_are_unique_contiguous_and_have_one_connect_pair(self) -> None:
        lines = valid_lines()
        lines[8:10] = [lines[8], lines[9], lines[8], lines[9]]
        lines[17] = lines[17].replace("interfaces_seen=1", "interfaces_seen=2").replace(
            "connections=1 disconnections=1", "connections=2 disconnections=2"
        )
        self.assert_fails(lines)

    def test_held_input_matches_immediately_prior_state(self) -> None:
        lines = valid_lines()
        neutral = line_index(lines, "GAMEPAD_D1_NEUTRAL", "controlled-cleanup")
        lines[neutral] = lines[neutral].replace("held_input_before=1", "held_input_before=0")
        self.assert_fails(lines)

        lines = valid_reconnect_lines()
        physical_neutral = next(
            index for index, value in enumerate(lines)
            if "source=device-event neutral=1" in value
        )
        lines[physical_neutral] = lines[physical_neutral].replace(
            "held_input_before=1", "held_input_before=0"
        )
        self.assert_fails(lines)

    def test_disconnect_sequence_allows_unlogged_report_jumps_but_not_stale(self) -> None:
        lines = valid_lines()
        disconnected = line_index(lines, "GAMEPAD_D1_DISCONNECTED", "controlled-cleanup")
        lines[disconnected] = lines[disconnected].replace("sequence=6", "sequence=11")
        stats = line_index(lines, "GAMEPAD_D1_STATS")
        lines[stats] = lines[stats].replace("reports_committed=4", "reports_committed=9")
        self.assert_passes(lines)

        lines = valid_lines()
        disconnected = line_index(lines, "GAMEPAD_D1_DISCONNECTED", "controlled-cleanup")
        neutral = line_index(lines, "GAMEPAD_D1_NEUTRAL", "controlled-cleanup")
        lines[disconnected] = lines[disconnected].replace("sequence=6", "sequence=11")
        stats = line_index(lines, "GAMEPAD_D1_STATS")
        lines[stats] = lines[stats].replace("reports_committed=4", "reports_committed=9")
        lines[neutral] = lines[neutral].replace("held_input_before=1", "held_input_before=0")
        self.assert_passes(lines)

        for sequence in (5, 4):
            lines = valid_lines()
            disconnected = line_index(
                lines, "GAMEPAD_D1_DISCONNECTED", "controlled-cleanup"
            )
            lines[disconnected] = lines[disconnected].replace(
                "sequence=6", f"sequence={sequence}"
            )
            with self.subTest(sequence=sequence):
                self.assert_fails(lines)

        lines = valid_reconnect_lines()
        reconnect_state = line_index(lines, "GAMEPAD_D1_STATE", "session=2")
        lines[reconnect_state] = lines[reconnect_state].replace(
            "sequence=9", "sequence=2"
        )
        self.assert_fails(lines)

    def test_stats_report_count_matches_final_disconnect_sequence(self) -> None:
        for reports in (1, 400):
            lines = valid_lines()
            stats = line_index(lines, "GAMEPAD_D1_STATS")
            lines[stats] = lines[stats].replace(
                "reports_committed=4", f"reports_committed={reports}"
            )
            with self.subTest(reports=reports):
                self.assert_fails(lines)

        lines = valid_reconnect_lines()
        stats = line_index(lines, "GAMEPAD_D1_STATS")
        lines[stats] = lines[stats].replace(
            "reports_committed=8", "reports_committed=4"
        )
        self.assert_fails(lines)
        lines = valid_lines()
        lines.insert(12, line("GAMEPAD_D1_DISCONNECTED session=1 sequence=6 source=device-event"))
        lines.insert(13, line("GAMEPAD_D1_NEUTRAL session=2 source=device-event neutral=1 held_input_before=1"))
        result = line_index(lines, "GAMEPAD_D1_RESULT")
        lines[result] = lines[result].replace(
            "physical_disconnect=0 disconnect_neutral=0 held_disconnect_neutral=0",
            "physical_disconnect=1 disconnect_neutral=1 held_disconnect_neutral=1",
        )
        self.assert_fails(lines)


class InactiveGateTests(unittest.TestCase):
    def test_committed_authorization_is_inactive(self) -> None:
        auth = STATE.load_object(ROOT / STATE.AUTH_REL, "authorization")
        self.assertIs(auth["active"], False)
        self.assertIs(auth["issuable"], False)
        with self.assertRaises(STATE.StateError):
            STATE.validate_active_authorization(ROOT / STATE.AUTH_REL, ROOT)

    def test_project_root_and_sector_span_are_exact(self) -> None:
        self.assertEqual(STATE.exact_project_root(ROOT), ROOT)
        self.assertEqual(STATE.mutation_span(301328), 311296)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(STATE.StateError):
                STATE.exact_project_root(pathlib.Path(directory))

    def test_arm_frame_contract_is_133_bytes(self) -> None:
        token = "a" * 64
        frame = (CAPTURE.ARM_FRAME_PREFIX + token + "\n").encode("ascii")
        self.assertEqual(len(frame), 133)
        self.assertEqual(CAPTURE.ARM_FRAME_BYTES, 133)

    def test_reset_arm_and_restore_attempt_truth_is_durable(self) -> None:
        owner = "a" * 64
        base = {
            "schema": 1,
            "authorization_id": STATE.AUTH_ID,
            "terminal": False,
            "restore_required": True,
            "owner_token_sha256": hashlib.sha256(owner.encode("ascii")).hexdigest(),
            "status": "hard-reset-attempted",
            "hard_reset_attempt_count": 1,
            "launch_hard_reset_count": 0,
            "arm_frame_tx_attempt_count": 0,
            "restore_download_reset_attempt_count": 0,
            "restore_write_attempt_count": 0,
        }
        with tempfile.TemporaryDirectory() as directory:
            state_path = pathlib.Path(directory) / "state.json"
            state_path.write_text(json.dumps(base))
            STATE.mark_hard_reset_succeeded(state_path, owner)
            state = STATE.load_object(state_path, "state")
            self.assertEqual(state["launch_hard_reset_count"], 1)
            self.assertEqual(state["hard_reset_outcome"], "hard-reset-succeeded")
            STATE.mark_arm_frame_attempt(state_path, owner)
            STATE.mark_arm_accepted(state_path, owner, 133, "b" * 64)
            state = STATE.load_object(state_path, "state")
            self.assertEqual(state["status"], "arm-accepted")
            self.assertEqual(state["arm_frame_tx_attempt_count"], 1)
            STATE.mark_restore_entry(state_path, owner)
            STATE.mark_restore_write_attempt(state_path, owner)
            state = STATE.load_object(state_path, "state")
            self.assertEqual(state["restore_download_reset_attempt_count"], 1)
            self.assertEqual(state["restore_write_attempt_count"], 1)
            # A later explicit recovery retry remains conservative and cumulative.
            STATE.mark_restore_entry(state_path, owner)
            STATE.mark_restore_write_attempt(state_path, owner)
            state = STATE.load_object(state_path, "state")
            self.assertEqual(state["restore_download_reset_attempt_count"], 2)
            self.assertEqual(state["restore_write_attempt_count"], 2)

    def test_capture_never_auto_retries_after_restore_entry(self) -> None:
        source = (ROOT / "scripts/gamepad-diag-capture.py").read_text()
        catch = source[source.index("except BaseException as error:", source.index("def capture")) :]
        retry_guard = catch[catch.index('state.get("status") in {') : catch.index(
            "and isinstance(receipt.get", catch.index('state.get("status") in {')
        )]
        self.assertNotIn('"restore-entry"', retry_guard)
        self.assertNotIn('"restore-write-attempted"', retry_guard)

    def test_preinstall_failure_destroys_bound_arm_secret_before_terminal(self) -> None:
        owner = "a" * 64
        local_state = ROOT / "hardware/local-state"
        local_state.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(
            dir=local_state, prefix="gamepad-diag-recovery."
        ) as directory:
            recovery = pathlib.Path(directory)
            recovery.chmod(0o700)
            secret = recovery / "arm-token"
            secret.write_bytes(b"b" * 64 + b"\n")
            secret.chmod(0o600)
            binding = STATE.private_file_binding(secret, 65, "arm secret")
            binding["token_sha256"] = hashlib.sha256(b"b" * 64).hexdigest()
            state = {
                "schema": 1,
                "authorization_id": STATE.AUTH_ID,
                "terminal": False,
                "restore_required": False,
                "owner_token_sha256": hashlib.sha256(owner.encode("ascii")).hexdigest(),
                "status": "preimage-bound",
                "recovery_directory": str(recovery.resolve()),
                "arm_token_sha256": binding["token_sha256"],
                "arm_secret": binding,
            }
            state_path = recovery / "state.json"
            state_path.write_text(json.dumps(state), encoding="utf-8")
            STATE.fail_if_reserved(state_path, owner, "preinstall-failure")
            finished = STATE.load_object(state_path, "state")
            self.assertFalse(secret.exists())
            self.assertTrue(finished["terminal"])
            self.assertTrue(finished["arm_secret_destroyed_before_terminal"])

    def test_already_consumed_arm_secret_terminalizes_idempotently(self) -> None:
        owner = "a" * 64
        local_state = ROOT / "hardware/local-state"
        local_state.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(
            dir=local_state, prefix="gamepad-diag-recovery."
        ) as directory:
            recovery = pathlib.Path(directory)
            recovery.chmod(0o700)
            secret = recovery / "arm-token"
            secret.write_bytes(b"b" * 64 + b"\n")
            secret.chmod(0o600)
            binding = STATE.private_file_binding(secret, 65, "arm secret")
            binding["token_sha256"] = hashlib.sha256(b"b" * 64).hexdigest()
            secret.unlink()
            state = {
                "schema": 1,
                "authorization_id": STATE.AUTH_ID,
                "terminal": False,
                "restore_required": False,
                "owner_token_sha256": hashlib.sha256(owner.encode("ascii")).hexdigest(),
                "status": "restore-verified",
                "recovery_directory": str(recovery.resolve()),
                "arm_token_sha256": binding["token_sha256"],
                "arm_secret": binding,
            }
            state_path = recovery / "state.json"
            state_path.write_text(json.dumps(state), encoding="utf-8")
            STATE.fail_if_reserved(state_path, owner, "restored-without-pass")
            finished = STATE.load_object(state_path, "state")
            self.assertEqual(finished["status"], "failed-safe-no-restore-required")
            self.assertTrue(finished["terminal"])
            self.assertTrue(finished["arm_secret_destroyed_before_terminal"])

    def test_compiled_fixture_binding_rejects_every_mismatch(self) -> None:
        qualification = {
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
        }
        expected = STATE.expected_compiled_fixture(
            "fixture-exact", "a" * 64, qualification
        )
        self.assertEqual(expected["fixture_evidence_id"], "fixture-exact")
        self.assertEqual(expected["fixture_evidence_sha256"], "a" * 64)
        self.assertEqual(expected["current_limit_ma"], 250)
        record = {
            "sdkconfig_path": "apps/gamepad_diag/sdkconfig",
            "sdkconfig_sha256": "b" * 64,
            "compiled_fixture": expected,
        }
        for field in expected:
            changed = dict(expected)
            value = changed[field]
            changed[field] = (
                not value if isinstance(value, bool)
                else 251 if isinstance(value, int)
                else "changed"
            )
            mismatch = dict(record)
            mismatch["compiled_fixture"] = changed
            with self.subTest(field=field), self.assertRaises(STATE.StateError):
                STATE.validate_active_resolved_fixture_config(ROOT, mismatch, expected)

    def test_recovery_gate_inventory_rejects_orchestrator_drift(self) -> None:
        evidence_path = ROOT / STATE.BUILD_EVIDENCE_REL
        inventory = {
            "scripts/gamepad-diag-capture.py": CAPTURE.sha256_file(
                pathlib.Path(CAPTURE.__file__)
            ),
            "scripts/gamepad-diag-one-shot-state.py": CAPTURE.sha256_file(
                CAPTURE.STATE_TOOL_PATH
            ),
            "scripts/gamepad-diag-restore.py": CAPTURE.sha256_file(
                CAPTURE.RESTORE_TOOL_PATH
            ),
            "scripts/gamepad-diag-install.py": CAPTURE.sha256_file(
                ROOT / "scripts/gamepad-diag-install.py"
            ),
            "scripts/tests/test-gamepad-diag-install.py": CAPTURE.sha256_file(
                ROOT / "scripts/tests/test-gamepad-diag-install.py"
            ),
            "scripts/flash.sh": CAPTURE.sha256_file(ROOT / "scripts/flash.sh"),
        }
        state = {
            "build_evidence_path": str(evidence_path.resolve()),
            "build_evidence_sha256": CAPTURE.sha256_file(evidence_path),
        }
        with mock.patch.object(
            CAPTURE, "load_json", return_value={"gate_inventory": inventory}
        ):
            CAPTURE.validate_recovery_gate_inventory(state)
            for field in inventory:
                changed = dict(inventory)
                changed[field] = "0" * 64
                with self.subTest(field=field), mock.patch.object(
                    CAPTURE, "load_json", return_value={"gate_inventory": changed}
                ), self.assertRaises(CAPTURE.CaptureError):
                    CAPTURE.validate_recovery_gate_inventory(state)

    def test_final_install_transition_rejects_every_bound_contract_drift(self) -> None:
        owner = "a" * 64
        auth_path = ROOT / "auth-test.json"
        fixture_path = ROOT / "fixture-test.json"
        metadata_path = ROOT / "metadata-test.json"
        profile_path = ROOT / "profile-test.json"
        build_path = ROOT / "build-test.json"
        arm_digest = "b" * 64
        base = {
            "schema": 1,
            "authorization_id": STATE.AUTH_ID,
            "status": "preimage-bound",
            "terminal": False,
            "restore_required": False,
            "owner_token_sha256": hashlib.sha256(owner.encode("ascii")).hexdigest(),
            "project_root": str(ROOT),
            "authorization_sha256": "auth-sha",
            "fixture_evidence_path": str(fixture_path.resolve()),
            "fixture_evidence_sha256": "fixture-sha",
            "metadata_path": str(metadata_path.resolve()),
            "metadata_sha256": "metadata-sha",
            "board_profile_path": str(profile_path.resolve()),
            "board_profile_sha256": "profile-sha",
            "build_evidence_path": str(build_path.resolve()),
            "build_evidence_sha256": "build-sha",
            "arm_token_sha256": arm_digest,
            "exact_artifact": {
                "offset": STATE.EXPECTED_OFFSET,
                "bytes": STATE.EXPECTED_BYTES,
                "sha256": STATE.EXPECTED_SHA256,
                "sector_bytes": STATE.SECTOR_BYTES,
                "install_write_block_bytes": STATE.INSTALL_WRITE_BLOCK_BYTES,
                "mutation_span_bytes": STATE.mutation_span(STATE.EXPECTED_BYTES),
            },
            "restore_preimage": {
                "path": "/sealed",
                "bytes": STATE.mutation_span(STATE.EXPECTED_BYTES),
                "offset": int(STATE.EXPECTED_OFFSET, 0),
                "sector_bytes": STATE.SECTOR_BYTES,
                "install_write_block_bytes": STATE.INSTALL_WRITE_BLOCK_BYTES,
                "mutation_span_bytes": STATE.mutation_span(STATE.EXPECTED_BYTES),
            },
            "arm_secret": {
                "path": "/secret", "bytes": 65, "token_sha256": arm_digest
            },
            "live_partition_table": {
                "restore_tool": {
                    "path": str(STATE.RESTORE_TOOL_PATH.resolve()),
                    "sha256": "restore-tool-sha",
                }
            },
        }

        class RestoreStub:
            @staticmethod
            def validate_partition_binding(*_args, **_kwargs) -> None:
                return None

        authorization_result = (
            {"host_arm": {"token_sha256": arm_digest}},
            {
                "id": "fixture-test",
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
            },
            fixture_path,
            "fixture-sha",
            metadata_path,
            "metadata-sha",
            profile_path,
            "profile-sha",
        )

        def fake_hash(path: pathlib.Path) -> str:
            return (
                "restore-tool-sha"
                if pathlib.Path(path).resolve() == STATE.RESTORE_TOOL_PATH.resolve()
                else "auth-sha"
            )

        mutations = (
            "project_root", "authorization_sha256", "fixture_evidence_path",
            "fixture_evidence_sha256", "metadata_path", "metadata_sha256",
            "board_profile_path", "board_profile_sha256", "build_evidence_path",
            "build_evidence_sha256", "arm_token_sha256",
        )
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(
            STATE, "validate_active_authorization", return_value=authorization_result
        ), mock.patch.object(
            STATE, "validate_active_build_evidence", return_value=(build_path, "build-sha")
        ), mock.patch.object(
            STATE, "load_restore_tool", return_value=RestoreStub()
        ), mock.patch.object(
            STATE, "binding_matches", return_value=True
        ), mock.patch.object(
            STATE, "sha256_file", side_effect=fake_hash
        ):
            state_path = pathlib.Path(directory) / "state.json"
            state_path.write_text(json.dumps(base), encoding="utf-8")
            STATE.mark_install_write_attempt(state_path, auth_path, ROOT, owner)
            self.assertEqual(
                STATE.load_object(state_path, "state")["status"],
                "install-write-attempted",
            )
            for field in mutations:
                drifted = dict(base)
                drifted[field] = "changed"
                state_path.write_text(json.dumps(drifted), encoding="utf-8")
                with self.subTest(field=field), self.assertRaises(STATE.StateError):
                    STATE.mark_install_write_attempt(state_path, auth_path, ROOT, owner)

    def test_scoped_board_profile_requires_exact_fixture_id(self) -> None:
        fixture = ROOT / "hardware/evidence/future-gamepad-fixture.json"
        fixture_sha = "a" * 64
        base_profile = {
            "schema": 1,
            "pin_map_authorized": False,
            "device_identity": {"sha256": STATE.EXPECTED_DEVICE_SHA256},
            "peripheral_authorizations": {
                "usb_host": {
                    "authorized": True,
                    "scope": "esp32p4_hs_peripheral_0_direct_j16_data_only",
                    "peripheral": "esp32p4_usb_otg_hs_peripheral_0",
                    "j16_vbus_mode": "physically-open-fixture-owned",
                    "fixture_evidence_id": "fixture-exact",
                    "fixture_evidence": fixture.relative_to(ROOT).as_posix(),
                    "fixture_evidence_sha256": fixture_sha,
                }
            },
        }
        with mock.patch.object(
            STATE, "load_object", return_value=base_profile
        ), mock.patch.object(
            STATE, "sha256_file", return_value="b" * 64
        ), mock.patch.object(pathlib.Path, "resolve", autospec=True) as resolve:
            # Keep the canonical board-profile path check hermetic while still
            # exercising the exact validator predicate.
            def resolve_path(path: pathlib.Path, *args, **kwargs):
                if path == ROOT / STATE.BOARD_PROFILE_REL:
                    return ROOT / STATE.BOARD_PROFILE_REL
                return pathlib.Path(path).absolute()
            resolve.side_effect = resolve_path
            with mock.patch.object(pathlib.Path, "is_file", return_value=True):
                STATE.validate_active_board_profile(
                    ROOT, fixture, fixture_sha, "fixture-exact"
                )
                with self.assertRaises(STATE.StateError):
                    STATE.validate_active_board_profile(
                        ROOT, fixture, fixture_sha, "fixture-wrong"
                    )


if __name__ == "__main__":
    unittest.main()
