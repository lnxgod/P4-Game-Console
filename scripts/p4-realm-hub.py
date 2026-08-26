#!/usr/bin/env python3
"""Run the Mac-hosted LORD realm hub over H1 USB or BLE."""

from __future__ import annotations

import argparse
import asyncio
import signal
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.p4_realm_hub.ble_link import BleRealmLink
from tools.p4_realm_hub.serial_link import SerialRealmLink
from tools.p4_realm_hub.store import RealmStore


def profile_port(value: str) -> tuple[str, str]:
    profile, separator, port = value.partition("=")
    if not separator or not profile.strip() or not port.strip():
        raise argparse.ArgumentTypeError("USB link must be PROFILE=/dev/cu.PORT")
    return profile.strip(), port.strip()


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Join a Console OS LORD P4MP room and host its realm on this Mac"
    )
    result.add_argument(
        "--database",
        default=str(ROOT / "local-data/realm/lord.sqlite3"),
        help="SQLite realm database (default: local-data/realm/lord.sqlite3)",
    )
    result.add_argument(
        "--usb",
        action="append",
        default=[],
        type=profile_port,
        metavar="PROFILE=PORT",
        help="serve one H1 CH343 serial console; may be repeated",
    )
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--ble-profile", help="serve one BLE console profile")
    result.add_argument("--ble-room", type=lambda value: int(value, 0))
    return result


def main() -> int:
    arguments = parser().parse_args()
    if not arguments.usb and not arguments.ble_profile:
        parser().error("provide at least one --usb or --ble-profile")
    store = RealmStore(arguments.database)
    links = [
        SerialRealmLink(profile, port, store, baudrate=arguments.baud)
        for profile, port in arguments.usb
    ]
    threads = [threading.Thread(target=link.run, daemon=True) for link in links]
    stop = threading.Event()

    def request_stop(_signum, _frame) -> None:
        stop.set()
        for link in links:
            link.stop()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    for thread in threads:
        thread.start()
    try:
        if arguments.ble_profile:
            asyncio.run(
                BleRealmLink(
                    arguments.ble_profile,
                    store,
                    room_session_id=arguments.ble_room,
                ).run()
            )
        else:
            while not stop.wait(0.25):
                if any(not thread.is_alive() for thread in threads):
                    raise RuntimeError("an H1 realm worker stopped")
    finally:
        request_stop(0, None)
        for thread in threads:
            thread.join(timeout=2.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
