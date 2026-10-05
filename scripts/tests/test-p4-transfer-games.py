#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import hashlib
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("transfer_games", ROOT / "scripts/p4-transfer.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

class GameTransferTests(unittest.TestCase):
    def resource(self):
        payload = b"original test pixels"
        header = bytearray(128)
        header[:8] = b"P4RES01\0"
        struct.pack_into("<6I", header, 8, 128, 128 + len(payload), 128, len(payload), 1, 0)
        header[32:64] = hashlib.sha256(payload).digest()
        name = b"org.test.game"
        header[64:64+len(name)] = name
        return header + payload

    def test_resource_validates_and_detects_payload_corruption(self):
        data = self.resource()
        tool.validate_resource_host(data)
        data[-1] ^= 1
        with self.assertRaisesRegex(tool.TransferError, "digest"):
            tool.validate_resource_host(data)

    def test_resource_rejects_reserved_bytes_and_bad_id(self):
        for offset in (112, 64):
            data = self.resource()
            data[offset] = 255
            with self.assertRaises(tool.TransferError):
                tool.validate_resource_host(data)

    def test_game_classes_reject_paths_and_wrong_extensions(self):
        for kind, suffix in ((tool.CLASS_P4G, "P4G"), (tool.CLASS_P4R, "P4R"), (tool.CLASS_P4CART, "P4CART")):
            self.assertEqual(tool.checked_remote_name("GAME." + suffix, kind), "GAME." + suffix)
            for name in ("../GAME." + suffix, "GAME.TXT", "a." + suffix, "A" * 40 + "." + suffix):
                with self.assertRaises(tool.TransferError):
                    tool.checked_remote_name(name, kind)

    def test_removal_requires_a_valid_exact_game_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "GAME.P4R"
            path.write_bytes(self.resource())
            source, kind, size, digest = tool.removal_input(path)
            self.assertEqual(kind, tool.CLASS_P4R)
            self.assertEqual(digest, hashlib.sha256(path.read_bytes()).digest())
            frame = tool.make_request(tool.DIRECTION_REMOVE, kind, source.name, size, digest)
            self.assertEqual(frame[4], 3)
            self.assertEqual(frame[12:44], digest)
            self.assertEqual(struct.unpack_from("<I", frame, 84)[0], tool.crc32(frame[:84]))
            for name in ("SAVE.DAT", "game.P4R", "BAD.P4G"):
                bad = Path(temporary) / name
                bad.write_bytes(b"not a game")
                with self.assertRaises(tool.TransferError):
                    tool.removal_input(bad)
            link = Path(temporary) / "LINK.P4R"
            link.symlink_to(path)
            with self.assertRaises(tool.TransferError):
                tool.removal_input(link)

    def test_lua_cart_uses_canonical_validator(self):
        spec = importlib.util.spec_from_file_location("p4cart_fixture", ROOT / "game-platform/scripts/p4cart.py")
        packer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packer)
        with tempfile.TemporaryDirectory() as temporary:
            valid = Path(temporary) / "GOOD.P4CART"
            packer.pack_source(ROOT / "game-platform/tests/fixtures/minimal-cart", valid)
            tool.validate_upload(valid, tool.CLASS_P4CART)
            bad = Path(temporary) / "BAD.P4CART"
            data = bytearray(valid.read_bytes())
            data[-1] ^= 1
            bad.write_bytes(data)
            with self.assertRaises(tool.TransferError):
                tool.validate_upload(bad, tool.CLASS_P4CART)

if __name__ == "__main__":
    unittest.main()
