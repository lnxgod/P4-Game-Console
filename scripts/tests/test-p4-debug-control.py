#!/usr/bin/env python3
"""USB debug host framing and persistent ownership tests; no physical port."""
import importlib.util
import io
from pathlib import Path
import struct
import threading
import time
from types import SimpleNamespace
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("p4_debug_control", ROOT / "scripts/p4-debug-control.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def reply(request, *, result=0, active=True, input_ms=0, text="shell multiplayer"):
    frame = bytearray(128)
    command = request[5]
    session, sequence, buttons, x, y = struct.unpack_from("<IIIHH", request, 8)
    struct.pack_into("<4sBBBBIIIHHII", frame, 0, b"P4E1", 1, command, result,
                     int(active) | (2 if request[6] else 0), session, sequence,
                     buttons, x, y, 30000 if active else 0, input_ms)
    encoded = text.encode()[:91]
    frame[32:32 + len(encoded)] = encoded
    struct.pack_into("<I", frame, 124, zlib.crc32(frame[:124]))
    return bytes(frame)


class FakeSerial:
    def __init__(self, responder=None, read_size=4096, write_size=32):
        self.condition = threading.Condition()
        self.rx = bytearray()
        self.tx = bytearray()
        self.requests = []
        self.closed = False
        self.read_size, self.write_size = read_size, write_size
        self.responder = responder or (lambda request, count: reply(request, active=request[5] != tool.CLOSE))
        self.reader_threads = set()

    def push(self, data):
        with self.condition:
            self.rx.extend(data)
            self.condition.notify_all()

    def read(self, count):
        with self.condition:
            self.reader_threads.add(threading.get_ident())
            if not self.rx:
                self.condition.wait(0.005)
            count = min(count, self.read_size)
            data = bytes(self.rx[:count])
            del self.rx[:count]
            return data

    def write(self, data):
        count = min(len(data), self.write_size)
        self.tx.extend(data[:count])
        while len(self.tx) >= 32:
            request = bytes(self.tx[:32])
            del self.tx[:32]
            self.requests.append(request)
            response = self.responder(request, len(self.requests))
            if response:
                self.push(response)
        return count

    def cancel_read(self):
        with self.condition:
            self.condition.notify_all()

    def close(self):
        self.closed = True


class SampledTouch:
    """Application input sampling lags behind immediate protocol replies."""
    def __init__(self, tick_ms):
        self.tick_us = tick_ms * 1000
        self.next_tick = self.tick_us
        self.now = 0
        self.touch = None
        self.was_down = False
        self.pressed = None
        self.clicks = []

    def hold(self, *, x, y, touch, hold_ms):
        assert touch
        self.touch = (x, y)
        return {"input_remaining_ms": hold_ms}

    def release(self):
        self.touch = None
        return {"result": "ok", "command": "release"}

    def sleep(self, seconds):
        end = self.now + round(seconds * 1_000_000)
        while self.next_tick <= end:
            if self.touch is None:
                if self.was_down and self.pressed is not None:
                    self.clicks.append(self.pressed)
                self.was_down = False
                self.pressed = None
            elif not self.was_down:
                self.was_down = True
                self.pressed = self.touch
            elif self.pressed != self.touch:
                # Moving to another control cancels the original press.
                self.pressed = None
            self.next_tick += self.tick_us
        self.now = end


class ProtocolTests(unittest.TestCase):
    def test_request_layout_and_crc(self):
        request = tool.make_request(tool.INPUT, 0x10203040, 19, buttons=17,
                                    x=1279, y=719, hold_ms=1000, touch=True)
        self.assertEqual(len(request), 32)
        self.assertEqual(request[:8], b"P4D1\x01\x03\x01\x00")
        self.assertEqual(struct.unpack_from("<IIIHHHH", request, 8),
                         (0x10203040, 19, 17, 1279, 719, 1000, 0))
        self.assertEqual(struct.unpack_from("<I", request, 28)[0], zlib.crc32(request[:28]))

    def test_invalid_input_never_reaches_transport(self):
        transport = FakeSerial()
        with tool.DebugClient(transport) as client:
            for payload in ({"x": 1280, "touch": True}, {"y": 720, "touch": True},
                            {"x": 1}, {"hold_ms": 0}, {"hold_ms": 1001},
                            {"buttons": 256}, {"buttons": True}, {"touch": 1},
                            {"buttons": "unknown"}, {"x": 1.2, "touch": True}):
                with self.subTest(payload=payload), self.assertRaises(ValueError):
                    client.hold(**payload)
            self.assertEqual(transport.requests, [])

    def test_noninput_payload_and_ids_rejected(self):
        for command, session, sequence, payload in (
                (tool.STATUS, 0, 0, {"hold_ms": 1}),
                (tool.OPEN, 0, 1, {}), (tool.INPUT, 1, 0, {"hold_ms": 1}),
                (9, 1, 1, {}), (tool.CLOSE, 1, 1, {"touch": True})):
            with self.assertRaises(ValueError):
                tool.make_request(command, session, sequence, **payload)

    def test_response_rejects_crc_header_and_unterminated_text(self):
        original = reply(tool.make_request(tool.STATUS, 0, 1))
        for offset, value in ((4, 2), (5, 9), (6, 5), (7, 128), (19, 128),
                              (23, 255), (27, 255), (31, 255)):
            frame = bytearray(original)
            frame[offset] = value
            struct.pack_into("<I", frame, 124, zlib.crc32(frame[:124]))
            with self.subTest(offset=offset), self.assertRaises(tool.DebugError):
                tool.parse_response(frame)
        frame = bytearray(original)
        frame[32:124] = b"x" * 92
        struct.pack_into("<I", frame, 124, zlib.crc32(frame[:124]))
        with self.assertRaises(tool.DebugError):
            tool.parse_response(frame)
        with self.assertRaises(tool.DebugError):
            tool.parse_response(original[:-1] + bytes([original[-1] ^ 1]))

    def test_scanner_fragments_noise_and_bad_crc_recover(self):
        request = tool.make_request(tool.STATUS, 0, 1)
        good = reply(request)
        corrupt = good[:-1] + bytes([good[-1] ^ 1])
        noise = bytearray()
        scanner = tool.ResponseScanner(noise.extend)
        stream = b"boot P4 P4E" + corrupt + b"\nready\n" + good + b"tail"
        responses = []
        for value in stream:
            responses.extend(scanner.feed(bytes([value])))
            self.assertLess(len(scanner.buffer), 128)
        self.assertEqual(len(responses), 1)
        self.assertEqual(responses[0]["sequence"], 1)
        self.assertEqual(scanner.bad_frames, 1)
        self.assertEqual(bytes(noise), b"boot P4 P4E" + corrupt + b"\nready\ntail")

    def test_button_names_and_mask(self):
        self.assertEqual(tool.button_mask("up+a"), 17)
        self.assertEqual(tool.button_mask("start, back"), 192)
        self.assertEqual(tool.button_mask("0x80"), 128)
        self.assertEqual(tool.button_mask("a+a"), 16)


class ClientTests(unittest.TestCase):
    def test_chained_taps_are_separate_application_clicks(self):
        for tick_ms in (17, 37, 83):
            with self.subTest(tick_ms=tick_ms):
                application = SampledTouch(tick_ms)
                # Exercise the production tap method while replacing only the
                # acknowledged wire state and elapsed time with a sampled app.
                client = object.__new__(tool.DebugClient)
                client._commands = threading.RLock()
                client.hold = application.hold
                client.release = application.release
                with mock.patch.object(tool.time, "sleep", side_effect=application.sleep):
                    first = client.tap(1204, 346, hold_ms=150)
                    second = client.tap(1080, 346, hold_ms=150)
                self.assertEqual(first["result"], "ok")
                self.assertEqual(second["result"], "ok")
                self.assertEqual(application.clicks, [(1204, 346), (1080, 346)])

    def test_status_is_read_only_and_context_does_not_open(self):
        transport = FakeSerial()
        with tool.DebugClient(transport) as client:
            self.assertEqual(client.status()["text"], "shell multiplayer")
            client.status()
        self.assertEqual([frame[5] for frame in transport.requests], [tool.STATUS, tool.STATUS])
        self.assertTrue(all(struct.unpack_from("<I", frame, 8)[0] == 0 for frame in transport.requests))
        self.assertEqual(len(transport.reader_threads), 1)
        self.assertFalse(transport.closed)

    def test_retry_is_identical_and_stale_response_ignored(self):
        def responder(request, count):
            if count == 1:
                stale = bytearray(request)
                struct.pack_into("<I", stale, 12, 999)
                return b"boot log\n" + reply(stale)
            return reply(request)
        transport = FakeSerial(responder, read_size=7, write_size=5)
        raw = io.BytesIO()
        with tool.DebugClient(transport, raw_log=raw, timeout=0.02, retries=1) as client:
            self.assertEqual(client.status()["sequence"], 1)
        self.assertEqual(len(transport.requests), 2)
        self.assertEqual(transport.requests[0], transport.requests[1])
        self.assertTrue(raw.getvalue().startswith(b"boot log\nP4E1"))

    def test_timeout_is_bounded(self):
        transport = FakeSerial(lambda _request, _count: None)
        with tool.DebugClient(transport, timeout=0.01, retries=2) as client:
            with self.assertRaises(TimeoutError):
                client.status()
        self.assertEqual(len(transport.requests), 3)
        self.assertEqual(len(set(transport.requests)), 1)

    def test_context_releases_closes_and_owns_transport(self):
        transport = FakeSerial(read_size=1)
        with tool.DebugClient(transport, close_transport=True) as client:
            client.hold("up+a", hold_ms=500)
        self.assertEqual([frame[5] for frame in transport.requests],
                         [tool.OPEN, tool.INPUT, tool.RELEASE, tool.CLOSE])
        sessions = {struct.unpack_from("<I", frame, 8)[0] for frame in transport.requests}
        self.assertEqual(len(sessions), 1)
        self.assertNotIn(0, sessions)
        self.assertEqual([struct.unpack_from("<I", frame, 12)[0] for frame in transport.requests], [1, 2, 3, 4])
        self.assertTrue(transport.closed)

    def test_logs_continue_during_idle(self):
        event = threading.Event()
        noise = bytearray()
        def console(data):
            noise.extend(data)
            if b"idle evidence" in noise:
                event.set()
        transport = FakeSerial()
        raw = io.BytesIO()
        with tool.DebugClient(transport, raw_log=raw, console=console):
            transport.push(b"idle evidence\n")
            self.assertTrue(event.wait(0.5))
        self.assertEqual(raw.getvalue(), b"idle evidence\n")
        self.assertEqual(transport.requests, [])

    def test_expired_session_opens_fresh_id(self):
        transport = FakeSerial()
        with mock.patch.object(tool.secrets, "randbits", side_effect=[123, 456]):
            with tool.DebugClient(transport) as client:
                client.hold("a")
                client._lease_deadline = 0
                client.hold("b")
        opens = [struct.unpack_from("<I", frame, 8)[0] for frame in transport.requests if frame[5] == tool.OPEN]
        self.assertEqual(opens, [123, 456])

    def test_remote_session_error_clears_owned_session(self):
        def responder(request, count):
            return reply(request, result=3 if request[5] == tool.INPUT else 0)
        transport = FakeSerial(responder)
        with tool.DebugClient(transport) as client:
            with self.assertRaises(tool.RemoteError):
                client.hold("a")
            self.assertEqual(client._session, 0)
        self.assertEqual([frame[5] for frame in transport.requests], [tool.OPEN, tool.INPUT])

    def test_inactive_status_retires_rebooted_lease_without_input_replay(self):
        boot = {"rebooted": False}
        def responder(request, count):
            return reply(request, active=not (boot["rebooted"] and request[5] == tool.STATUS))
        transport = FakeSerial(responder)
        with mock.patch.object(tool.secrets, "randbits", side_effect=[123, 456]):
            with tool.DebugClient(transport) as client:
                client.hold("a")
                boot["rebooted"] = True
                status = client.status()
                self.assertFalse(status["active"])
                self.assertEqual((client._session, client._lease_deadline), (0, 0.0))
                self.assertEqual([f[5] for f in transport.requests], [tool.OPEN, tool.INPUT, tool.STATUS])
                client.hold("b")
                self.assertEqual([f[5] for f in transport.requests],
                                 [tool.OPEN, tool.INPUT, tool.STATUS, tool.OPEN, tool.INPUT])
                self.assertEqual(struct.unpack_from("<I", transport.requests[-1], 8)[0], 456)

    def test_active_status_session_zero_does_not_retire_or_extend_lease(self):
        transport = FakeSerial()
        with tool.DebugClient(transport) as client:
            client.hold("a")
            before = (client._session, client._lease_deadline)
            self.assertEqual(client.status()["session"], 0)
            self.assertEqual((client._session, client._lease_deadline), before)

    def test_stale_explicit_open_reopens_after_inactive_status(self):
        transport = FakeSerial(lambda request, count: reply(request, active=request[5] != tool.STATUS))
        with mock.patch.object(tool.secrets, "randbits", side_effect=[123, 456]):
            with tool.DebugClient(transport) as client:
                client.open_session()
                response = client.open_session()
                self.assertEqual(response["command_id"], tool.OPEN)
                self.assertEqual(client._session, 456)
                self.assertEqual([f[5] for f in transport.requests], [tool.OPEN, tool.STATUS, tool.OPEN])

    def test_status_timeout_does_not_retire_uncertain_lease_or_replay_input(self):
        transport = FakeSerial(lambda request, count: None if request[5] == tool.STATUS else reply(request))
        with tool.DebugClient(transport, timeout=0.01, retries=0) as client:
            client.hold("a")
            before = (client._session, client._lease_deadline)
            with self.assertRaises(TimeoutError):
                client.status()
            self.assertEqual((client._session, client._lease_deadline), before)
            self.assertEqual([f[5] for f in transport.requests], [tool.OPEN, tool.INPUT, tool.STATUS])

    def test_rejected_input_is_not_replayed_after_session_error(self):
        transport = FakeSerial(lambda request, count: reply(request, result=3 if request[5] == tool.INPUT else 0))
        with tool.DebugClient(transport) as client:
            with self.assertRaises(tool.RemoteError):
                client.hold("a")
            self.assertEqual([f[5] for f in transport.requests], [tool.OPEN, tool.INPUT])
            self.assertEqual(client._session, 0)

    def test_lost_open_retains_identity_for_cleanup(self):
        def responder(request, count):
            return None if request[5] == tool.OPEN else reply(request, active=request[5] != tool.CLOSE)
        transport = FakeSerial(responder)
        with tool.DebugClient(transport, timeout=0.01, retries=1) as client:
            with self.assertRaises(TimeoutError):
                client.open_session()
            self.assertNotEqual(client._session, 0)
        self.assertEqual([frame[5] for frame in transport.requests],
                         [tool.OPEN, tool.OPEN, tool.RELEASE, tool.CLOSE])
        self.assertEqual(len({struct.unpack_from("<I", frame, 8)[0] for frame in transport.requests}), 1)

    def test_open_port_sets_inactive_lines_and_preserves_input(self):
        connection = mock.Mock()
        connection.fileno.return_value = 42
        def verify_open():
            self.assertIs(connection.dtr, False)
            self.assertIs(connection.rts, False)
        connection.open.side_effect = verify_open
        serial_module = SimpleNamespace(Serial=mock.Mock(return_value=connection))
        import termios
        attributes = [0, 0, termios.HUPCL | termios.CS8, 0, 0, 0, []]
        with mock.patch.dict("sys.modules", {"serial": serial_module}), \
                mock.patch.object(termios, "tcgetattr", return_value=attributes), \
                mock.patch.object(termios, "tcsetattr") as configure:
            self.assertIs(tool.open_port("fake-port"), connection)
        self.assertTrue(serial_module.Serial.call_args.kwargs["exclusive"])
        self.assertEqual(configure.call_args.args[2][2] & termios.HUPCL, 0)
        connection.reset_input_buffer.assert_not_called()
        connection.flushInput.assert_not_called()

    def test_json_command_validation(self):
        client = mock.Mock()
        for request in ([], {"command": []}, {"command": "bogus"}, {"command": "tap", "x": 1},
                        {"command": "status", "buttons": 1}, {"command": "buttons"}):
            with self.assertRaises(ValueError):
                tool.execute(client, request)
        self.assertEqual(client.mock_calls, [])
        tool.execute(client, {"command": "hold", "buttons": "a", "hold_ms": 100, "id": "step1"})
        client.hold.assert_called_once_with("a", x=0, y=0, touch=False, hold_ms=100)


if __name__ == "__main__":
    unittest.main()
