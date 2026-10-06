#!/usr/bin/env python3
import argparse
import contextlib
import io
from unittest import mock
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
        for kind, suffix in ((tool.CLASS_P4G, "P4G"), (tool.CLASS_P4R, "P4R")):
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

    def native(self):
        payload = b"native fixture payload"
        header = bytearray(256); header[:8] = b"P4GAME1\0"
        struct.pack_into("<6I", header, 8, 256, 256+len(payload), 256, len(payload), 1, 1)
        header[48:80] = hashlib.sha256(payload).digest()
        header[80:93] = b"org.test.game"
        return header + payload

    def cart(self, root):
        path = root / "OLD.P4CART"
        path.write_bytes(b"P4CART1\0removed format marker")
        return path

    def test_retired_cart_upload_rejects_class_extension_and_renamed_magic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); cart = self.cart(root)
            renamed = root / "archive.bin"; renamed.write_bytes(cart.read_bytes())
            for path, kind in ((cart, tool.CLASS_P4CART), (cart, tool.CLASS_EXCHANGE),
                               (renamed, tool.CLASS_EXCHANGE), (renamed, tool.CLASS_P4G)):
                with self.subTest(path=path, kind=kind), self.assertRaisesRegex(tool.TransferError, "retired"):
                    tool.validate_upload(path, kind)
            self.assertEqual(cart.read_bytes(), renamed.read_bytes())

    def test_push_rejects_retired_cart_before_opening_serial(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); cart = self.cart(root)
            renamed = root / "archive.bin"; renamed.write_bytes(cart.read_bytes())
            harmless = root / "note.txt"; harmless.write_text("not a cartridge")
            for source, kind, remote in ((cart,"p4cart",None),(cart,"exchange",None),
                                        (renamed,"exchange",None),(harmless,"exchange","OLD.P4CART")):
                args = argparse.Namespace(input=source,file_class=kind,remote_name=remote,
                    no_replace=False,port="must-not-open",done_timeout=30)
                with self.subTest(kind=kind,source=source), mock.patch.object(tool,"open_port") as opened:
                    with self.assertRaisesRegex(tool.TransferError,"retired"):
                        tool.push(args)
                    opened.assert_not_called()
            with mock.patch.object(tool,"open_port") as opened, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as rejected:
                    tool.main(["push",str(cart),"--class","p4cart","--port","must-not-open"])
                self.assertEqual(rejected.exception.code,2); opened.assert_not_called()

    def test_removed_download_class_and_inspector_cannot_open_serial(self):
        self.assertFalse(hasattr(tool,"validate_cart_host"))
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/"old.bin"
            args=argparse.Namespace(file_class="p4cart",remote_name="OLD.P4CART",output=output,
                                    replace=False,port="must-not-open")
            with mock.patch.object(tool,"open_port") as opened:
                with self.assertRaisesRegex(tool.TransferError,"retired"):tool.pull(args)
                opened.assert_not_called()
            with self.assertRaisesRegex(tool.TransferError,"retired"):
                tool.checked_remote_name("OLD.P4CART",tool.CLASS_P4CART)
            self.assertFalse(output.exists())

    def test_remove_preserves_retired_cartridges_without_device_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            cart = self.cart(Path(temporary)); before = cart.read_bytes()
            with mock.patch.object(tool,"open_port") as opened:
                with self.assertRaisesRegex(tool.TransferError,"retired"):
                    tool.remove_files(argparse.Namespace(input=[cart],port="must-not-open"))
                opened.assert_not_called()
            self.assertEqual(cart.read_bytes(),before)

    def test_bundle_rejects_mixed_or_legacy_only_cart_before_any_transfer(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); (root/"GAMES").mkdir(); (root/"P4/GAMES").mkdir(parents=True)
            cart=self.cart(root); cart.rename(root/"P4/GAMES/old.p4cart")
            native=root/"GAMES/NEW.P4G"
            for mixed in (False,True):
                if mixed: native.write_bytes(self.native())
                with mock.patch.object(tool,"open_port") as opened, mock.patch.object(tool,"push") as pushed:
                    with self.assertRaisesRegex(tool.TransferError,"retired"):
                        tool.push_bundle(argparse.Namespace(input=root,port="must-not-open",no_replace=False,done_timeout=30))
                    opened.assert_not_called(); pushed.assert_not_called()
            self.assertTrue((root/"P4/GAMES/old.p4cart").exists())

    def test_native_bundle_prevalidates_and_preserves_resource_first_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); (root/"GAMES").mkdir(); (root/"P4/GAMES").mkdir(parents=True)
            (root/"GAMES/GAME.P4G").write_bytes(self.native())
            resource=root/"GAMES/GAME.P4R"; resource.write_bytes(self.resource())
            args=argparse.Namespace(input=root,port="fixture",no_replace=False,done_timeout=30)
            with mock.patch.object(tool,"open_port") as opened, mock.patch.object(tool,"push") as pushed:
                tool.push_bundle(args)
                opened.assert_called_once_with("fixture")
                self.assertEqual([call.args[0].file_class for call in pushed.call_args_list],["p4r","p4g"])
            resource.write_bytes(b"damaged resource")
            with mock.patch.object(tool,"open_port") as opened, mock.patch.object(tool,"push") as pushed:
                with self.assertRaises(tool.TransferError): tool.push_bundle(args)
                opened.assert_not_called(); pushed.assert_not_called()

if __name__ == "__main__":
    unittest.main()
