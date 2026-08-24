#!/usr/bin/env python3

"""Shared validation and encoding for native-game multiplayer profiles."""

from __future__ import annotations

import struct
from typing import Any, Collection, Mapping


PROFILE_SCHEMA = 1
PROFILE_MAGIC = b"P4MP"
PROFILE_HEADER_FLAG = 1 << 1
PROFILE_BYTES = 16
MULTIPLAYER_CAPABILITY = "multiplayer-session"

STYLE_CODES = {
    "turn-based": 1,
    "realtime": 2,
    "lockstep": 3,
}
CODE_STYLES = {value: key for key, value in STYLE_CODES.items()}

STYLE_DEFAULTS = {
    "turn-based": {"tick_rate_hz": 10, "input_delay_ticks": 0},
    "realtime": {"tick_rate_hz": 30, "input_delay_ticks": 0},
    "lockstep": {"tick_rate_hz": 60, "input_delay_ticks": 2},
}

PROFILE_KEYS = {
    "schema",
    "style",
    "min_players",
    "max_players",
    "tick_rate_hz",
    "input_delay_ticks",
    "message_bytes",
    "protocol",
}


def _integer(profile: Mapping[str, Any], key: str, default: int) -> int:
    value = profile.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"multiplayer.{key} must be an integer")
    return value


def normalize_multiplayer_profile(
    manifest: Mapping[str, Any], capabilities: Collection[str]
) -> dict[str, int | str] | None:
    """Return one canonical bounded profile, or None when not declared."""

    raw = manifest.get("multiplayer")
    if raw is None:
        return None
    if not isinstance(raw, dict):
        raise ValueError("multiplayer must be a JSON object")
    unknown = sorted(set(raw) - PROFILE_KEYS)
    if unknown:
        raise ValueError(f"multiplayer contains unknown fields: {unknown}")
    if MULTIPLAYER_CAPABILITY not in capabilities:
        raise ValueError(
            "multiplayer requires the multiplayer-session capability"
        )
    if raw.get("schema") != PROFILE_SCHEMA:
        raise ValueError(f"multiplayer.schema must be {PROFILE_SCHEMA}")
    style = raw.get("style")
    if not isinstance(style, str) or style not in STYLE_CODES:
        raise ValueError(
            "multiplayer.style must be turn-based, realtime, or lockstep"
        )

    defaults = STYLE_DEFAULTS[style]
    minimum = _integer(raw, "min_players", 2)
    maximum = _integer(raw, "max_players", 2)
    tick_rate = _integer(raw, "tick_rate_hz", defaults["tick_rate_hz"])
    input_delay = _integer(
        raw, "input_delay_ticks", defaults["input_delay_ticks"]
    )
    message_bytes = _integer(raw, "message_bytes", 64)
    protocol = _integer(raw, "protocol", 1)
    if not 2 <= minimum <= maximum <= 4:
        raise ValueError(
            "multiplayer player range must satisfy 2 <= min_players "
            "<= max_players <= 4"
        )
    if not 1 <= tick_rate <= 240:
        raise ValueError("multiplayer.tick_rate_hz must be in 1..240")
    if not 0 <= input_delay <= 15:
        raise ValueError("multiplayer.input_delay_ticks must be in 0..15")
    if style != "lockstep" and input_delay != 0:
        raise ValueError(
            "multiplayer.input_delay_ticks must be zero unless style is lockstep"
        )
    if not 1 <= message_bytes <= 64:
        raise ValueError("multiplayer.message_bytes must be in 1..64")
    if not 1 <= protocol <= 0xFFFF:
        raise ValueError("multiplayer.protocol must be in 1..65535")
    return {
        "schema": PROFILE_SCHEMA,
        "style": style,
        "style_code": STYLE_CODES[style],
        "min_players": minimum,
        "max_players": maximum,
        "tick_rate_hz": tick_rate,
        "input_delay_ticks": input_delay,
        "message_bytes": message_bytes,
        "protocol": protocol,
    }


def encode_multiplayer_profile(profile: Mapping[str, Any]) -> bytes:
    """Encode one normalized profile into the fixed P4G v1 extension."""

    encoded = struct.pack(
        "<4sBBBBHBBHH",
        PROFILE_MAGIC,
        int(profile["schema"]),
        int(profile["style_code"]),
        int(profile["min_players"]),
        int(profile["max_players"]),
        int(profile["tick_rate_hz"]),
        int(profile["input_delay_ticks"]),
        int(profile["message_bytes"]),
        int(profile["protocol"]),
        0,
    )
    if len(encoded) != PROFILE_BYTES:
        raise AssertionError("multiplayer profile encoding changed size")
    return encoded


def decode_multiplayer_profile(encoded: bytes) -> dict[str, int | str]:
    """Decode and revalidate one fixed P4G profile extension."""

    if len(encoded) != PROFILE_BYTES:
        raise ValueError("multiplayer profile must be exactly 16 bytes")
    magic, schema, style_code, minimum, maximum, tick_rate, input_delay, \
        message_bytes, protocol, flags = struct.unpack("<4sBBBBHBBHH", encoded)
    if magic != PROFILE_MAGIC or flags != 0 or style_code not in CODE_STYLES:
        raise ValueError("multiplayer profile marker, style, or flags are invalid")
    raw = {
        "schema": schema,
        "style": CODE_STYLES[style_code],
        "min_players": minimum,
        "max_players": maximum,
        "tick_rate_hz": tick_rate,
        "input_delay_ticks": input_delay,
        "message_bytes": message_bytes,
        "protocol": protocol,
    }
    normalized = normalize_multiplayer_profile(
        {"multiplayer": raw}, {MULTIPLAYER_CAPABILITY}
    )
    if normalized is None or encode_multiplayer_profile(normalized) != encoded:
        raise ValueError("multiplayer profile is not canonical")
    return normalized


def expected_multiplayer_extension(
    manifest: Mapping[str, Any], capabilities: Collection[str]
) -> tuple[int, bytes]:
    profile = normalize_multiplayer_profile(manifest, capabilities)
    if profile is None:
        return 0, bytes(PROFILE_BYTES)
    return PROFILE_HEADER_FLAG, encode_multiplayer_profile(profile)
