#!/usr/bin/env python3
"""Keep exact-unit, silicon, security and image gates without firmware snapshot reads."""
import importlib.util
import pathlib
import struct
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location('installer', pathlib.Path(__file__).resolve().parents[1] / 'flash-console-os-tab5.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

class Device:
    CHIP_NAME = 'ESP32-P4'
    secure_download_mode = False
    major = 1
    minor = 3
    size = 0x184046
    secure = False
    encrypted = False
    def read_mac(self): return (1, 2, 3, 4, 5, 6)
    def get_major_chip_version(self): return self.major
    def get_minor_chip_version(self): return self.minor
    def flash_id(self): return self.size
    def get_secure_boot_enabled(self): return self.secure
    def get_flash_encryption_enabled(self): return self.encrypted

class Gates(unittest.TestCase):
    def setUp(self):
        self.device = Device()
        self.device.read_flash = Mock(side_effect=AssertionError('Identity checks must not read firmware'))
        self.identity = installer.sha(bytes(self.device.read_mac()).hex().encode('ascii'))
    def test_matching_unit(self):
        live = installer.validate_live(self.device, self.identity)
        self.assertEqual(live, {'identity_sha256': self.identity, 'revision': 'v1.3',
                                'flash_bytes': 16777216})
        self.device.read_flash.assert_not_called()
    def test_wrong_identity(self):
        with self.assertRaisesRegex(ValueError, 'identity'):
            installer.validate_live(self.device, '0'*64)
        self.device.read_flash.assert_not_called()
    def test_unsafe_devices(self):
        for key, value in [('CHIP_NAME', 'ESP32-C6'), ('major', 3), ('size', 0x194046),
                           ('secure', True), ('encrypted', True), ('secure_download_mode', True)]:
            with self.subTest(key=key):
                d = Device();setattr(d, key, value)
                with self.assertRaises(ValueError): installer.validate_live(d, self.identity)
    def test_image_family(self):
        data=bytearray(32);data[0]=0xe9
        struct.pack_into('<H',data,12,18);struct.pack_into('<HH',data,15,100,199)
        installer.validate_image(data)
        struct.pack_into('<HH',data,15,300,399)
        with self.assertRaisesRegex(ValueError,'silicon'):installer.validate_image(data)
    def test_checksum_without_full_read(self):
        data=b'fixed app artifact'; device=Mock()
        device.flash_md5sum.return_value=installer.hashlib.md5(data).hexdigest()
        self.assertTrue(installer.flash_range_matches(device,0x20000,data,'device-checksum'))
        device.read_flash.assert_not_called()
        device.flash_md5sum.assert_called_once_with(0x20000,len(data))
        device.flash_md5sum.return_value='00'*16
        self.assertFalse(installer.flash_range_matches(device,0x20000,data,'device-checksum'))
    def test_optional_readback_and_failures(self):
        device=Mock();device.read_flash.return_value=b'app'
        self.assertTrue(installer.flash_range_matches(device,0x20000,b'app','full-readback'))
        device.read_flash.return_value=b'bad'
        self.assertFalse(installer.flash_range_matches(device,0x20000,b'app','full-readback'))
        device.flash_md5sum.side_effect=RuntimeError('checksum unsupported')
        with self.assertRaises(RuntimeError):
            installer.flash_range_matches(device,0x20000,b'app','device-checksum')
        with self.assertRaises(ValueError):
            installer.flash_range_matches(device,0x20000,b'app','none')
    def test_redaction(self):
        self.assertNotIn('01:02:03:04:05:06',installer.redact('MAC: 01:02:03:04:05:06'))

if __name__ == '__main__': unittest.main()
