#!/usr/bin/env python3
"""Host-only receipt/ledger and static same-handle transport tests."""

from __future__ import annotations

import ast
import hashlib
import json
import os
import pathlib
import stat
import sys
import tempfile
import datetime as dt


SCRIPTS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import d23_capture_transport as transport  # noqa: E402


def write_private(path: pathlib.Path, value: dict) -> None:
    path.write_text(json.dumps(value) + "\n")
    path.chmod(0o600)


def must_fail(callable_, message: str) -> None:
    try:
        callable_()
    except Exception:
        return
    raise AssertionError(message)


def main() -> None:
    source = (SCRIPTS / "d23_capture_transport.py").read_text()
    flash_source = (SCRIPTS / "flash.sh").read_text()
    tree = ast.parse(source)
    forbidden = {"subprocess", "sounddevice"}
    imported = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imported.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            imported.add(node.module.split(".")[0])
    assert not (forbidden & imported), "transport shells out or uses sounddevice"
    assert "ESP32P4ROM(device, 115200, False)" in source
    assert "esp.run()" in source
    assert "esp.hard_reset()" in source
    assert source.count("esp.run()") == 1
    assert source.count("esp.hard_reset()") == 1
    launch_definition = source.index("def launch_cli_equivalent")
    assert source.index("esp.run()", launch_definition) \
        < source.index("esp.hard_reset()", launch_definition) \
        < source.index("device.dtr = False", launch_definition) \
        < source.index("device.rts = False", launch_definition)
    capture_definition = source.index("def capture")
    assert source.index("device = open_serial_once", capture_definition) \
        < source.index("launch_cli_equivalent(esp, device)", capture_definition) \
        < source.index("device.baudrate = 115200", capture_definition)
    launch_source = source[launch_definition:capture_definition]
    assert "flush_input" not in launch_source and "reset_input_buffer" not in launch_source
    assert "esp._port is not device or esp.uses_usb_otg()" in source
    assert source.index("esp.flush_input()") < source.index(
        "consumed = atomic_consume(receipt_path"
    )

    class FakeDevice:
        def __init__(self) -> None:
            self.events: list[str] = []
            self._dtr = False
            self._rts = False

        @property
        def dtr(self) -> bool:
            return self._dtr

        @dtr.setter
        def dtr(self, value: bool) -> None:
            self.events.append(f"dtr={int(value)}")
            self._dtr = value

        @property
        def rts(self) -> bool:
            return self._rts

        @rts.setter
        def rts(self, value: bool) -> None:
            self.events.append(f"rts={int(value)}")
            self._rts = value

    class FakeEsp:
        def __init__(self, device: FakeDevice, fail: str | None = None) -> None:
            self.device = device
            self.fail = fail

        def run(self) -> None:
            self.device.events.append("run")
            if self.fail == "run":
                raise RuntimeError("run failed")

        def hard_reset(self) -> None:
            self.device.events.extend(("hard-reset", "rts=1", "rts=0"))
            self.device._rts = False
            if self.fail == "hard-reset":
                self.device._rts = True
                raise RuntimeError("hard reset failed")

    launch_device = FakeDevice()
    transport.launch_cli_equivalent(FakeEsp(launch_device), launch_device)
    assert launch_device.events == [
        "run", "hard-reset", "rts=1", "rts=0", "dtr=0", "rts=0"
    ]
    for failure in ("run", "hard-reset"):
        failed_device = FakeDevice()
        must_fail(
            lambda failure=failure, device=failed_device:
                transport.launch_cli_equivalent(FakeEsp(device, failure), device),
            f"{failure} launch failure unexpectedly passed",
        )
        assert not failed_device.dtr and not failed_device.rts
    assert source.count("open_serial_once(args.port)") == 1
    assert "os.link(receipt_path, consumed)" in source
    assert source.index("atomic_consume(receipt_path") < source.index("esp.run()")
    assert "attributes[2] &= ~getattr(termios, \"HUPCL\", 0)" in source
    assert "device.dtr = False" in source and "device.rts = False" in source
    assert transport.ARM_NONCE == hashlib.sha256(transport.ARM_NONCE_SEED.encode()).hexdigest()
    assert len(transport.ARM_FRAME.encode()) == 138
    assert hashlib.sha256(transport.ARM_FRAME.encode()).hexdigest() == transport.ARM_FRAME_SHA256
    assert "--launch-receipt" in flash_source
    assert "--defer-launch-for-capture" in flash_source
    assert 'P4_D23_PREFLIGHT_RUN" -le 3' in flash_source
    assert flash_source.index("preflight-host --port") \
        < flash_source.index("check-issuable") \
        < flash_source.index("P4_D23_RESERVATION_ACTIVE=true") \
        < flash_source.index('"$P4_D23_CAPTURE_TOOL" reserve') \
        < flash_source.index("P4_PROBE_OUTPUT=$(esptool.py")
    assert flash_source.index("Authorized application readback identity mismatch") \
        < flash_source.index('"$P4_D23_CAPTURE_TOOL" emit-receipt')
    assert "trap p4_cleanup_flash_temps EXIT\n" in flash_source
    assert "trap p4_handle_hup HUP" in flash_source
    assert "trap p4_handle_int INT" in flash_source
    assert "trap p4_handle_term TERM" in flash_source
    assert "trap p4_cleanup_flash_temps EXIT HUP INT TERM" not in flash_source
    assert all(token in flash_source for token in ("exit 129", "exit 130", "exit 143"))
    audio_branch = flash_source[flash_source.rindex(
        'if [ "$P4_APP" = audio_direct_diag ]; then'):]
    assert "emit-receipt" in audio_branch and "esptool.py --chip esp32p4 --port" not in audio_branch
    accepted = transport.validate_initial_prefix(b"ESP-ROM:esp32p4\nrst:0x3 boot\n")
    assert accepted["reset_count"] == 1 and accepted["reset_code"] == "0x3"
    must_fail(lambda: transport.validate_initial_prefix(b"ESP-ROM:esp32p4\n"),
              "missing deliberate hard-reset code was accepted")
    must_fail(lambda: transport.validate_initial_prefix(b"rst:0x7 unsafe\n"),
              "unsafe reset code was accepted")
    must_fail(lambda: transport.validate_initial_prefix(b"ets legacy\n"),
              "legacy boot banner was accepted")

    with tempfile.TemporaryDirectory(dir="/private/tmp") as directory:
        root = pathlib.Path(directory)
        auth = root / "auth.json"
        write_private(auth, {"id": transport.AUTH_ID})
        state = root / "state.json"
        receipt = root / "receipt.json"
        nonce = transport.ARM_NONCE
        now = dt.datetime.now(dt.timezone.utc)
        receipt_value = {
            "nonce": nonce,
            "authorization_id": transport.AUTH_ID,
            "authorization_sha256": hashlib.sha256(auth.read_bytes()).hexdigest(),
            "authorization_path": str(auth),
            "created_at_utc": transport.utc_text(now),
            "expires_at_utc": transport.utc_text(now + dt.timedelta(minutes=10)),
            "capture_tool": transport.regular_sha_entry(SCRIPTS / "capture-audio-direct-diag.py"),
            "capture_transport": transport.regular_sha_entry(SCRIPTS / "d23_capture_transport.py"),
            "verifier": transport.regular_sha_entry(SCRIPTS / "verify-audio-direct-diag.py"),
            "analyzer": transport.regular_sha_entry(SCRIPTS / "analyze-audio-tone.py"),
        }
        write_private(receipt, receipt_value)
        ledger = {
            "schema": 1,
            "authorization_id": transport.AUTH_ID,
            "authorization_sha256": hashlib.sha256(auth.read_bytes()).hexdigest(),
            "arm_nonce": nonce,
            "nonce_sha256": hashlib.sha256(nonce.encode()).hexdigest(),
            "status": "pending",
            "created_at_utc": "2026-08-13T00:00:00Z",
            "receipt_path": str(receipt),
            "receipt_sha256": hashlib.sha256(receipt.read_bytes()).hexdigest(),
            "launch_count": 0,
        }
        write_private(state, ledger)
        consumed = transport.atomic_consume(receipt, state, receipt_value)
        assert not receipt.exists() and consumed.exists()
        updated = json.loads(state.read_text())
        assert updated["status"] == "consumed" and updated["launch_count"] == 1
        must_fail(lambda: transport.atomic_consume(receipt, state, receipt_value),
                  "consumed receipt was reusable")
        must_fail(lambda: transport.check_issuable(state, auth),
                  "same authorization was issuable twice")
        assert stat.S_IMODE(state.stat().st_mode) == 0o600

        transcript = b"prefix " + transport.SERIAL_ATTACH_LINE.encode() + b"\n" \
            + b"prefix " + transport.WAIT_ARM_LINE.encode() + b"\n" \
            + b"prefix " + transport.FIXED_MARKER_LINES[0].encode() + b"\n"
        parsed = transport.parse_contract_lines(transcript)
        assert [line["contract_line"] for line in parsed] == [
            transport.SERIAL_ATTACH_LINE, transport.WAIT_ARM_LINE,
            transport.FIXED_MARKER_LINES[0]
        ]
        assert parsed[0]["monotonic_ns"] < parsed[1]["monotonic_ns"]
        initial_banner = "rst:0x1 initial boot\n" + transport.SERIAL_ATTACH_LINE + "\n" \
            + transport.WAIT_ARM_LINE + "\n"
        after_attach = initial_banner.lower().find(transport.SERIAL_ATTACH_LINE.lower())
        assert not any(marker in initial_banner.lower()[after_attach:]
                       for marker in transport.RESET_SUBSTRINGS)
        replay = initial_banner + "rst:0x3 replay\n"
        after_attach = replay.lower().find(transport.SERIAL_ATTACH_LINE.lower())
        assert any(marker in replay.lower()[after_attach:]
                   for marker in transport.RESET_SUBSTRINGS)

        expired = dict(receipt_value)
        expired["created_at_utc"] = "2026-01-01T00:00:00Z"
        expired["expires_at_utc"] = "2026-01-01T00:10:00Z"
        second_receipt = root / "expired.json"
        write_private(second_receipt, expired)
        second_state = root / "expired-state.json"
        expired_ledger = dict(ledger)
        expired_ledger["receipt_path"] = str(second_receipt)
        expired_ledger["receipt_sha256"] = hashlib.sha256(second_receipt.read_bytes()).hexdigest()
        write_private(second_state, expired_ledger)
        must_fail(lambda: transport.atomic_consume(second_receipt, second_state, expired),
                  "expired receipt was consumed")

        output = root / "exclusive.bin"
        transport.atomic_write_bytes(output, b"first")
        assert output.read_bytes() == b"first"
        assert stat.S_IMODE(output.stat().st_mode) == 0o600
        must_fail(lambda: transport.atomic_write_bytes(output, b"second"),
                  "exclusive output was overwritten")

        reservation_state = root / "reservation.json"
        class Args:
            state = reservation_state
            receipt = root / "reserved-receipt.json"
            authorization = auth
            device_identity_sha256 = "b" * 64
            offset = 0x10000
            bytes = 123
            sha256 = "c" * 64
        auth_payload = {"id": transport.AUTH_ID,
                        "exact_artifact": {"offset": "0x10000", "bytes": 123,
                                           "sha256": "c" * 64}}
        write_private(auth, auth_payload)
        transport.reserve(Args)
        assert json.loads(reservation_state.read_text())["status"] == "preparing"
        must_fail(lambda: transport.reserve(Args), "duplicate reservation was accepted")

    print("D2.3 capture transport tests: PASS same_handle=1 receipt_atomic=1 one_shot=1")


if __name__ == "__main__":
    main()
