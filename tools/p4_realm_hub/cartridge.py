"""Exact LORD P4G identity used by the backend-hosted P4MP lobby."""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path

from tools.p4_realm_hub import p4mp


P4G_HEADER_BYTES = 256
P4G_MAGIC = b"P4GAME1\0"
PROFILE_MAGIC = b"P4MP"
PROFILE_BYTES = 16
LORD_GAME_ID = "org.p4console.lord"
LORD_PROTOCOL = 0x4C53


def _c_string(value: bytes, label: str) -> str:
    if b"\0" not in value:
        raise ValueError(f"{label} is not terminated")
    text, padding = value.split(b"\0", 1)
    if not text or any(padding):
        raise ValueError(f"{label} has invalid padding")
    return text.decode("ascii")


def lord_offer_from_p4g(path: str | Path, *, session_seed: int) -> p4mp.Offer:
    """Validate the installed cartridge and derive Console OS lobby hashes."""
    package = Path(path).read_bytes()
    if not P4G_HEADER_BYTES < len(package) <= 512 * 1024:
        raise ValueError("LORD P4G size is invalid")
    if package[:8] != P4G_MAGIC:
        raise ValueError("LORD P4G magic is invalid")
    (
        header_bytes,
        package_bytes,
        payload_offset,
        payload_bytes,
        format_version,
        api_version,
        _launcher_id,
        required,
        optional,
        _accent,
        flags,
    ) = struct.unpack_from("<9IHH", package, 8)
    if (
        header_bytes != P4G_HEADER_BYTES
        or payload_offset != P4G_HEADER_BYTES
        or package_bytes != len(package)
        or payload_bytes != len(package) - payload_offset
        or format_version != 1
        or api_version != 1
        or not flags & 0x0002
        or not (required | optional) & (1 << 9)
    ):
        raise ValueError("LORD P4G layout or multiplayer flags are invalid")
    payload_sha256 = hashlib.sha256(package[payload_offset:]).digest()
    if package[48:80] != payload_sha256:
        raise ValueError("LORD P4G payload digest is invalid")
    game_id = _c_string(package[80:128], "LORD P4G game ID")
    if game_id != LORD_GAME_ID:
        raise ValueError("cartridge is not LORD")
    profile = package[240:256]
    (
        magic,
        schema,
        style,
        minimum,
        maximum,
        tick_rate,
        input_delay,
        message_bytes,
        protocol,
        profile_flags,
    ) = struct.unpack("<4sBBBBHBBHH", profile)
    if (
        magic != PROFILE_MAGIC
        or schema != 1
        or style != 1
        or minimum != 2
        or maximum != 2
        or not 1 <= tick_rate <= 240
        or input_delay != 0
        or message_bytes != p4mp.GAME_MESSAGE_MAX_BYTES
        or protocol != LORD_PROTOCOL
        or profile_flags != 0
    ):
        raise ValueError("LORD P4G multiplayer profile is incompatible")
    content_sha256 = hashlib.sha256(payload_sha256 + profile).digest()
    compatibility = p4mp.compatibility_sha256(
        mode=1,
        game_api_major=api_version,
        game_api_minor=0,
        player_capacity=2,
        input_delay_ticks=input_delay,
        tick_rate_hz=tick_rate,
        game_protocol=protocol,
        game_id=game_id,
        content_sha256=content_sha256,
    )
    return p4mp.Offer(
        mode=1,
        game_api_major=api_version,
        game_api_minor=0,
        players_present=1,
        player_capacity=2,
        input_delay_ticks=input_delay,
        tick_rate_hz=tick_rate,
        game_protocol=protocol,
        session_seed=session_seed,
        game_id=game_id,
        content_sha256=content_sha256,
        compatibility_sha256=compatibility,
        game_settings=bytes(8),
    )
