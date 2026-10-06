#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise native warm-boot acceptance without opening a serial device."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
serial_stub = SimpleNamespace(SerialException=type("SerialException", (Exception,), {}))
spec = importlib.util.spec_from_file_location("warm_boot", ROOT / "scripts/capture-tab5-warm-boot.py")
capture = importlib.util.module_from_spec(spec)
with mock.patch.dict(sys.modules, {"serial": serial_stub}):
    spec.loader.exec_module(capture)

NATIVE_LOG = "\n".join((
    "CHIP_USB_UART_RESET",
    "P4_CONSOLE_OS GAME_CATALOG available=1 packages=17",
    "P4_CONSOLE_OS READY board=m5stack-tab5 display=mipi-dsi",
    "OTA_BOOT_VALID result=ESP_OK",
))

class NativeWarmBootTests(unittest.TestCase):
    def test_native_boot_passes_without_legacy_readiness(self):
        self.assertNotIn("P4CART_READY", NATIVE_LOG)
        self.assertTrue(capture.boot_passed(NATIVE_LOG, None))

    def test_every_existing_readiness_condition_is_still_required(self):
        for marker in ("CHIP_USB_UART_RESET", "P4_CONSOLE_OS READY board=m5stack-tab5",
                       "OTA_BOOT_VALID result=ESP_OK"):
            with self.subTest(marker=marker):
                self.assertFalse(capture.boot_passed(NATIVE_LOG.replace(marker, "missing"), None))
        self.assertFalse(capture.boot_passed(NATIVE_LOG.replace("m5stack-tab5", "other-board"), None))

    def test_fatal_and_capture_errors_still_reject(self):
        for marker in ("FATAL_HOLD", "Guru Meditation", "HALT stage=display"):
            self.assertFalse(capture.boot_passed(NATIVE_LOG + "\n" + marker, None))
        self.assertFalse(capture.boot_passed(NATIVE_LOG, "serial disconnected"))

    def test_nonzero_legacy_option_rejects_before_serial_or_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "must-not-create"
            argv = ["capture", "--port", "must-not-open", "--unit", "A", "--output", str(output),
                    "--expected-script-carts", "1"]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(serial_stub, "Serial", create=True) as port, \
                 contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    capture.main()
                self.assertEqual(error.exception.code, 2)
                port.assert_not_called()
            self.assertFalse(output.exists())

    def test_zero_compatibility_option_records_native_only_receipt(self):
        connection = mock.Mock()
        connection.read.return_value = NATIVE_LOG.encode()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture"
            argv = ["capture", "--port", "mock-only", "--unit", "B", "--output", str(output),
                    "--cycles", "1", "--expected-script-carts", "0"]
            with mock.patch.object(sys, "argv", argv), \
                 mock.patch.object(serial_stub, "Serial", create=True, return_value=connection), \
                 contextlib.redirect_stdout(io.StringIO()):
                capture.main()
            connection.open.assert_called_once()
            connection.close.assert_called_once()
            receipt = json.loads((output / "receipt.json").read_text())
            self.assertTrue(receipt["cycles"][0]["passed"])
            self.assertEqual((output / "cycle-1.log").read_text(), NATIVE_LOG)
            sentinel = output / "cycle-1.log"
            with mock.patch.object(sys, "argv", argv), \
                 mock.patch.object(serial_stub, "Serial", create=True) as port:
                with self.assertRaises(FileExistsError):
                    capture.main()
                port.assert_not_called()
            self.assertEqual(sentinel.read_text(), NATIVE_LOG)

if __name__ == "__main__":
    unittest.main()
