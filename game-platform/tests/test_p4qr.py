#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


GAME_PLATFORM = Path(__file__).resolve().parents[1]
P4CART_SCRIPT = GAME_PLATFORM / "scripts" / "p4cart.py"
P4QR_SCRIPT = GAME_PLATFORM / "scripts" / "p4qr.py"
TEMPLATE = GAME_PLATFORM / "templates" / "bounce-lab"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


p4cart = load_module("test_p4cart_module", P4CART_SCRIPT)
p4qr = load_module("test_p4qr_module", P4QR_SCRIPT)


class P4QrTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        shutil.copytree(TEMPLATE, self.source)
        self.cart = self.root / "bounce-lab.p4cart"
        p4cart.pack_source(self.source, self.cart)

    def tearDown(self):
        self.temporary.cleanup()

    def test_base45_round_trip_and_strict_errors(self):
        for length in range(1, 260):
            source = bytes((index * 73 + length) & 0xFF for index in range(length))
            self.assertEqual(
                p4qr.base45_decode(p4qr.base45_encode(source)),
                source,
            )
        with self.assertRaisesRegex(p4qr.QrTransferError, "invalid length"):
            p4qr.base45_decode("A")
        with self.assertRaisesRegex(p4qr.QrTransferError, "unsupported character"):
            p4qr.base45_decode("a0")

    def test_out_of_order_and_duplicate_frames_rebuild_exact_cart(self):
        original = self.cart.read_bytes()
        envelope = p4qr.build_envelope(original)
        frames = p4qr.split_envelope(envelope, 256)
        scanned = list(reversed(frames)) + [frames[0]]
        self.assertGreater(len(frames), 1)
        self.assertEqual(p4qr.assemble_frames(scanned), original)

    def test_written_frame_set_joins_and_revalidates_p4cart(self):
        frame_directory = self.root / "frames"
        summary = p4qr.write_frame_set(self.cart, frame_directory, 384)
        self.assertEqual(summary["qr_codes"], len(list(frame_directory.glob("*.p4qr"))))
        self.assertEqual(summary["game"]["title"], "Bounce Lab")

        inspection = p4qr.inspect_frame_set(frame_directory)
        self.assertTrue(inspection["complete"])
        self.assertEqual(inspection["qr_codes"], summary["qr_codes"])

        rebuilt = self.root / "rebuilt.p4cart"
        joined = p4qr.join_frame_set(frame_directory, rebuilt)
        self.assertEqual(joined["cart_sha256"], summary["cart_sha256"])
        self.assertEqual(rebuilt.read_bytes(), self.cart.read_bytes())
        manifest, _, _ = p4cart.inspect_cart(rebuilt)
        self.assertEqual(manifest["game"]["id"], joined["game"]["id"])

    def test_missing_corrupt_and_mixed_frames_are_rejected(self):
        first_envelope = p4qr.build_envelope(self.cart.read_bytes())
        first = p4qr.split_envelope(first_envelope, 256)
        with self.assertRaisesRegex(p4qr.QrTransferError, "missing frame"):
            p4qr.assemble_frames(first[:-1])

        fields = first[0].split(":", 5)
        replacement = "0" if fields[5][-1] != "0" else "1"
        fields[5] = fields[5][:-1] + replacement
        damaged = ":".join(fields)
        with self.assertRaisesRegex(p4qr.QrTransferError, "CRC-32"):
            p4qr.parse_frame(damaged)

        changed_cart = bytearray(self.cart.read_bytes())
        changed_cart[-1] ^= 0x01
        second = p4qr.split_envelope(p4qr.build_envelope(bytes(changed_cart)), 256)
        with self.assertRaisesRegex(p4qr.QrTransferError, "different transfer sets"):
            p4qr.collect_frames([first[0], second[0]])

    def test_estimate_reports_all_profiles_without_mutating_cart(self):
        original = self.cart.read_bytes()
        summary = p4qr.estimate_cart(self.cart)
        self.assertEqual(
            [entry["profile"] for entry in summary["profiles"]],
            ["easy", "balanced", "dense"],
        )
        self.assertGreater(summary["cart_bytes"], summary["compressed_payload_bytes"])
        self.assertGreaterEqual(summary["profiles"][0]["qr_codes"], 1)
        self.assertEqual(self.cart.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
