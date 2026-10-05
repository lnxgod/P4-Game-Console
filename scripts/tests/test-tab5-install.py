#!/usr/bin/env python3
"""Reject wrong devices, silicon, flash capacity, security and image families."""
import importlib.util
import pathlib
import struct
import unittest

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
        self.identity = installer.sha(bytes(self.device.read_mac()).hex().encode('ascii'))
    def test_matching_unit(self):
        self.assertEqual(installer.validate_live(self.device, self.identity)['revision'], 'v1.3')
    def test_wrong_identity(self):
        with self.assertRaisesRegex(ValueError, 'identity'):
            installer.validate_live(self.device, '0'*64)
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
    def test_redaction(self):
        self.assertNotIn('01:02:03:04:05:06',installer.redact('MAC: 01:02:03:04:05:06'))

if __name__ == '__main__': unittest.main()
