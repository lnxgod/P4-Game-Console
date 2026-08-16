#!/usr/bin/env python3

"""Cross-check the repository's hardware identity and acceptance evidence."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
RAW_MAC_RE = re.compile(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}")


def load(relative_path: str) -> dict:
    return json.loads((ROOT / relative_path).read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"metadata verification failed: {message}")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    manifest = load("hardware/backups/manifest.json")
    profile = load("hardware/board-profile.json")
    identification = profile.get("identification")
    require(isinstance(identification, dict), "board profile identification must be an object")
    variant_evidence_path = identification.get("evidence")
    require(isinstance(variant_evidence_path, str), "board profile has no variant evidence path")
    variant_evidence = load(variant_evidence_path)
    peripheral_authorizations = profile.get("peripheral_authorizations")
    require(
        isinstance(peripheral_authorizations, dict),
        "board profile peripheral_authorizations must be an object",
    )
    display_authorization = peripheral_authorizations.get("display")
    require(isinstance(display_authorization, dict), "display authorization must be an object")
    display_evidence_path = display_authorization.get("evidence")
    require(isinstance(display_evidence_path, str), "display authorization has no evidence path")
    display_evidence = load(display_evidence_path)
    display_test_path = display_authorization.get("hardware_test")
    require(isinstance(display_test_path, str), "display authorization has no hardware-test path")
    display_test = load(display_test_path)
    test_path = profile["last_hardware_test"]
    test_run = load(test_path)
    backup_baseline_path = "hardware/test-runs/2026-08-12-bringup.json"
    backup_baseline = load(backup_baseline_path)

    require(
        manifest.get("schema") == profile.get("schema") ==
        test_run.get("schema") == backup_baseline.get("schema") == 1,
        "unsupported or inconsistent hardware metadata schema",
    )

    manifest_identity = manifest["device"]["identity"]
    profile_identity = profile["device_identity"]
    identity_hash = manifest_identity["sha256"]
    require(SHA256_RE.fullmatch(identity_hash) is not None, "invalid device identity hash")
    require(manifest_identity == profile_identity, "board profile identity differs from backup manifest")
    require(manifest_identity["raw_value_stored"] is False, "raw device identity must not be stored")
    require(
        test_run["hardware"]["device_identity_sha256"] == identity_hash,
        "test run identity differs from backup manifest",
    )
    require(
        manifest["restore"]["requires_matching_live_device_identity"] is True,
        "restore policy must require a matching live identity",
    )

    require(
        manifest["device"]["revision"]
        == profile["measured"]["chip_revision"]
        == test_run["hardware"]["chip_revision"],
        "chip revision differs across evidence files",
    )
    require(
        manifest["device"]["flash_bytes"]
        == profile["measured"]["flash_bytes"]
        == test_run["hardware"]["flash_bytes"],
        "flash size differs across evidence files",
    )

    require(variant_evidence.get("schema") == 1, "unsupported firmware-variant evidence schema")
    require(
        variant_evidence.get("classification") == "factory-firmware-variant"
        and variant_evidence.get("result") == "pass",
        "firmware-variant evidence must be a passing, narrowly classified result",
    )
    variant_backup = variant_evidence.get("factory_backup")
    require(isinstance(variant_backup, dict), "firmware-variant backup evidence must be an object")
    require(
        all(variant_backup.get(field) == manifest["backup"].get(field) for field in ("file", "bytes", "sha256")),
        "firmware-variant evidence is not bound to the recovery backup",
    )
    conclusion = variant_evidence.get("conclusion")
    require(isinstance(conclusion, dict), "firmware-variant conclusion must be an object")
    require(conclusion.get("scope") == "factory_firmware_variant", "screen/SKU evidence scope is too broad")
    require(
        profile.get("screen_inches") == conclusion.get("screen_inches") == 10.1,
        "profile screen differs from firmware-variant evidence",
    )
    require(
        profile.get("sku") == conclusion.get("sku") == "DHE04310D",
        "profile SKU differs from firmware-variant evidence",
    )
    require(
        identification.get("screen_sku_scope") == conclusion.get("scope"),
        "profile does not label screen/SKU as a firmware-variant identification",
    )
    require(
        identification.get("physical_screen_sku_confirmed") is False
        and conclusion.get("physical_screen_sku_confirmed") is False,
        "firmware evidence cannot claim physical screen/SKU confirmation",
    )
    require(
        profile.get("pcb_revision") is None and conclusion.get("pcb_revision") is None,
        "firmware evidence cannot infer the PCB revision",
    )
    require(
        profile.get("pin_map_authorized") is False and conclusion.get("pin_map_authorized") is False,
        "firmware evidence cannot authorize a pin map",
    )

    require(display_evidence.get("schema") == 1, "unsupported display evidence schema")
    require(
        display_evidence.get("classification") == "cross-revision-display-path"
        and display_evidence.get("result") == "pass"
        and display_evidence.get("scope") == "display_only",
        "display evidence must be a passing display-only cross-revision result",
    )
    display_source = display_evidence.get("official_source")
    require(isinstance(display_source, dict), "display evidence official_source must be an object")
    require(
        display_source.get("vendor") == profile.get("vendor") == "Elecrow"
        and display_source.get("model") == profile.get("sku") == "DHE04310D",
        "display evidence does not match the board vendor/model",
    )
    require(
        SHA256_RE.fullmatch(str(display_source.get("commit"))) is None
        and re.fullmatch(r"[0-9a-f]{40}", str(display_source.get("commit"))) is not None,
        "invalid official display-source Git commit",
    )
    require(
        display_source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"],
        "display evidence must compare every published hardware revision",
    )
    schematics = display_evidence.get("schematics")
    require(isinstance(schematics, list) and len(schematics) == 3, "expected three schematic records")
    require(
        [item.get("revision") for item in schematics if isinstance(item, dict)]
        == ["V1.0", "V1.1", "V1.2"],
        "schematic revision coverage is incomplete or unordered",
    )
    require(
        all(
            isinstance(item, dict)
            and isinstance(item.get("path"), str)
            and SHA256_RE.fullmatch(str(item.get("sha256"))) is not None
            for item in schematics
        ),
        "invalid display schematic provenance",
    )
    invariants = display_evidence.get("cross_revision_invariants")
    require(isinstance(invariants, dict), "display cross-revision invariants must be an object")
    require(
        invariants.get("resolution") == {"width": 1024, "height": 600}
        and invariants.get("controller") == "EK79007",
        "unexpected display geometry or controller",
    )
    require(
        invariants.get("mipi_dsi")
        == {
            "bus_id": 0,
            "data_lanes": 2,
            "dedicated_signal_nets": [
                "DSI_CLK_P",
                "DSI_CLK_N",
                "DSI_DATA0_P",
                "DSI_DATA0_N",
                "DSI_DATA1_P",
                "DSI_DATA1_N",
            ],
        },
        "unexpected MIPI DSI display path",
    )
    require(
        invariants.get("power") == {"ldo3_mv": 2500, "ldo4_mv": 3300},
        "unexpected display LDO configuration",
    )
    backlight = invariants.get("backlight")
    require(isinstance(backlight, dict), "display backlight evidence must be an object")
    require(
        backlight.get("gpio") == 31
        and backlight.get("driver") == "MT9201"
        and backlight.get("pwm_hz") == 30000
        and backlight.get("power_gate_gpio_29_required") is False,
        "unexpected display backlight path",
    )
    require(
        invariants.get("reset", {}).get("driver_gpio") == -1
        and invariants.get("reset", {}).get("policy") == "do_not_drive",
        "display diagnostic must not drive the revision-unneeded reset GPIO",
    )
    lesson = display_evidence.get("official_lesson_07")
    require(isinstance(lesson, dict), "official Lesson 07 evidence must be an object")
    require(
        lesson.get("effective_settings_match") is True
        and lesson.get("lane_bit_rate_mbps") == 900
        and lesson.get("dpi_clock_mhz") == 51
        and lesson.get("bits_per_pixel") == 16,
        "official Lesson 07 settings are incomplete or inconsistent",
    )
    for revision in ("v1_1", "v1_2"):
        lesson_revision = lesson.get(revision)
        require(isinstance(lesson_revision, dict), f"missing Lesson 07 {revision} provenance")
        require(
            all(
                SHA256_RE.fullmatch(str(lesson_revision.get(field))) is not None
                for field in ("driver_sha256", "header_sha256")
            ),
            f"invalid Lesson 07 {revision} hashes",
        )
    display_evidence_authorization = display_evidence.get("authorization")
    require(
        display_authorization.get("authorized") is True
        and display_evidence_authorization.get("authorized") is True,
        "display authorization is not enabled in both profile and evidence",
    )
    require(
        display_authorization.get("scope") == "display_only_cross_revision_v1_0_through_v1_2",
        "display profile authorization has an unexpected scope",
    )
    require(
        set(display_evidence_authorization.get("scope", []))
        == {
            "mipi_dsi_bus_0_dedicated_pins",
            "internal_ldo_channel_3_at_2500_mv",
            "internal_ldo_channel_4_at_3300_mv",
            "gpio31_lcd_backlight_enable_pwm",
        },
        "display authorization grants an unexpected hardware resource",
    )
    require(
        {"gpio29_lcd_backlight_optional_power_gate", "gpio41_lcd_reset", "all_other_gpio"}
        <= set(display_evidence_authorization.get("explicitly_not_authorized", [])),
        "display evidence does not explicitly deny unneeded GPIOs",
    )

    require(
        display_test.get("schema") == 1
        and display_test.get("classification") == "hardware-tested"
        and display_test.get("result") == "pass",
        "display hardware test is not a passing hardware-tested result",
    )
    require(
        profile.get("measured", {}).get("display_acceptance") == "pass",
        "board profile does not record the display acceptance pass",
    )
    display_test_hardware = display_test.get("hardware")
    require(isinstance(display_test_hardware, dict), "display hardware-test target is missing")
    require(
        display_test_hardware.get("device_identity_sha256") == identity_hash
        and display_test_hardware.get("chip_revision") == profile["measured"]["chip_revision"]
        and display_test_hardware.get("flash_bytes") == profile["measured"]["flash_bytes"],
        "display hardware test is not bound to the recorded board",
    )
    require(
        display_test_hardware.get("pcb_revision") is None
        and display_test_hardware.get("physical_screen_sku_confirmed") is False,
        "display-only runtime evidence cannot infer the physical label or PCB revision",
    )
    require(
        display_test.get("official_board_source", {}).get("commit")
        == display_source.get("commit"),
        "display runtime evidence uses a different Elecrow source commit",
    )
    display_build_path = display_test.get("build_evidence")
    require(isinstance(display_build_path, str), "display runtime evidence has no build-evidence path")
    display_build = load(display_build_path)
    display_artifacts = display_build.get("artifacts", {})
    display_firmware = display_test.get("firmware", {})
    require(
        display_firmware.get("binary_bytes") == display_artifacts.get("app_binary_bytes")
        and display_firmware.get("binary_sha256") == display_artifacts.get("app_binary_sha256")
        and display_firmware.get("elf_bytes") == display_artifacts.get("elf_bytes")
        and display_firmware.get("elf_sha256") == display_artifacts.get("elf_sha256"),
        "display runtime firmware differs from the reviewed reproducible build",
    )
    display_checks = display_test.get("checks", {})
    require(
        display_checks.get("installed_app_readback_matches_binary") is True
        and display_checks.get("installed_app_readback_bytes") == display_firmware.get("binary_bytes")
        and display_checks.get("installed_app_readback_sha256") == display_firmware.get("binary_sha256"),
        "display application readback is missing or differs from the reviewed binary",
    )
    require(
        display_checks.get("visible_test_patterns_observed") is True
        and display_checks.get("failure_markers_observed") is False,
        "display visual/serial acceptance did not pass",
    )
    display_markers = set(display_checks.get("serial_markers", []))
    require(
        {
            "P4_DISPLAY M1 PANEL_READY resolution=1024x600 format=rgb565 lane_mbps=900 dpi_mhz=51",
            "P4_DISPLAY M1 PATTERN name=bars-vertical",
            "P4_DISPLAY M1 PATTERN name=bars-horizontal",
            "P4_DISPLAY M1 PATTERN name=ber-vertical",
        }
        <= display_markers,
        "display serial acceptance markers are incomplete",
    )

    variants = variant_evidence.get("reference_variants")
    require(isinstance(variants, list), "firmware reference variants must be an array")
    variants_by_id = {
        item.get("id"): item for item in variants if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    require(set(variants_by_id) == {"inch7", "inch9", "inch10_1"}, "unexpected firmware candidate set")
    require(
        variants_by_id["inch7"].get("backup_full_asset_occurrence_offsets") == []
        and variants_by_id["inch9"].get("backup_full_asset_occurrence_offsets") == [],
        "7- and 9-inch negative full-asset searches are not recorded",
    )
    require(
        variants_by_id["inch10_1"].get("backup_full_asset_occurrence_offsets") == ["0x71004"],
        "10.1-inch full-asset occurrence is not recorded at the expected flash offset",
    )
    selected_asset = variants_by_id["inch10_1"].get("asset")
    require(isinstance(selected_asset, dict), "10.1-inch asset evidence must be an object")
    require(selected_asset.get("bytes") == 1843200, "unexpected 10.1-inch asset byte count")
    require(
        SHA256_RE.fullmatch(str(selected_asset.get("sha256"))) is not None,
        "invalid 10.1-inch asset SHA-256",
    )
    if profile["pin_map_authorized"]:
        require(
            all(profile.get(field) is not None for field in ("screen_inches", "sku", "pcb_revision")),
            "pin map cannot be authorized without screen size, SKU, and PCB revision",
        )
        require(
            identification.get("physical_screen_sku_confirmed") is True,
            "pin map cannot be authorized from a firmware-variant identification alone",
        )

    backup = manifest["backup"]
    backup_root = (ROOT / "hardware/backups").resolve()
    backup_path = (ROOT / backup["file"]).resolve()
    require(backup_path.is_relative_to(backup_root), "backup path leaves hardware/backups")
    require(SHA256_RE.fullmatch(backup["sha256"]) is not None, "invalid backup SHA-256")
    if backup_path.exists():
        require(backup_path.stat().st_size == backup["bytes"], "local backup byte count differs from manifest")
        require(sha256_file(backup_path) == backup["sha256"], "local backup hash differs from manifest")

        selected_offset = int(variants_by_id["inch10_1"]["backup_full_asset_occurrence_offsets"][0], 16)
        with backup_path.open("rb") as source:
            source.seek(selected_offset)
            selected_bytes = source.read(selected_asset["bytes"])
        require(
            len(selected_bytes) == selected_asset["bytes"]
            and hashlib.sha256(selected_bytes).hexdigest() == selected_asset["sha256"],
            "saved backup does not contain the pinned 10.1-inch asset at the recorded offset",
        )

        prefix_offset = int(
            backup_baseline["checks"]["factory_prefix_readback_offset"], 0)
        prefix_bytes = backup_baseline["checks"]["factory_prefix_readback_bytes"]
        require(prefix_offset == 0, "M0 factory-prefix evidence must start at offset 0")
        with backup_path.open("rb") as source:
            expected_prefix_hash = hashlib.sha256(source.read(prefix_bytes)).hexdigest()
        require(
            expected_prefix_hash ==
            backup_baseline["checks"]["factory_prefix_readback_sha256"],
            "recorded factory-prefix hash differs from the saved backup",
        )

    checks = backup_baseline["checks"]
    require(checks["factory_backup_bytes"] == backup["bytes"], "test run backup byte count differs from manifest")
    require(checks["factory_backup_sha256"] == backup["sha256"], "test run backup hash differs from manifest")
    require(checks["factory_backup_sha256_verified_before_write"] is True, "pre-write backup check not recorded")
    require(checks["factory_prefix_readback_matches_backup"] is True, "factory-prefix readback did not pass")
    require(checks["installed_app_readback_matches_binary"] is True, "application readback did not pass")
    require(
        checks["installed_app_readback_bytes"] ==
        backup_baseline["firmware"]["binary_bytes"],
        "application readback byte count differs from recorded binary",
    )
    require(
        checks["installed_app_readback_sha256"] ==
        backup_baseline["firmware"]["binary_sha256"],
        "application readback hash differs from recorded binary",
    )

    for relative_path in (
        "hardware/backups/manifest.json",
        "hardware/board-profile.json",
        variant_evidence_path,
        display_evidence_path,
        display_test_path,
        display_test.get("build_evidence"),
        test_path,
        backup_baseline_path,
    ):
        require(
            RAW_MAC_RE.search((ROOT / relative_path).read_text()) is None,
            f"raw MAC-like identifier found in {relative_path}",
        )

    print("hardware metadata: PASS")


if __name__ == "__main__":
    main()
