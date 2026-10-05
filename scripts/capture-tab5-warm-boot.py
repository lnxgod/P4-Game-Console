#!/usr/bin/env python3
"""Capture bounded native-USB warm boots; opening this port may reboot the Tab5."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import serial
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--unit", required=True, choices=("A", "B"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cycles", type=int, choices=(1, 2, 3), default=3)
    parser.add_argument("--expected-script-carts", type=int, choices=range(17), default=0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    reports = []
    try:
        for cycle in range(1, args.cycles + 1):
            connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
            connection.dtr = False
            connection.rts = False
            connection.port = args.port
            received = bytearray()
            started = time.monotonic()
            deadline = started + 40.0
            capture_error = None
            try:
                # Native USB can briefly disappear during re-enumeration.
                open_deadline = started + 5.0
                while True:
                    try:
                        connection.open()
                        break
                    except (serial.SerialException, OSError):
                        connection.close()
                        if time.monotonic() >= open_deadline:
                            raise
                        time.sleep(0.1)
                while time.monotonic() < deadline and len(received) < 1024 * 1024:
                    received.extend(connection.read(4096))
                    text = received.decode("utf-8", "replace")
                    if any(marker in text for marker in ("FATAL_HOLD", "Guru Meditation", "HALT stage=")):
                        break
                    if "OTA_BOOT_VALID result=ESP_OK" in text:
                        break
            except (serial.SerialException, OSError) as exc:
                capture_error = str(exc)
            finally:
                connection.close()
            text = re.sub(r"(?i)\b[0-9a-f]{2}(?::[0-9a-f]{2}){5}\b", "[redacted]", received.decode("utf-8", "replace"))
            path = args.output / f"cycle-{cycle}.log"
            path.write_text(text)
            passed = capture_error is None and all(marker in text for marker in (
                "CHIP_USB_UART_RESET", "P4_CONSOLE_OS READY board=m5stack-tab5",
                "OTA_BOOT_VALID result=ESP_OK", f"P4CART_READY valid={args.expected_script_carts} rejected=0")) and not any(
                marker in text for marker in ("FATAL_HOLD", "Guru Meditation", "HALT stage="))
            report = {"cycle": cycle, "passed": passed, "seconds": round(time.monotonic() - started, 3),
                      "log": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                      "i2c_retries": text.count("I2C_INIT_RETRY"),
                      "capture_error": capture_error}
            reports.append(report)
            print(json.dumps(report), flush=True)
            if not passed:
                raise RuntimeError("warm boot acceptance failed; inspect the preserved log")
    finally:
        (args.output / "receipt.json").write_text(json.dumps({"unit": args.unit,
            "port": args.port, "kind": "native-usb-port-open-warm-boot", "cycles": reports}, indent=2) + "\n")

if __name__ == "__main__":
    main()
