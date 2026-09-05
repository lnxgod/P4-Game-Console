#!/usr/bin/env python3
"""Host checks for the fixed-size H1 USB Drive control client framing."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest


SERIAL_STUB = types.SimpleNamespace(Serial=object, SerialException=OSError)
sys.modules.setdefault("serial", SERIAL_STUB)
SCRIPT = Path(__file__).resolve().parents[1] / "p4-transfer.py"
SPEC = importlib.util.spec_from_file_location("p4_transfer", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
P4_TRANSFER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P4_TRANSFER)


class UsbDriveProtocolTests(unittest.TestCase):
    def test_status_request_is_bounded_and_crc_protected(self) -> None:
        request = P4_TRANSFER.make_usb_drive_request(
            P4_TRANSFER.USB_DRIVE_COMMAND_STATUS, False, 0x12345678, 0
        )
        self.assertEqual(len(request), P4_TRANSFER.USB_DRIVE_REQUEST_BYTES)
        self.assertEqual(request[:4], b"P4U1")
        self.assertEqual(request[4:8], bytes((1, 1, 0, 0)))
        self.assertEqual(
            struct.unpack_from("<I", request, 16)[0], P4_TRANSFER.crc32(request[:16])
        )

    def test_mode_request_rejects_unbounded_or_invalid_fields(self) -> None:
        with self.assertRaises(P4_TRANSFER.TransferError):
            P4_TRANSFER.make_usb_drive_request(99, False, 1, 0)
        with self.assertRaises(P4_TRANSFER.TransferError):
            P4_TRANSFER.make_usb_drive_request(
                P4_TRANSFER.USB_DRIVE_COMMAND_SET_MODE, True, 0, 1
            )

    def test_response_status_includes_eject_gate(self) -> None:
        response = bytearray(P4_TRANSFER.USB_DRIVE_RESPONSE_BYTES)
        response[:4] = b"P4V1"
        response[4:10] = bytes((1, 2, 4, 1, 3, 0b01011))
        struct.pack_into("<III", response, 12, 9, 0x44, 1)
        struct.pack_into("<I", response, 24, P4_TRANSFER.crc32(response[:24]))

        class Reader:
            def frame(self, marker: bytes, size: int, timeout: float) -> bytes:
                self.marker = marker
                self.size = size
                self.timeout = timeout
                return bytes(response)

        parsed = P4_TRANSFER.read_usb_drive_response(Reader())
        rendered = P4_TRANSFER.render_usb_drive_status(parsed)
        self.assertIn("result=denied", rendered)
        self.assertIn("attached=1", rendered)
        self.assertIn("ejected=0", rendered)
        self.assertIn("msc_driver=1", rendered)

    def test_status_session_retries_only_absent_bounded_response(self) -> None:
        session = 0x12345678
        response = {
            "command": P4_TRANSFER.USB_DRIVE_COMMAND_STATUS,
            "result": 0,
            "mode": 0,
            "storage_state": 0,
            "flags": 0,
            "generation": 0,
            "session": session,
            "sequence": 0,
        }
        writes: list[bytes] = []
        outcomes: list[object] = [
            P4_TRANSFER.UsbDriveResponseTimeout("not ready"), response
        ]
        original_write_all = P4_TRANSFER.write_all
        original_read_response = P4_TRANSFER.read_usb_drive_response

        def fake_write_all(connection: object, data: bytes) -> None:
            self.assertIs(connection, marker)
            writes.append(data)

        def fake_read_response(reader: object, timeout: float) -> dict[str, int]:
            self.assertIs(reader, reader_marker)
            self.assertEqual(timeout, P4_TRANSFER.USB_DRIVE_STATUS_ATTEMPT_TIMEOUT)
            outcome = outcomes.pop(0)
            if isinstance(outcome, Exception):
                raise outcome
            return outcome  # type: ignore[return-value]

        marker = object()
        reader_marker = object()
        try:
            P4_TRANSFER.write_all = fake_write_all
            P4_TRANSFER.read_usb_drive_response = fake_read_response
            actual = P4_TRANSFER.open_usb_drive_session(
                marker, reader_marker, session
            )
        finally:
            P4_TRANSFER.write_all = original_write_all
            P4_TRANSFER.read_usb_drive_response = original_read_response

        self.assertEqual(actual, response)
        self.assertEqual(len(writes), 2)
        self.assertEqual(writes[0], writes[1])
        self.assertEqual(
            writes[0],
            P4_TRANSFER.make_usb_drive_request(
                P4_TRANSFER.USB_DRIVE_COMMAND_STATUS, False, session, 0
            ),
        )

    def test_status_session_does_not_retry_invalid_response(self) -> None:
        marker = object()
        reader_marker = object()
        writes: list[bytes] = []
        original_write_all = P4_TRANSFER.write_all
        original_read_response = P4_TRANSFER.read_usb_drive_response

        try:
            P4_TRANSFER.write_all = lambda connection, data: writes.append(data)
            P4_TRANSFER.read_usb_drive_response = (
                lambda reader, timeout: (_ for _ in ()).throw(
                    P4_TRANSFER.TransferError("invalid response")
                )
            )
            with self.assertRaisesRegex(P4_TRANSFER.TransferError, "invalid response"):
                P4_TRANSFER.open_usb_drive_session(marker, reader_marker, 1)
        finally:
            P4_TRANSFER.write_all = original_write_all
            P4_TRANSFER.read_usb_drive_response = original_read_response

        self.assertEqual(len(writes), 1)

    def test_status_session_readiness_retries_are_strictly_bounded(self) -> None:
        marker = object()
        reader_marker = object()
        writes: list[bytes] = []
        original_write_all = P4_TRANSFER.write_all
        original_read_response = P4_TRANSFER.read_usb_drive_response

        try:
            P4_TRANSFER.write_all = lambda connection, data: writes.append(data)
            P4_TRANSFER.read_usb_drive_response = (
                lambda reader, timeout: (_ for _ in ()).throw(
                    P4_TRANSFER.UsbDriveResponseTimeout("not ready")
                )
            )
            with self.assertRaisesRegex(P4_TRANSFER.TransferError, "30 bounded"):
                P4_TRANSFER.open_usb_drive_session(marker, reader_marker, 1)
        finally:
            P4_TRANSFER.write_all = original_write_all
            P4_TRANSFER.read_usb_drive_response = original_read_response

        self.assertEqual(len(writes), P4_TRANSFER.USB_DRIVE_STATUS_RETRY_ATTEMPTS)


if __name__ == "__main__":
    unittest.main()
