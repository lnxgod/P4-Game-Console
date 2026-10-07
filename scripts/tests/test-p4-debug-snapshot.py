#!/usr/bin/env python3
"""Snapshot client corruption/retry/image tests without a serial device."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("debug_tests", HERE / "test-p4-debug-control.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
tool = base.tool


def response(request, data=b"", *, width=40, height=30, result=0, offset=None):
    capture, sequence, requested_offset = struct.unpack_from("<III", request, 8)
    frame = bytearray(32)
    struct.pack_into("<4sBBBBIIIIHHHBB", frame, 0, b"P4T1", 1, request[5], result, 0,
                     capture, sequence, requested_offset if offset is None else offset,
                     width * height * 2, len(data), width, height, 1, 1)
    frame.extend(data)
    frame.extend(struct.pack("<I", zlib.crc32(frame)))
    return bytes(frame)


class SnapshotServer:
    def __init__(self, *, drop_begin=False, drop_read=False, bad_offset=False):
        self.data = bytes((i * 17) & 255 for i in range(2400))
        self.drop_begin, self.drop_read = drop_begin, drop_read
        self.bad_offset = bad_offset
        self.captures = 0
        self.id = None
        self.ended = False

    def __call__(self, request, _count):
        if request[:4] != b"P4S1":
            return base.reply(request)
        self_id = struct.unpack_from("<I", request, 8)[0]
        if request[5] == tool.SNAP_BEGIN:
            if self.id is None:
                self.captures += 1
                self.id = self_id
            if self.drop_begin:
                self.drop_begin = False
                return b"lost begin reply\n"
            return b"capture ready\n" + response(request)
        if request[5] == tool.SNAP_END:
            self.ended = True
            return response(request)
        offset, length = struct.unpack_from("<IH", request, 16)
        result = response(request, self.data[offset:offset + length],
                          offset=offset + 1 if self.bad_offset else offset)
        if self.drop_read:
            self.drop_read = False
            return result[:-1] + bytes([result[-1] ^ 1])
        return b"read progress\n" + result


class QuarterServer:
    """Fake wire peer: source sample centers survive unchanged in native order."""
    def __init__(self, *, drop_begin=False, drop_read=False, legacy=False,
                 oversized=False, change_dimensions=False, dimensions=(180, 320)):
        self.width, self.height = (181, 320) if oversized else dimensions
        self.data = b"".join(struct.pack("<H", self.pixel(4*x+2, 4*y+2))
                             for y in range(self.height) for x in range(self.width))
        self.drop_begin, self.drop_read = drop_begin, drop_read
        self.legacy, self.change_dimensions = legacy, change_dimensions
        self.id, self.captures, self.ended = None, 0, False

    @staticmethod
    def pixel(x, y):
        return ((x & 31) << 11) | ((y & 63) << 5) | ((x+y) & 31)

    def __call__(self, request, _count):
        if request[:4] != b"P4S1":
            return base.reply(request)
        assert zlib.crc32(request[:28]) == struct.unpack_from("<I", request, 28)[0]
        assert request[7] == 0 and request[22:28] == bytes(6)
        command = request[5]
        if command == tool.SNAP_BEGIN:
            assert request[6] == 1
            if self.legacy:
                return response(request, width=0, height=0, result=1)
            if self.id is None:
                self.id = struct.unpack_from("<I", request, 8)[0]
                self.captures += 1
            if self.drop_begin:
                self.drop_begin = False
                return b"lost reduced begin\n"
            return response(request, width=self.width, height=self.height)
        assert request[6] == 0
        if command == tool.SNAP_END:
            self.ended = True
            return response(request, width=0, height=0)
        offset, length = struct.unpack_from("<IH", request, 16)
        width, height = ((self.height, self.width) if self.change_dimensions
                         else (self.width, self.height))
        result = response(request, self.data[offset:offset+length], width=width, height=height)
        if self.drop_read:
            self.drop_read = False
            return result[:-1] + bytes([result[-1] ^ 1])
        return result


class SnapshotTests(unittest.TestCase):
    def test_client_retry_demux_and_persistent_port(self):
        server = SnapshotServer(drop_begin=True, drop_read=True)
        serial = base.FakeSerial(server, read_size=17, write_size=7)
        raw, noise = io.BytesIO(), bytearray()
        with tool.DebugClient(serial, raw_log=raw, console=noise.extend, timeout=0.02) as client:
            capture = client.capture()
            self.assertEqual(capture["data"], server.data)
            self.assertEqual(capture["bytes"], 2400)
            self.assertEqual((capture["width"], capture["height"]), (30, 40))
            self.assertEqual(client.status()["text"], "shell multiplayer")
        self.assertEqual(server.captures, 1)
        self.assertTrue(server.ended)
        self.assertEqual(serial.requests[0], serial.requests[1])
        self.assertEqual(serial.requests[2], serial.requests[3])
        self.assertEqual(len(serial.reader_threads), 1)
        self.assertFalse(serial.closed)
        self.assertIn(b"capture ready\n", noise)
        self.assertIn(b"P4T1", raw.getvalue())

    def test_mismatched_offset_fails_and_releases(self):
        server = SnapshotServer(bad_offset=True)
        with tool.DebugClient(base.FakeSerial(server), timeout=0.02) as client:
            with self.assertRaises(tool.DebugError):
                client.capture()
        self.assertTrue(server.ended)

    def test_parser_recovers_from_oversize_and_bad_crc(self):
        request = tool.make_snapshot_request(tool.SNAP_READ, 1, 1, 0, 2)
        good = response(request, b"\x00\xf8")
        large = bytearray(good[:32])
        struct.pack_into("<H", large, 24, 65535)
        bad = good[:-1] + bytes([good[-1] ^ 1])
        scanner = tool.ResponseScanner()
        frames = []
        for value in large + bad + good:
            frames.extend(scanner.feed(bytes([value])))
            self.assertLessEqual(len(scanner.buffer), 2084)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["data"], b"\x00\xf8")
        self.assertEqual(scanner.bad_frames, 2)

    def test_malformed_requests_and_metadata(self):
        for args in ((0, 1, 1), (1, 0, 1), (1, 1, 0), (2, 1, 1, 0, 0),
                     (2, 1, 1, 0, 2049), (1, 1, 1, 1, 0)):
            with self.assertRaises(ValueError):
                tool.make_snapshot_request(*args)
        request = tool.make_snapshot_request(tool.SNAP_BEGIN, 1, 1)
        for offset, value in ((4, 2), (5, 4), (6, 7), (7, 1), (30, 2), (31, 0)):
            frame = bytearray(response(request))
            frame[offset] = value
            struct.pack_into("<I", frame, len(frame)-4, zlib.crc32(frame[:-4]))
            with self.assertRaises(tool.DebugError):
                tool.parse_snapshot_response(frame)

    def test_png_rotation_and_rgb565_primary_colors(self):
        # Native rows: red green / blue white / black yellow. CW90 must be
        # black blue red / yellow white green in the logical 3x2 image.
        capture = {"native_width": 2, "native_height": 3,
                   "data": struct.pack("<6H", 0xf800, 0x07e0, 0x001f, 0xffff, 0, 0xffe0)}
        png = tool.snapshot_png(capture)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset, compressed = 8, bytearray()
        while offset < len(png):
            size = struct.unpack_from(">I", png, offset)[0]
            kind, data = png[offset+4:offset+8], png[offset+8:offset+8+size]
            self.assertEqual(struct.unpack_from(">I", png, offset+8+size)[0], zlib.crc32(kind+data))
            if kind == b"IHDR":
                self.assertEqual(struct.unpack_from(">II", data), (3, 2))
            if kind == b"IDAT":
                compressed.extend(data)
            offset += size + 12
        self.assertEqual(zlib.decompress(compressed), bytes((
            0, 0,0,0, 0,0,255, 255,0,0,
            0, 255,255,0, 255,255,255, 0,255,0)))

    def test_screenshot_receipt_and_json_dispatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "menu.png"
            with tool.DebugClient(base.FakeSerial(SnapshotServer())) as client:
                receipt = tool.execute(client, {"command": "screenshot", "path": str(path)})
            self.assertNotIn("data", receipt)
            self.assertEqual(receipt["path"], str(path.resolve()))
            self.assertTrue(path.read_bytes().startswith(b"\x89PNG"))


class QuarterSnapshotTests(unittest.TestCase):
    def test_default_wire_is_unchanged_and_quarter_flag_is_begin_only(self):
        expected = bytearray(32)
        struct.pack_into("<4sBBBBIIIH", expected, 0, b"P4S1", 1, tool.SNAP_BEGIN,
                         0, 0, 7, 9, 0, 0)
        struct.pack_into("<I", expected, 28, zlib.crc32(expected[:28]))
        self.assertEqual(tool.make_snapshot_request(tool.SNAP_BEGIN, 7, 9), expected)
        self.assertEqual(tool.make_snapshot_request(tool.SNAP_BEGIN, 7, 9, scale=1), expected)
        quarter = tool.make_snapshot_request(tool.SNAP_BEGIN, 7, 9, scale=4)
        expected[6] = 1
        struct.pack_into("<I", expected, 28, zlib.crc32(expected[:28]))
        self.assertEqual(quarter, expected)
        for command, length in ((tool.SNAP_READ, 2), (tool.SNAP_END, 0)):
            with self.assertRaises(ValueError):
                tool.make_snapshot_request(command, 7, 9, length=length, scale=4)

    def test_invalid_scale_never_reaches_transport(self):
        serial = base.FakeSerial()
        with tool.DebugClient(serial) as client, tempfile.TemporaryDirectory() as directory:
            for scale in (0, 2, 3, 5, -4, True, False, "4", 4.0, None):
                with self.subTest(scale=scale):
                    with self.assertRaises(ValueError):
                        tool.make_snapshot_request(tool.SNAP_BEGIN, 1, 1, scale=scale)
                    with self.assertRaises(ValueError):
                        client.capture(scale=scale)
                    path = Path(directory) / "invalid.png"
                    with self.assertRaises(ValueError):
                        client.screenshot(path, scale=scale)
                    with self.assertRaises(ValueError):
                        tool.execute(client, {"command": "screenshot", "path": str(path), "scale": scale})
                    self.assertFalse(path.exists())
            self.assertEqual(serial.requests, [])

    def test_quarter_transfer_bound_metadata_and_tail(self):
        server = QuarterServer()
        serial = base.FakeSerial(server, read_size=97, write_size=11)
        with tool.DebugClient(serial) as client:
            capture = client.capture(scale=4)
            self.assertEqual(client.status()["result"], "ok")
        self.assertEqual(capture["data"], server.data)
        self.assertEqual(capture["bytes"], 115200)
        self.assertEqual((capture["native_width"], capture["native_height"]), (180, 320))
        self.assertEqual((capture["width"], capture["height"], capture["rotation"], capture["scale"]), (320, 180, 1, 4))
        reads = [request for request in serial.requests if request[:4] == b"P4S1" and request[5] == tool.SNAP_READ]
        self.assertEqual(len(reads), 57)
        self.assertEqual([struct.unpack_from("<IH", request, 16) for request in reads],
                         [(offset, min(2048, 115200-offset)) for offset in range(0, 115200, 2048)])
        self.assertEqual(server.captures, 1)
        self.assertTrue(server.ended)
        self.assertEqual(len(serial.reader_threads), 1)

    def test_quarter_retry_preserves_identical_begin_flag_and_read(self):
        server = QuarterServer(drop_begin=True, drop_read=True)
        serial = base.FakeSerial(server)
        with tool.DebugClient(serial, timeout=0.02) as client:
            self.assertEqual(client.capture(scale=4)["data"], server.data)
        self.assertEqual(serial.requests[0], serial.requests[1])
        self.assertEqual(serial.requests[0][6], 1)
        self.assertEqual(serial.requests[2], serial.requests[3])
        self.assertEqual(serial.requests[2][6], 0)
        self.assertEqual(server.captures, 1)
        self.assertTrue(server.ended)

    def test_quarter_dimensions_come_from_response_metadata(self):
        server = QuarterServer(dimensions=(10, 8))
        with tool.DebugClient(base.FakeSerial(server)) as client:
            capture = client.capture(scale=4)
        self.assertEqual((capture["native_width"], capture["native_height"]), (10, 8))
        self.assertEqual((capture["width"], capture["height"], capture["bytes"]), (8, 10, 160))
        self.assertEqual(capture["data"], server.data)

    def test_quarter_oversize_rejected_before_read_and_released(self):
        server = QuarterServer(oversized=True)
        serial = base.FakeSerial(server)
        with tool.DebugClient(serial) as client:
            with self.assertRaisesRegex(tool.DebugError, "quarter snapshot exceeds"):
                client.capture(scale=4)
        self.assertEqual([request[5] for request in serial.requests], [tool.SNAP_BEGIN, tool.SNAP_END])
        self.assertTrue(server.ended)

    def test_old_firmware_rejects_flag_without_full_size_fallback(self):
        server = QuarterServer(legacy=True)
        serial = base.FakeSerial(server)
        with tool.DebugClient(serial) as client:
            with self.assertRaisesRegex(tool.DebugError, "snapshot: invalid"):
                client.capture(scale=4)
        self.assertEqual([request[5] for request in serial.requests], [tool.SNAP_BEGIN, tool.SNAP_END])
        self.assertEqual(server.captures, 0)
        self.assertTrue(server.ended)

    def test_quarter_immutable_metadata_still_required(self):
        server = QuarterServer(change_dimensions=True)
        with tool.DebugClient(base.FakeSerial(server)) as client:
            with self.assertRaisesRegex(tool.DebugError, "immutable capture"):
                client.capture(scale=4)
        self.assertTrue(server.ended)

    def test_quarter_json_png_rotation_and_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "quarter.png"
            with tool.DebugClient(base.FakeSerial(QuarterServer())) as client:
                receipt = tool.execute(client, {"command": "screenshot", "path": str(path), "scale": 4})
            self.assertNotIn("data", receipt)
            self.assertEqual(receipt["scale"], 4)
            self.assertEqual(receipt["bytes"], 115200)
            png = path.read_bytes()
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset, compressed = 8, bytearray()
        while offset < len(png):
            size = struct.unpack_from(">I", png, offset)[0]
            kind, data = png[offset+4:offset+8], png[offset+8:offset+8+size]
            self.assertEqual(struct.unpack_from(">I", png, offset+8+size)[0], zlib.crc32(kind+data))
            if kind == b"IHDR":
                self.assertEqual(struct.unpack_from(">II", data), (320, 180))
            if kind == b"IDAT":
                compressed.extend(data)
            offset += size + 12
        pixels = zlib.decompress(compressed)
        self.assertEqual(len(pixels), 180*(1+320*3))
        for x, y in ((0, 0), (319, 0), (0, 179), (319, 179), (147, 91)):
            value = QuarterServer.pixel(4*y+2, 4*(319-x)+2)
            r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
            expected = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
            start = y*(1+320*3) + 1 + x*3
            self.assertEqual(pixels[start:start+3], expected)

    def test_cli_selects_quarter_with_fake_transport(self):
        serial = base.FakeSerial(QuarterServer())
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(tool, "open_port", return_value=serial) as open_port, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            path = Path(directory) / "quarter.png"
            tool.main(["--port", "fake", "--log", str(Path(directory)/"serial.log"),
                       "screenshot", "--output", str(path), "--scale", "4"])
            self.assertTrue(path.exists())
            self.assertEqual(json.loads(output.getvalue())["response"]["scale"], 4)
            open_port.assert_called_once_with("fake")

    def test_invalid_cli_scale_rejected_before_port_open(self):
        with mock.patch.object(tool, "open_port") as open_port, contextlib.redirect_stderr(io.StringIO()):
            for extra in (("screenshot", "--output", "unused.png", "--scale", "2"),
                          ("status", "--scale", "4"), ("--interactive", "--scale", "4")):
                with self.subTest(extra=extra), self.assertRaises(SystemExit):
                    tool.main(["--port", "fake", "--log", "unused.log", *extra])
            open_port.assert_not_called()


if __name__ == "__main__":
    unittest.main()
