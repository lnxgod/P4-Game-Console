#!/usr/bin/env python3
"""Core2 app-only installation needs live device evidence, never a backup."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import zlib


ESPTOOL = SimpleNamespace(
    bin_image=SimpleNamespace(LoadFirmwareImage=Mock()),
    detect_chip=Mock(), main=Mock())
SERIAL = SimpleNamespace(Serial=Mock())
SPEC = importlib.util.spec_from_file_location(
    'dice_installer', Path(__file__).resolve().parents[1] / 'install-dice-core2.py')
installer = importlib.util.module_from_spec(SPEC)
with patch.dict(sys.modules, esptool=ESPTOOL, serial=SERIAL):
    SPEC.loader.exec_module(installer)


def partition(kind, subtype, offset, size, label):
    return struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size,
                       label.encode().ljust(16, b'\0'), 0)


def ota_record(sequence=1, state=2):
    raw = struct.pack('<I20sI', sequence, b'\xff' * 20, state)
    return raw + struct.pack('<I', zlib.crc32(raw[:4], 0xffffffff))


class Core2Installation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mac = (1, 2, 3, 4, 5, 6)
        self.profile = {
            'board': 'm5stack-core2', 'chip': 'esp32', 'chip_revision': 301,
            'device_identity_sha256': installer.sha(bytes(self.mac).hex().encode()),
            'flash_bytes': 16777216, 'app_offset': 0x10000,
            'app_partition_bytes': 0x640000,
        }
        self.profile_path = self.root / 'hardware/boards/m5stack-core2-dice.json'
        self.profile_path.parent.mkdir(parents=True)
        self.write_profile()
        self.artifact = self.root / 'apps/dice_core2/build-core2/p4_dice_core2.bin'
        self.artifact.parent.mkdir(parents=True)
        self.image = b'candidate ESP32 application'
        self.artifact.write_bytes(self.image)
        self.artifact.with_name('artifact.json').write_text(json.dumps({
            'sha256': installer.sha(self.image), 'bytes': len(self.image)}))
        self.table = (partition(1, 2, 0x9000, 0x5000, 'nvs') +
                      partition(1, 0, 0xe000, 0x2000, 'otadata') +
                      partition(0, 0x10, 0x10000, 0x640000, 'app0') +
                      partition(0, 0x11, 0x650000, 0x640000, 'app1'))
        self.table = self.table.ljust(0x1000, b'\xff')
        self.ota = {0xe000: ota_record(), 0xf000: b'\xff' * 32}
        self.esp = SimpleNamespace(
            CHIP_NAME='ESP32', secure_download_mode=False,
            get_chip_revision=Mock(return_value=301),
            read_mac=Mock(return_value=self.mac),
            get_secure_boot_enabled=Mock(return_value=False),
            get_flash_encryption_enabled=Mock(return_value=False),
            change_baud=Mock(), flash_id=Mock(return_value=0x184046),
            read_flash=Mock(side_effect=self.read_flash),
            flash_md5sum=Mock(side_effect=self.checksum),
            hard_reset=Mock(), _port=SimpleNamespace(close=Mock()),
        )
        self.esp.run_stub = Mock(return_value=self.esp)
        ESPTOOL.detect_chip.reset_mock()
        ESPTOOL.detect_chip.return_value = self.esp
        ESPTOOL.main.reset_mock()
        ESPTOOL.bin_image.LoadFirmwareImage.reset_mock()
        ESPTOOL.bin_image.LoadFirmwareImage.return_value = SimpleNamespace(chip_id=0)
        self.addCleanup(patch.stopall)
        patch.object(installer, 'ROOT', self.root).start()

    def write_profile(self):
        self.profile_path.write_text(json.dumps(self.profile))

    def read_flash(self, offset, size):
        if (offset, size) == (0x8000, 0x1000):
            return self.table
        if size == 32 and offset in self.ota:
            return self.ota[offset]
        raise AssertionError(f'Unexpected flash transfer: {offset:#x} + {size:#x}')

    def checksum(self, offset, size):
        if (offset, size) == (0, 0x10000):
            return '12' * 16
        if (offset, size) == (self.profile['app_offset'], len(self.image)):
            return hashlib.md5(self.image).hexdigest()
        raise AssertionError(f'Unexpected checksum range: {offset:#x} + {size:#x}')

    def run_main(self, install=False):
        argv = ['install-dice-core2.py', '--port', '/dev/fake']
        if install:
            argv.append('--install')
            SERIAL.Serial.return_value = SimpleNamespace(
                open=Mock(), read=Mock(return_value=b''), close=Mock())
            patch.object(installer.time, 'monotonic', side_effect=[0, 16]).start()
        output = io.StringIO()
        with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(output):
            installer.main()
        return output.getvalue()

    def assert_rejected(self, message):
        with self.assertRaisesRegex((RuntimeError, SystemExit), message):
            self.run_main(install=True)
        ESPTOOL.main.assert_not_called()

    def test_no_backup_manifest_image_or_profile_hash_required(self):
        output = self.run_main()
        self.assertIn('"installed": false', output)
        self.assertFalse((self.root / 'hardware/backups').exists())
        ESPTOOL.main.assert_not_called()

    def test_install_without_backup_preserves_prefix_and_uses_checksum(self):
        output = self.run_main(install=True)
        ESPTOOL.main.assert_called_once()
        self.assertIs(ESPTOOL.main.call_args.kwargs['esp'], self.esp)
        args = ESPTOOL.main.call_args.args[0]
        self.assertEqual(args[-2:], ['0x10000', str(self.artifact)])
        self.assertIn('"verification_method": "device-checksum"', output)
        self.assertIn('"boot_partition_nvs_prefix_preserved": true', output)
        self.assertNotIn('factory_backup_sha256', output)
        self.assertFalse((self.root / 'hardware/backups').exists())
        self.assertEqual(self.esp.flash_md5sum.call_args_list[0].args, (0, 0x10000))
        self.assertEqual(self.esp.flash_md5sum.call_args_list[-1].args, (0, 0x10000))

    def test_unused_stale_backup_material_does_not_block(self):
        backups = self.root / 'hardware/backups'
        backups.mkdir()
        (backups / 'manifest.json').write_text('broken historical manifest')
        (backups / 'old-firmware.bin').write_bytes(b'broken historical firmware')
        self.run_main()
        self.assertEqual((backups / 'manifest.json').read_text(), 'broken historical manifest')
        self.assertEqual((backups / 'old-firmware.bin').read_bytes(), b'broken historical firmware')

    def test_wrong_identity_chip_revision_and_flash_capacity_rejected(self):
        cases = [('identity', 'Device', lambda: self.esp.read_mac.configure_mock(return_value=(6,5,4,3,2,1))),
                 ('chip', 'chip/revision', lambda: setattr(self.esp, 'CHIP_NAME', 'ESP32-P4')),
                 ('revision', 'chip/revision', lambda: self.esp.get_chip_revision.configure_mock(return_value=300)),
                 ('capacity', 'flash size', lambda: self.esp.flash_id.configure_mock(return_value=0x174046))]
        for _, message, change in cases:
            with self.subTest(case=_):
                change()
                self.assert_rejected(message)
                self.esp.CHIP_NAME = 'ESP32'
                self.esp.read_mac.return_value = self.mac
                self.esp.get_chip_revision.return_value = 301
                self.esp.flash_id.return_value = 0x184046

    def test_flash_security_states_rejected(self):
        for key in ('secure_download_mode', 'get_secure_boot_enabled', 'get_flash_encryption_enabled'):
            with self.subTest(key=key):
                if key == 'secure_download_mode':
                    setattr(self.esp, key, True)
                else:
                    getattr(self.esp, key).return_value = True
                self.assert_rejected('security state')
                if key == 'secure_download_mode':
                    setattr(self.esp, key, False)
                else:
                    getattr(self.esp, key).return_value = False

    def test_wrong_app0_layout_rejected(self):
        self.table = self.table.replace(struct.pack('<I', 0x640000), struct.pack('<I', 0x630000), 1)
        self.assert_rejected('app0')

    def test_overlapping_data_partition_and_out_of_bounds_app_rejected(self):
        original = self.table
        self.table = self.table.replace(struct.pack('<I', 0x5000), struct.pack('<I', 0x9000), 1)
        self.assert_rejected('layout')
        self.table = original.replace(struct.pack('<I', 0x650000), struct.pack('<I', 0xc00000), 1)
        self.assert_rejected('layout')

    def test_valid_partition_table_md5_marker_accepted(self):
        entries = self.table[:128]
        marker = b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(entries).digest()
        self.table = (entries + marker).ljust(0x1000, b'\xff')
        self.run_main()

    def test_malformed_partition_entry_rejected(self):
        self.table = self.table[:128] + b'\x01\x02' + self.table[130:]
        self.assert_rejected('Invalid live partition entry')

    def test_invalid_partition_md5_marker_or_digest_rejected(self):
        entries = self.table[:128]
        marker = b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(entries).digest()
        for malformed, message in ((marker[:2] + b'\0' + marker[3:], 'checksum marker'),
                                   (marker[:16] + b'\0' * 16, 'table checksum differs')):
            with self.subTest(message=message):
                self.table = (entries + malformed).ljust(0x1000, b'\xff')
                self.assert_rejected(message)

    def test_missing_partition_table_terminator_rejected(self):
        entries = self.table[:128]
        filler = b''.join(partition(1, 0x81, 0xc90000 + i * 0x1000, 0x1000, 'data')
                          for i in range(124))
        self.table = entries + filler
        self.assertEqual(len(self.table), 0x1000)
        self.assert_rejected('no terminator')

    def test_active_app1_and_invalid_ota_crc_rejected(self):
        for value in (ota_record(sequence=2), ota_record()[:-1] + b'\0'):
            with self.subTest(value=value.hex()):
                self.ota[0xe000] = value
                self.assert_rejected('app0')

    def test_artifact_hash_size_and_chip_family_rejected(self):
        self.artifact.write_bytes(b'changed image')
        self.assert_rejected('hash/size')
        self.artifact.write_bytes(self.image)
        ESPTOOL.bin_image.LoadFirmwareImage.return_value = SimpleNamespace(chip_id=18)
        self.assert_rejected('not ESP32')

    def test_checksum_failure_and_prefix_change_are_reported(self):
        self.esp.flash_md5sum.side_effect = ['12' * 16, '00' * 16]
        with self.assertRaisesRegex(RuntimeError, 'checksum'):
            self.run_main(install=True)
        self.esp.flash_md5sum.side_effect = ['12' * 16, hashlib.md5(self.image).hexdigest(), '34' * 16]
        with self.assertRaisesRegex(RuntimeError, 'prefix changed'):
            self.run_main(install=True)


if __name__ == '__main__':
    unittest.main()
