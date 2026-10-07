#!/usr/bin/env python3
"""Drive Tab5 touch/buttons over native USB while continuously preserving logs."""
import argparse
from collections import deque
import contextlib
import hashlib
import json
import os
from pathlib import Path
import secrets
import struct
import sys
import threading
import time
import zlib

STATUS, OPEN, INPUT, RELEASE, CLOSE = range(1, 6)
COMMANDS = {STATUS: "status", OPEN: "open", INPUT: "input", RELEASE: "release", CLOSE: "close"}
RESULTS = {0: "ok", 1: "invalid", 2: "busy", 3: "session", 4: "sequence"}
BUTTONS = {"up": 1, "down": 2, "left": 4, "right": 8,
           "a": 16, "b": 32, "start": 64, "back": 128}
WIDTH, HEIGHT = 1280, 720
SNAP_BEGIN, SNAP_READ, SNAP_END = 1, 2, 3
SNAP_RESULTS = {0: "ok", 1: "invalid", 2: "busy", 3: "expired", 4: "unavailable", 5: "failed"}
SNAP_MAX_BYTES, SNAP_CHUNK_BYTES = WIDTH * HEIGHT * 2, 2048
SNAP_QUARTER_BYTES = SNAP_MAX_BYTES // 16


class DebugError(RuntimeError):
    """Transport, protocol or remote command failure."""


class RemoteError(DebugError):
    def __init__(self, response):
        self.response = response
        super().__init__(f"{response['command']}: {response['result']} ({response['text']})")


def integer(value, name, low, high):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in {low}..{high}")
    return value


def button_mask(value):
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError:
            names = value.lower().replace("+", ",").split(",")
            if not names or any(name.strip() not in BUTTONS for name in names):
                raise ValueError("buttons must be a mask or names: " + ", ".join(BUTTONS)) from None
            value = 0
            for name in names:
                value |= BUTTONS[name.strip()]
    return integer(value, "buttons", 0, 255)


def make_request(command, session, sequence, *, buttons=0, x=0, y=0,
                 hold_ms=0, touch=False):
    integer(command, "command", STATUS, CLOSE)
    integer(session, "session", 0, 0xffffffff)
    integer(sequence, "sequence", 0, 0xffffffff)
    integer(buttons, "buttons", 0, 255)
    integer(x, "x", 0, WIDTH - 1)
    integer(y, "y", 0, HEIGHT - 1)
    if not isinstance(touch, bool):
        raise ValueError("touch must be a boolean")
    if command == INPUT:
        integer(hold_ms, "hold_ms", 1, 1000)
        if not touch and (x or y):
            raise ValueError("coordinates require touch=true")
    elif buttons or x or y or hold_ms or touch:
        raise ValueError("only INPUT carries touch, buttons or hold_ms")
    if command != STATUS and (not session or not sequence):
        raise ValueError("session and sequence must be nonzero")
    frame = bytearray(32)
    struct.pack_into("<4sBBBBIIIHHH", frame, 0, b"P4D1", 1, command, int(touch), 0,
                     session, sequence, buttons, x, y, hold_ms)
    struct.pack_into("<I", frame, 28, zlib.crc32(frame[:28]))
    return bytes(frame)


def parse_response(frame):
    if len(frame) != 128 or frame[:4] != b"P4E1":
        raise DebugError("invalid response length or magic")
    if zlib.crc32(frame[:124]) != struct.unpack_from("<I", frame, 124)[0]:
        raise DebugError("invalid response CRC")
    version, command, result, flags = frame[4:8]
    if version != 1 or command not in COMMANDS or result not in RESULTS or flags & ~3:
        raise DebugError("invalid response header")
    session, sequence, buttons, x, y, session_ms, input_ms = struct.unpack_from("<IIIHHII", frame, 8)
    if buttons & ~255 or x >= WIDTH or y >= HEIGHT or session_ms > 30000 or input_ms > 1000:
        raise DebugError("invalid response state")
    text = frame[32:124]
    if b"\0" not in text:
        raise DebugError("unterminated response text")
    return {"command": COMMANDS[command], "command_id": command,
            "result": RESULTS[result], "result_id": result,
            "session": session, "sequence": sequence,
            "active": bool(flags & 1), "touch": bool(flags & 2),
            "buttons": buttons, "x": x, "y": y,
            "session_remaining_ms": session_ms, "input_remaining_ms": input_ms,
            "text": text.split(b"\0", 1)[0].decode("utf-8", "replace")}


def snapshot_scale(value):
    integer(value, "snapshot scale", 1, 4)
    if value not in (1, 4):
        raise ValueError("snapshot scale must be 1 or 4")
    return value


def make_snapshot_request(command, capture_id, sequence, offset=0, length=0, *, scale=1):
    integer(command, "snapshot command", SNAP_BEGIN, SNAP_END)
    integer(capture_id, "capture_id", 1, 0xffffffff)
    integer(sequence, "sequence", 1, 0xffffffff)
    integer(offset, "offset", 0, SNAP_MAX_BYTES - 1)
    integer(length, "length", 0, SNAP_CHUNK_BYTES)
    snapshot_scale(scale)
    if command != SNAP_BEGIN and scale != 1:
        raise ValueError("only snapshot BEGIN carries a scale flag")
    if (command == SNAP_READ and not length) or (command != SNAP_READ and (offset or length)):
        raise ValueError("only snapshot READ carries a nonzero length and offset")
    frame = bytearray(32)
    struct.pack_into("<4sBBBBIIIH", frame, 0, b"P4S1", 1, command, int(scale == 4), 0,
                     capture_id, sequence, offset, length)
    struct.pack_into("<I", frame, 28, zlib.crc32(frame[:28]))
    return bytes(frame)


def parse_snapshot_response(frame):
    if len(frame) < 36 or frame[:4] != b"P4T1":
        raise DebugError("invalid snapshot response")
    version, command, result, reserved = frame[4:8]
    capture_id, sequence, offset, total, length, width, height, pixel_format, rotation = struct.unpack_from("<IIIIHHHBB", frame, 8)
    if (version != 1 or command not in (SNAP_BEGIN, SNAP_READ, SNAP_END) or
            result not in SNAP_RESULTS or reserved or not capture_id or not sequence or
            length > SNAP_CHUNK_BYTES or len(frame) != 36 + length or
            total > SNAP_MAX_BYTES or pixel_format != 1 or rotation != 1):
        raise DebugError("invalid snapshot header")
    if zlib.crc32(frame[:-4]) != struct.unpack_from("<I", frame, len(frame) - 4)[0]:
        raise DebugError("invalid snapshot CRC")
    if result == 0 and command != SNAP_END:
        if not width or not height or total != width * height * 2:
            raise DebugError("invalid snapshot dimensions")
        if command == SNAP_READ and (not length or offset >= total or length > total - offset):
            raise DebugError("invalid snapshot payload bounds")
    if (result or command != SNAP_READ) and length:
        raise DebugError("unexpected snapshot payload")
    return {"protocol": "snapshot", "command_id": command, "capture_id": capture_id,
            "sequence": sequence, "result_id": result, "result": SNAP_RESULTS[result],
            "offset": offset, "bytes": total, "native_width": width, "native_height": height,
            "format": "rgb565-le", "rotation": rotation, "data": bytes(frame[32:-4])}


def snapshot_png(capture):
    """Encode native RGB565 scanout as a clockwise-rotated RGB PNG, using stdlib."""
    width, height = capture["native_height"], capture["native_width"]
    data = capture["data"]
    if len(data) != width * height * 2:
        raise DebugError("snapshot length does not match image dimensions")
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: none
        for x in range(width):
            offset = ((width - 1 - x) * height + y) * 2
            pixel = data[offset] | (data[offset + 1] << 8)
            r, g, b = pixel >> 11, (pixel >> 5) & 63, pixel & 31
            rows.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class ResponseScanner:
    """Bounded framing amid arbitrary firmware logs, including split magic/CRC."""
    def __init__(self, noise=None):
        self.buffer = bytearray()
        self.noise = noise or (lambda data: None)
        self.bad_frames = 0

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            starts = [value for magic in (b"P4E1", b"P4T1") if (value := self.buffer.find(magic)) >= 0]
            start = min(starts) if starts else -1
            if start < 0:
                # Retain only the longest suffix which might become a magic.
                keep = next((n for n in (3, 2, 1) if any(self.buffer.endswith(magic[:n]) for magic in (b"P4E1", b"P4T1"))), 0)
                count = len(self.buffer) - keep
                self.noise(bytes(self.buffer[:count]))
                del self.buffer[:count]
                break
            if start:
                self.noise(bytes(self.buffer[:start]))
                del self.buffer[:start]
            snapshot = self.buffer[:4] == b"P4T1"
            if snapshot and len(self.buffer) < 32:
                break
            size = 36 + struct.unpack_from("<H", self.buffer, 24)[0] if snapshot else 128
            if snapshot and size > 36 + SNAP_CHUNK_BYTES:
                self.bad_frames += 1
                self.noise(bytes(self.buffer[:1]))
                del self.buffer[:1]
                continue
            if len(self.buffer) < size:
                break
            try:
                frame = (parse_snapshot_response if snapshot else parse_response)(bytes(self.buffer[:size]))
            except DebugError:
                self.bad_frames += 1
                self.noise(bytes(self.buffer[:1]))
                del self.buffer[:1]
                continue
            del self.buffer[:size]
            frames.append(frame)
        return frames


class DebugClient:
    """One reader owns an already-open, bounded-timeout serial connection.

    raw_log is an optional binary file-like object. console receives log bytes
    outside response frames. Neither logs nor pending input are ever discarded.
    The caller owns the transport unless close_transport=True.
    """
    def __init__(self, transport, *, raw_log=None, console=None, timeout=1.5,
                 retries=2, close_transport=False):
        if timeout <= 0 or retries < 0:
            raise ValueError("timeout must be positive and retries nonnegative")
        self.transport, self.raw_log = transport, raw_log
        self.timeout, self.retries = timeout, retries
        self.close_transport = close_transport
        self.scanner = ResponseScanner(console)
        self._condition = threading.Condition()
        self._commands = threading.RLock()
        self._responses = deque(maxlen=64)
        self._stop = threading.Event()
        self._reader_error = None
        self._closed = False
        self._session = 0
        self._sequence = 0
        self._lease_deadline = 0.0
        self._reader = threading.Thread(target=self._read, name="p4-debug-rx", daemon=True)
        self._reader.start()

    def _read(self):
        try:
            while not self._stop.is_set():
                data = self.transport.read(4096)
                if not data:
                    continue
                if self.raw_log is not None:
                    self.raw_log.write(data)
                    self.raw_log.flush()
                frames = self.scanner.feed(data)
                if frames:
                    with self._condition:
                        self._responses.extend(frames)
                        self._condition.notify_all()
        except Exception as exc:
            with self._condition:
                self._reader_error = exc
                self._condition.notify_all()

    def _next_sequence(self):
        if self._sequence == 0xffffffff:
            raise DebugError("sequence exhausted; close this client and open a new session")
        self._sequence += 1
        return self._sequence

    def _exchange(self, command, session, **payload):
        if self._closed:
            raise DebugError("client is closed")
        sequence = self._next_sequence()
        request = make_request(command, session, sequence, **payload)
        key = (command, session, sequence)
        for _attempt in range(self.retries + 1):
            with self._condition:
                if self._reader_error:
                    raise DebugError(f"serial reader failed: {self._reader_error}") from self._reader_error
            remaining = memoryview(request)
            while remaining:
                count = self.transport.write(remaining)
                if count is None or count <= 0 or count > len(remaining):
                    raise DebugError("serial write made no progress")
                remaining = remaining[count:]
            deadline = time.monotonic() + self.timeout
            with self._condition:
                while True:
                    for response in self._responses:
                        if response.get("protocol") == "snapshot":
                            continue
                        if (response["command_id"], response["session"], response["sequence"]) == key:
                            self._responses.remove(response)
                            if response["result_id"]:
                                if response["result_id"] == 3:
                                    self._session = 0
                                    self._lease_deadline = 0.0
                                raise RemoteError(response)
                            # Remaining time comes from the device, including a
                            # duplicate response which does not refresh a hold.
                            if command != STATUS and session == self._session:
                                self._lease_deadline = time.monotonic() + response["session_remaining_ms"] / 1000
                            return response
                    if self._reader_error:
                        raise DebugError(f"serial reader failed: {self._reader_error}") from self._reader_error
                    wait = deadline - time.monotonic()
                    if wait <= 0:
                        break
                    self._condition.wait(wait)
        raise TimeoutError(f"no {COMMANDS[command]} response after {self.retries + 1} identical requests")

    def status(self):
        """Read state without opening or extending a debug lease."""
        with self._commands:
            response = self._exchange(STATUS, 0)
            # STATUS echoes request session 0 even while a lease is active.
            # Only an explicit inactive receipt proves our cached lease ended
            # (for example after a device reboot). The next requested input
            # opens a fresh lease; no uncertain input is replayed here.
            if not response["active"] and response["session_remaining_ms"] == 0:
                self._session = 0
                self._lease_deadline = 0.0
            return response

    def _snapshot_exchange(self, command, capture_id, offset=0, length=0, *, scale=1):
        if self._closed:
            raise DebugError("client is closed")
        sequence = self._next_sequence()
        request = make_snapshot_request(command, capture_id, sequence, offset, length, scale=scale)
        for _attempt in range(self.retries + 1):
            remaining = memoryview(request)
            while remaining:
                count = self.transport.write(remaining)
                if count is None or count <= 0 or count > len(remaining):
                    raise DebugError("serial write made no progress")
                remaining = remaining[count:]
            deadline = time.monotonic() + self.timeout
            with self._condition:
                while True:
                    for response in self._responses:
                        if (response.get("protocol") == "snapshot" and
                                (response["command_id"], response["capture_id"], response["sequence"]) == (command, capture_id, sequence)):
                            self._responses.remove(response)
                            if response["result_id"]:
                                raise DebugError("snapshot: " + response["result"])
                            return response
                    if self._reader_error:
                        raise DebugError(f"serial reader failed: {self._reader_error}") from self._reader_error
                    wait = deadline - time.monotonic()
                    if wait <= 0:
                        break
                    self._condition.wait(wait)
        raise TimeoutError(f"no snapshot response after {self.retries + 1} identical requests")

    def capture(self, *, scale=1):
        """Read one immutable scanout, optionally at one quarter of each dimension."""
        snapshot_scale(scale)
        with self._commands:
            capture_id = secrets.randbits(32) or 1
            try:
                metadata = self._snapshot_exchange(SNAP_BEGIN, capture_id, scale=scale)
                if scale == 4 and metadata["bytes"] > SNAP_QUARTER_BYTES:
                    raise DebugError("quarter snapshot exceeds reduced image bound")
                data = bytearray()
                while len(data) < metadata["bytes"]:
                    offset = len(data)
                    length = min(SNAP_CHUNK_BYTES, metadata["bytes"] - offset)
                    response = self._snapshot_exchange(SNAP_READ, capture_id, offset, length)
                    if (response["offset"] != offset or len(response["data"]) != length or
                            any(response[key] != metadata[key] for key in ("bytes", "native_width", "native_height", "rotation"))):
                        raise DebugError("snapshot chunk does not match immutable capture")
                    data.extend(response["data"])
                return {"capture_id": capture_id, "native_width": metadata["native_width"],
                        "native_height": metadata["native_height"], "width": metadata["native_height"],
                        "height": metadata["native_width"], "format": "rgb565-le", "rotation": 1,
                        "scale": scale,
                        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "data": bytes(data)}
            finally:
                with contextlib.suppress(DebugError, TimeoutError, OSError):
                    self._snapshot_exchange(SNAP_END, capture_id)

    def screenshot(self, path, *, scale=1):
        """Save a PNG of device-submitted pixels; returns a JSON-safe receipt."""
        path = Path(path).expanduser().resolve()
        capture = self.capture(scale=scale)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(snapshot_png(capture))
        return {key: value for key, value in capture.items() if key != "data"} | {"path": str(path)}

    def open_session(self):
        with self._commands:
            if self._session and time.monotonic() < self._lease_deadline:
                response = self.status()
                if self._session:
                    return response
            session = secrets.randbits(32) or 1
            self._session = session
            # A lost OPEN response leaves an uncertain device outcome. Retain
            # this identity so cleanup/next input can address that same lease.
            self._lease_deadline = time.monotonic() + 30.0
            try:
                response = self._exchange(OPEN, session)
            except TimeoutError:
                raise
            except Exception:
                self._session = 0
                raise
            return response

    def hold(self, buttons=0, *, x=0, y=0, touch=False, hold_ms=200):
        """Set input for 1..1000 ms; a later hold replaces the current input."""
        mask = button_mask(buttons)
        # Validate before opening a session or writing anything to the device.
        make_request(INPUT, 1, 1, buttons=mask, x=x, y=y, touch=touch, hold_ms=hold_ms)
        with self._commands:
            if not self._session or time.monotonic() >= self._lease_deadline:
                self.open_session()
            return self._exchange(INPUT, self._session, buttons=mask, x=x, y=y,
                                  touch=touch, hold_ms=hold_ms)

    def tap(self, x, y, hold_ms=120):
        """Press, release, then allow the foreground input loop to sample up."""
        with self._commands:
            pressed = self.hold(x=x, y=y, touch=True, hold_ms=hold_ms)
            time.sleep(pressed["input_remaining_ms"] / 1000)
            released = self.release()
            # A USB reply acknowledges protocol state before the application
            # samples it. An immediate next press can overwrite the neutral
            # state and turn consecutive taps into a drag. Leave room for the
            # shell's input/render/network loop; loading still needs a separate
            # state/log check because a timed gap is not an application receipt.
            time.sleep(0.2)
            return released

    def release(self):
        with self._commands:
            if not self._session:
                return None
            return self._exchange(RELEASE, self._session)

    def close_session(self):
        with self._commands:
            if not self._session:
                return None
            try:
                return self._exchange(CLOSE, self._session)
            finally:
                self._session = 0
                self._lease_deadline = 0.0

    def shutdown(self):
        if self._closed:
            return
        try:
            if self._session:
                # Best effort while the reader still runs. Device TTLs protect
                # against an unplugged cable, lost response, or killed process.
                with contextlib.suppress(DebugError, TimeoutError, OSError):
                    self.release()
                with contextlib.suppress(DebugError, TimeoutError, OSError):
                    self.close_session()
        finally:
            self._closed = True
            self._stop.set()
            cancel = getattr(self.transport, "cancel_read", None)
            if cancel:
                with contextlib.suppress(OSError):
                    cancel()
            self._reader.join(timeout=2.0)
            if self.close_transport:
                self.transport.close()

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        self.shutdown()


def open_port(path):
    """Open exclusively with inactive modem lines; never flush received logs."""
    import serial
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.05,
                               write_timeout=2.0, exclusive=True,
                               dsrdtr=False, rtscts=False)
    connection.dtr = False
    connection.rts = False
    connection.port = path
    try:
        connection.open()
        if os.name == "posix":
            import termios
            attributes = termios.tcgetattr(connection.fileno())
            attributes[2] &= ~termios.HUPCL
            termios.tcsetattr(connection.fileno(), termios.TCSANOW, attributes)
        return connection
    except Exception:
        connection.close()
        raise


def execute(client, request):
    if not isinstance(request, dict):
        raise ValueError("command must be a JSON object")
    command = request.get("command")
    fields = {"status": set(), "open": set(), "release": set(), "close": set(),
              "screenshot": {"path", "scale"},
              "tap": {"x", "y", "hold_ms"},
              "hold": {"buttons", "x", "y", "touch", "hold_ms"},
              "buttons": {"buttons", "hold_ms"}}
    if not isinstance(command, str) or command not in fields:
        raise ValueError("unknown command; choose status/open/tap/hold/buttons/release/close/screenshot")
    extra = set(request) - fields[command] - {"command", "id"}
    if extra:
        raise ValueError("unexpected fields: " + ", ".join(sorted(extra)))
    if command in ("status", "open", "release", "close"):
        return getattr(client, {"open": "open_session", "close": "close_session"}.get(command, command))()
    if command == "screenshot":
        if not isinstance(request.get("path"), str) or not request["path"]:
            raise ValueError("screenshot requires a local output path")
        scale = snapshot_scale(request.get("scale", 1))
        return client.screenshot(request["path"], scale=scale)
    if command == "tap":
        if "x" not in request or "y" not in request:
            raise ValueError("tap requires x and y")
        return client.tap(request["x"], request["y"], request.get("hold_ms", 120))
    if command == "buttons" and "buttons" not in request:
        raise ValueError("buttons requires a buttons mask or names")
    return client.hold(request.get("buttons", 0), x=request.get("x", 0),
                       y=request.get("y", 0), touch=request.get("touch", False),
                       hold_ms=request.get("hold_ms", 200))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True, type=Path, help="append every received byte to this local binary log")
    parser.add_argument("--show-log", action="store_true", help="also print firmware log text to stderr")
    parser.add_argument("--interactive", action="store_true", help="read one JSON command per stdin line; stay connected until EOF")
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--retries", type=int, default=2)
    parser.add_argument("command", nargs="?", choices=("status", "tap", "buttons", "screenshot"), default="status")
    parser.add_argument("--output", type=Path, help="PNG output for screenshot")
    parser.add_argument("--scale", type=int, choices=(1, 4),
                        help="screenshot dimension divisor (default 1; 4 gives 320x180 on Tab5)")
    parser.add_argument("--x", type=int)
    parser.add_argument("--y", type=int)
    parser.add_argument("--buttons", default="0")
    parser.add_argument("--hold-ms", type=int, default=120)
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.retries < 0:
        parser.error("--timeout must be positive and --retries nonnegative")
    if not args.interactive and args.command == "tap" and (args.x is None or args.y is None):
        parser.error("tap requires --x and --y")
    if not args.interactive and args.command == "screenshot" and args.output is None:
        parser.error("screenshot requires --output")
    if args.scale is not None and (args.interactive or args.command != "screenshot"):
        parser.error("--scale requires a non-interactive screenshot command; use JSON scale interactively")
    request = {"command": args.command}
    if not args.interactive:
        try:
            if args.command == "tap":
                request.update(x=args.x, y=args.y, hold_ms=args.hold_ms)
                make_request(INPUT, 1, 1, x=args.x, y=args.y, touch=True, hold_ms=args.hold_ms)
            elif args.command == "buttons":
                mask = button_mask(args.buttons)
                request.update(buttons=mask, hold_ms=args.hold_ms)
                make_request(INPUT, 1, 1, buttons=mask, hold_ms=args.hold_ms)
            elif args.command == "screenshot":
                request["path"] = str(args.output)
                request["scale"] = args.scale if args.scale is not None else 1
        except ValueError as exc:
            parser.error(str(exc))
    args.log.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(args.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    os.fchmod(descriptor, 0o600)
    console = (lambda data: (sys.stderr.write(data.decode("utf-8", "replace")), sys.stderr.flush())) if args.show_log else None
    try:
        with os.fdopen(descriptor, "ab", buffering=0) as raw_log:
            with DebugClient(open_port(args.port), raw_log=raw_log, console=console,
                             timeout=args.timeout, retries=args.retries, close_transport=True) as client:
                if args.interactive:
                    print(json.dumps({"event": "connected", "port": args.port, "log": str(args.log)}), flush=True)
                    for line in sys.stdin:
                        request = None
                        try:
                            request = json.loads(line)
                            result = {"ok": True, "response": execute(client, request)}
                        except (ValueError, DebugError, TimeoutError, OSError) as exc:
                            result = {"ok": False, "error": str(exc)}
                        if isinstance(request, dict) and "id" in request:
                            result["id"] = request["id"]
                        print(json.dumps(result), flush=True)
                else:
                    response = execute(client, request)
                    if args.command == "buttons":
                        time.sleep(response["input_remaining_ms"] / 1000)
                    print(json.dumps({"ok": True, "response": response}), flush=True)
    except (DebugError, TimeoutError, OSError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
