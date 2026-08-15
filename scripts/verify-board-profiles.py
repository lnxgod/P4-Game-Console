#!/usr/bin/env python3

"""Validate additive board profiles without weakening exact-device evidence."""

from __future__ import annotations

import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def load(path: str) -> dict:
    return json.loads((ROOT / path).read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"board profile verification failed: {message}")


def main() -> None:
    elecrow = load("hardware/board-profile.json")
    olimex = load("hardware/boards/olimex-esp32-p4-pc-rev-b.json")
    evidence = load(
        "hardware/evidence/olimex-esp32-p4-pc-rev-b-source-review.json"
    )

    require(elecrow.get("vendor") == "Elecrow", "default profile changed")
    require(
        elecrow.get("device_identity", {}).get("sha256") is not None,
        "default exact-device identity was removed",
    )
    require(olimex.get("schema") == 1, "unsupported Olimex profile schema")
    require(olimex.get("id") == "olimex-esp32-p4-pc-rev-b", "wrong board ID")
    require(olimex.get("pcb_revision") == "B", "profile must be Rev.B-specific")
    require(olimex.get("soc") == "ESP32-P4NRW32", "unexpected module")
    require(
        olimex.get("memory") == {
            "flash_bytes": 16 * 1024 * 1024,
            "psram_bytes": 32 * 1024 * 1024,
        },
        "unexpected Olimex memory geometry",
    )
    source = olimex.get("official_source", {})
    require(COMMIT_RE.fullmatch(str(source.get("commit"))) is not None,
            "official source commit is not pinned")
    require(
        all(SHA256_RE.fullmatch(str(source.get(key))) is not None
            for key in ("manual_sha256", "schematic_sha256")),
        "manual/schematic hashes are missing",
    )
    display = olimex.get("interfaces", {}).get("display", {})
    require(
        display.get("bridge") == "LT8912B"
        and (display.get("width"), display.get("height")) == (1280, 720)
        and (display.get("i2c_sda_gpio"), display.get("i2c_scl_gpio")) == (7, 8),
        "unexpected HDMI path",
    )
    storage = olimex.get("interfaces", {}).get("storage", {})
    require(
        [storage.get(key) for key in
         ("clk_gpio", "cmd_gpio", "d0_gpio", "d1_gpio", "d2_gpio", "d3_gpio")]
        == [43, 44, 39, 40, 41, 42],
        "unexpected SDMMC pin map",
    )
    require(storage.get("runtime_format_allowed") is False,
            "runtime formatting must remain forbidden")
    require(storage.get("hot_removal_supported") is False,
            "unimplemented SD hot-removal must not be claimed")
    require(
        olimex.get("interfaces", {}).get("programming_usb", {}).get(
            "game_storage_mass_storage"
        ) is False,
        "USB-C must not be advertised as game-storage MSC",
    )
    flash_layout = olimex.get("interfaces", {}).get("flash_layout", {})
    require(
        flash_layout == {
            "partition_table": "apps/console_os/partitions-olimex.csv",
            "ota_slot_bytes": 0x7F0000,
            "persistent_game_storage": "microSD",
        },
        "unexpected Olimex flash/storage layout",
    )
    require(
        (ROOT / flash_layout["partition_table"]).is_file(),
        "Olimex partition table is missing",
    )
    defaults = (
        ROOT / "hardware/boards/olimex-esp32-p4-pc-rev-b/sdkconfig.defaults"
    ).read_text(encoding="utf-8")
    require(
        "CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B=y" in defaults
        and 'CONFIG_PARTITION_TABLE_FILENAME="partitions-olimex.csv"' in defaults
        and "CONFIG_DOOM_AUDIO_ENGINE_ADAPTER=y" in defaults
        and "CONFIG_TINYUSB_MSC_ENABLED" not in defaults,
        "Olimex board defaults do not select the safe profile",
    )
    audio = olimex.get("interfaces", {}).get("audio", {})
    require(
        audio.get("codec") == "ES8311"
        and audio.get("implemented") is True
        and audio.get("hardware_tested") is False
        and [audio.get(key) for key in (
            "i2s_mclk_gpio", "i2s_bclk_gpio", "i2s_lrclk_gpio",
            "i2s_dout_gpio", "i2s_din_gpio", "amplifier_enable_gpio",
        )] == [13, 12, 10, 9, 11, 53],
        "unexpected Olimex audio contract",
    )
    require(
        olimex.get("device_identity") is None
        and olimex.get("factory_backup") is None
        and olimex.get("hardware_tested") is False
        and olimex.get("flash_authorized") is False,
        "unverified Olimex hardware must remain write-locked",
    )
    require(evidence.get("board_profile") ==
            "hardware/boards/olimex-esp32-p4-pc-rev-b.json",
            "source evidence is not bound to the profile")
    require(evidence.get("authorization", {}).get("flash") is False,
            "source review cannot authorize flashing")

    print("Board profiles: PASS (Elecrow default preserved; Olimex Rev.B write-locked)")


if __name__ == "__main__":
    main()
