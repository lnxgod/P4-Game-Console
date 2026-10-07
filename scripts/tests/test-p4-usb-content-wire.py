#!/usr/bin/env python3
"""USB framing and native-port startup regression coverage."""
import importlib.util
from pathlib import Path
import sys
import unittest
import tempfile
import hashlib
from unittest.mock import patch, MagicMock

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
    def test_boot_and_resume_audit_survives_fragmentation(self):
        reader=wire.WireReader(Port([]))
        trace=b'boot P4_CONSOLE_OS READY board=m5stack-tab5\nP4_USB_CONTENT READY resume=1 reboot=0\n'
        for byte in trace: reader.observe(bytes([byte]))
        reader.observe(b'next file, same connection')
        self.assertEqual(reader.boot_ready_events,1)
        self.assertEqual(reader.resume_events,1)
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

class BatchTests(unittest.TestCase):
    def test_whole_bundle_uses_one_connection_and_reader(self):
        port=MagicMock();port.__enter__.return_value=port
        with patch.object(wire,'open_port',return_value=port) as opened, \
             patch.object(wire,'validate_content'), patch.object(wire,'install_content') as install:
            self.assertEqual(wire.main(['game-changers-ai','--port','test-port']),0)
            opened.assert_called_once_with('test-port')
            self.assertEqual(install.call_count,16)
            readers=[c.kwargs['reader'] for c in install.call_args_list]
            self.assertTrue(all(r is readers[0] for r in readers))
            self.assertTrue(all(c.kwargs['connection'] is port for c in install.call_args_list))
    def test_timeout_retries_file_once_after_idle_on_same_connection(self):
        port=MagicMock();port.__enter__.return_value=port
        with patch.object(wire,'open_port',return_value=port) as opened, \
             patch.object(wire,'validate_content'), \
             patch.object(wire,'wait_for_content_ready',return_value=True) as ready, \
             patch.object(wire,'install_content',side_effect=[wire.TransferTimeout('ack')]+[None]*16) as install:
            self.assertEqual(wire.main(['game-changers-ai','--port','test-port']),0)
            opened.assert_called_once_with('test-port');ready.assert_called_once()
            self.assertEqual(install.call_count,17)
            self.assertEqual(install.call_args_list[0],install.call_args_list[1])
    def test_timeout_stops_if_device_not_ready_or_retry_fails(self):
        for idle in [False,True]:
            port=MagicMock();port.__enter__.return_value=port
            with self.subTest(idle=idle), patch.object(wire,'open_port',return_value=port) as opened, \
                 patch.object(wire,'validate_content'), \
                 patch.object(wire,'wait_for_content_ready',return_value=idle), \
                 patch.object(wire,'install_content',side_effect=wire.TransferTimeout('ack')) as install:
                self.assertEqual(wire.main(['game-changers-ai','--port','test-port']),2)
                self.assertEqual(install.call_count,2 if idle else 1)
                opened.assert_called_once_with('test-port')
    def test_batch_preflight_failure_never_opens_usb(self):
        with patch.object(wire,'open_port') as opened, \
             patch.object(wire,'validate_content',side_effect=wire.TransferError('missing')):
            self.assertEqual(wire.main(['game-changers-ai','--port','test-port']),2)
            opened.assert_not_called()

class ArenaBundleTests(unittest.TestCase):
    def test_bundle_has_unique_wire_ids_and_three_separate_wads(self):
        entries = wire.ARENA_BUNDLE['files']
        self.assertEqual(len(entries), 16)
        self.assertEqual(len({f['usb_kind'] for f in entries}), 16)
        self.assertTrue(all(f['usb_kind'] > 4 for f in entries))
        self.assertEqual([f['filename'] for f in entries[:3]], ['FREEDOOM2.WAD', 'PUREHADES.WAD', 'DWANGO5.WAD'])
    def test_all_inputs_preflight_before_serial_and_notices_are_required(self):
        with patch.object(wire, 'validate_content') as validate, patch.object(wire.serial, 'Serial') as serial:
            files = wire.arena_inputs(Path('/base.wad'), Path('/pure'), Path('/dwango'))
            self.assertEqual(validate.call_count, 16)
            self.assertEqual(files[2][1], Path('/dwango/DWANGO5.WAD'))
            self.assertEqual(files[3][1], Path('/pure/licenses/Freedoom-0.13.0-COPYING.txt'))
            self.assertEqual(files[8][1], Path('/pure/music/tracklist.json'))
            self.assertEqual(files[-1][1].parent, Path('/dwango'))
            serial.assert_not_called()
            validate.side_effect = [None]*5 + [wire.TransferError('missing credits')]
            with self.assertRaises(wire.TransferError):
                wire.arena_inputs(Path('/base.wad'), Path('/pure'), Path('/dwango'))
            serial.assert_not_called()
    def test_exact_hash_rejects_same_size_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'notice.txt'
            path.write_bytes(b'credits')
            spec = wire.ContentSpec('test', 15, 7, hashlib.sha256(b'credits').hexdigest(), path)
            wire.validate_content(path, spec)
            path.write_bytes(b'corrupt')
            with self.assertRaises(wire.TransferError):
                wire.validate_content(path, spec)

if __name__ == '__main__':
    unittest.main()
