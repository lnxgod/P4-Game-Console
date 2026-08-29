#!/usr/bin/env python3
"""Run the Mac-hosted LORD realm hub over H1 USB or BLE."""

from __future__ import annotations

import argparse
import signal
import secrets
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.p4_realm_hub.ble_link import BleRealmLink
from tools.p4_realm_hub.cartridge import lord_offer_from_p4g
from tools.p4_realm_hub.serial_link import SerialRealmLink
from tools.p4_realm_hub.store import RealmStore


def profile_port(value: str) -> tuple[str, str]:
    profile, separator, port = value.partition("=")
    if not separator or not profile.strip() or not port.strip():
        raise argparse.ArgumentTypeError("USB link must be PROFILE=/dev/cu.PORT")
    return profile_name(profile), port.strip()


def profile_name(value: str) -> str:
    normalized = value.strip()
    if not normalized or len(normalized.encode("utf-8")) > 64:
        raise argparse.ArgumentTypeError("profile must contain 1..64 UTF-8 bytes")
    return normalized


def validate_bindings(
    usb: list[tuple[str, str]],
    ble_profile: str | None,
    adoption_profiles: list[str],
) -> list[str]:
    profiles = [profile for profile, _port in usb]
    if ble_profile is not None:
        profiles.append(ble_profile)
    duplicate_profiles = sorted(
        profile for profile in set(profiles) if profiles.count(profile) > 1
    )
    if duplicate_profiles:
        raise ValueError(
            "each PROFILE may be bound once: " + ", ".join(duplicate_profiles)
        )
    ports = [port for _profile, port in usb]
    duplicate_ports = sorted(port for port in set(ports) if ports.count(port) > 1)
    if duplicate_ports:
        raise ValueError(
            "each USB port may be bound once: " + ", ".join(duplicate_ports)
        )
    duplicate_adoptions = sorted(
        profile
        for profile in set(adoption_profiles)
        if adoption_profiles.count(profile) > 1
    )
    if duplicate_adoptions:
        raise ValueError(
            "each --adopt-local PROFILE may appear once: "
            + ", ".join(duplicate_adoptions)
        )
    unknown = sorted(set(adoption_profiles) - set(profiles))
    if unknown:
        raise ValueError(
            "--adopt-local must name exactly one configured link: "
            + ", ".join(unknown)
        )
    return profiles


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Host LORD P4MP rooms and their shared realm on this Mac"
    )
    result.add_argument(
        "--cartridge",
        default=str(
            ROOT
            / "apps/console_os/build-waveshare-landscape/sd-card/GAMES/LORD.P4G"
        ),
        help="exact LORD.P4G installed on every joining console",
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
    result.add_argument(
        "--ble-profile",
        type=profile_name,
        help="serve one BLE console profile",
    )
    result.add_argument(
        "--adopt-local",
        action="append",
        default=[],
        type=profile_name,
        metavar="PROFILE",
        help="authorize this configured profile's one-time local-save adoption",
    )
    result.add_argument(
        "--migrate-legacy-artifacts",
        action="store_true",
        help=(
            "after taking a database backup, explicitly classify and repair "
            "legacy multiplayer artifacts; ordinary restarts fail closed"
        ),
    )
    return result


def main() -> int:
    argument_parser = parser()
    arguments = argument_parser.parse_args()
    if not arguments.usb and not arguments.ble_profile:
        argument_parser.error("provide at least one --usb or --ble-profile")
    try:
        profiles = validate_bindings(
            arguments.usb,
            arguments.ble_profile,
            arguments.adopt_local,
        )
    except ValueError as error:
        argument_parser.error(str(error))
    offer = lord_offer_from_p4g(
        arguments.cartridge, session_seed=secrets.randbits(64) or 1
    )
    store = RealmStore(
        arguments.database,
        migrate_legacy_artifacts=arguments.migrate_legacy_artifacts,
    )
    for profile in profiles:
        store.actor_for_profile(profile)
    for profile in arguments.adopt_local:
        try:
            store.authorize_local_adoption(profile)
        except ValueError as error:
            argument_parser.error(str(error))
    links = [
        SerialRealmLink(profile, port, store, offer, baudrate=arguments.baud)
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
            BleRealmLink(arguments.ble_profile, store, offer).run(stop)
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
