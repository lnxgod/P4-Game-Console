#!/usr/bin/env python3
"""USB framing and native-port startup regression coverage."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[1] / 'p4-usb-content.py'
spec = importlib.util.spec_from_file_location('usb_content_wire', path)
wire = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = wire
spec.loader.exec_module(wire)

class Port:
    def __init__(self, blocks):
        self.blocks = list(blocks)
        self.writes = []
        self.baudrate = 115200
    @property
    def in_waiting(self):
        return len(self.blocks[0]) if self.blocks else 0
    def read(self, count):
        return self.blocks.pop(0) if self.blocks else b''
    def write(self, data):
        self.writes.append(data)
        return len(data)

class WireTests(unittest.TestCase):
    def test_coalesced_ack_and_done_are_retained(self):
        ack = b'P4A1' + bytes(5)
        done = b'P4D1' + bytes(37)
        reader = wire.WireReader(Port([b'log\r\n' + ack + done]))
        self.assertEqual(reader.frame(b'P4A1', 9, 1), ack)
        self.assertEqual(reader.frame(b'P4D1', 41, 1), done)
    def test_reboot_retries_manifest_once_when_service_starts(self):
        ready = b'P4R1' + bytes(7)
        port = Port([b'boot\r\nP4_USB_CON', b'TENT READY native\r\n', ready])
        self.assertEqual(wire.WireReader(port).frame(b'P4R1', 11, 1, b'manifest'), ready)
        self.assertEqual(port.writes, [b'manifest'])
    def test_live_service_reply_does_not_resend_manifest(self):
        ready = b'P4R1' + bytes(7)
        port = Port([ready])
        wire.WireReader(port).frame(b'P4R1', 11, 1, b'manifest')
        self.assertEqual(port.writes, [])
    def test_deferred_wad_scan_does_not_hide_successful_reboot(self):
        port = Port([b'CONTENT_VALIDATION_DEFERRED\r\nP4_USB_CONTENT READY\r\n'])
        self.assertTrue(wire.wait_for_content_ready(port, wire.WireReader(port), 1))

if __name__ == '__main__':
    unittest.main()
