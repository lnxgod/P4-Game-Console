#!/usr/bin/env python3

"""Bounded, receive-only E1 Doom serial capture with reset lines held inactive."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import termios
import time


STATS_RE = re.compile(
    rb"P4_DOOM_EMBEDDED E1 VIDEO_STATS frames=(\d+) submits=(\d+) "
    rb"completions=(\d+) timeouts=(\d+) failures=(\d+) refreshes=(\d+) "
    rb"underruns=unavailable"
)
REJECT = (
    b"P4_DOOM_EMBEDDED E1 HALT stage=",
    b"P4_DOOM_EMBEDDED E1 EXIT_DARK",
    b"P4_DOOM_EMBEDDED E1 EXIT_DARK_FAIL",
    b"Guru Meditation Error",
    b"panic'ed",
    b"Task watchdog got triggered",
    b"Brownout detector was triggered",
    b"ESP-ROM:",
    b"rst:",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--raw", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--min-stats", type=int, default=2)
    args = parser.parse_args()
    if not 1.0 <= args.seconds <= 120.0:
        parser.error("--seconds must be in [1, 120]")
    if not 1 <= args.min_stats <= 20:
        parser.error("--min-stats must be in [1, 20]")
    if args.raw.resolve() == args.summary.resolve():
        parser.error("capture outputs must be distinct")
    if args.raw.exists() or args.summary.exists():
        parser.error("capture outputs must not already exist")
    return args


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    args = parse_args()
    try:
        import serial
    except ImportError as error:
        raise SystemExit("activate the pinned ESP-IDF environment first") from error

    device = serial.Serial(
        port=None,
        baudrate=115200,
        timeout=0.05,
        write_timeout=0.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
        exclusive=True,
    )
    device.port = args.port
    device.dtr = False
    device.rts = False
    opened_ns = time.time_ns()
    raw = bytearray()
    try:
        device.open()
        attributes = termios.tcgetattr(device.fileno())
        attributes[2] &= ~getattr(termios, "HUPCL", 0)
        termios.tcsetattr(device.fileno(), termios.TCSANOW, attributes)
        verify = termios.tcgetattr(device.fileno())
        if (getattr(termios, "HUPCL", 0) and verify[2] & termios.HUPCL) \
                or not device.exclusive or device.dtr or device.rts:
            raise RuntimeError("exclusive/DTR/RTS/HUPCL receive-only contract failed")
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            chunk = device.read(4096)
            if chunk:
                raw.extend(chunk)
                if len(raw) > 1024 * 1024:
                    raise RuntimeError("serial transcript exceeded 1 MiB bound")
    finally:
        device.close()
    closed_ns = time.time_ns()

    payload = bytes(raw)
    rejects = [marker.decode("ascii", "replace") for marker in REJECT if marker.lower() in payload.lower()]
    stats = [tuple(int(field) for field in match.groups()) for match in STATS_RE.finditer(payload)]
    stats_valid = all(
        frames % 300 == 0
        and submits == frames + 1
        and completions == submits
        and timeouts == 0
        and failures == 0
        and refreshes > 0
        for frames, submits, completions, timeouts, failures, refreshes in stats
    )
    monotonic = all(
        later[0] > earlier[0]
        and later[1] > earlier[1]
        and later[2] > earlier[2]
        and later[5] >= earlier[5]
        for earlier, later in zip(stats, stats[1:])
    )
    passed = len(stats) >= args.min_stats and stats_valid and monotonic and not rejects
    summary = {
        "schema": 1,
        "capture": "doom-embedded-e1-receive-only-no-reset",
        "serial_port": args.port,
        "baud": 115200,
        "serial_exclusive": True,
        "dtr": False,
        "rts": False,
        "hupcl": False,
        "transmitted_bytes": 0,
        "opened_wall_ns": opened_ns,
        "closed_wall_ns": closed_ns,
        "raw_bytes": len(payload),
        "raw_sha256": sha256(payload),
        "stats_count": len(stats),
        "stats": [
            {
                "frames": values[0],
                "submits": values[1],
                "completions": values[2],
                "timeouts": values[3],
                "failures": values[4],
                "refreshes": values[5],
            }
            for values in stats
        ],
        "stats_valid": stats_valid,
        "stats_monotonic": monotonic,
        "reject_markers": rejects,
        "result": "pass" if passed else "fail",
    }
    args.raw.parent.mkdir(parents=True, exist_ok=True)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.raw.write_bytes(payload)
    args.summary.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, separators=(",", ":")))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
