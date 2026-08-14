#!/usr/bin/env python3

"""Fail-closed preflash verification for the scoped display diagnostic."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/display_diag"
EVIDENCE_REL = "hardware/evidence/elecrow-10.1-display-path.json"
BUILD_EVIDENCE = ROOT / "test-runs/2026-08-12-display-m1-build.json"
EXPECTED_LOCK_SHA256 = "a444148460bc071992c1463794413b62b56f62bfe74bcef9154748cc0804fdd2"
EXPECTED_EK79007_VERSION = "1.0.2"
EXPECTED_EK79007_HASH = "07c1afab7e9fd4dd2fd06ff9245e65327c5bbd5485efec199496e19a9304d47b"
EXPECTED_METADATA_INTERFACES = {
    "mipi_dsi_bus_0",
    "internal_ldo_3_2500mv",
    "internal_ldo_4_3300mv",
    "gpio31_backlight_pwm",
}
EXPECTED_AUTHORIZED_RESOURCES = {
    "mipi_dsi_bus_0_dedicated_pins",
    "internal_ldo_channel_3_at_2500_mv",
    "internal_ldo_channel_4_at_3300_mv",
    "gpio31_lcd_backlight_enable_pwm",
}
EXPECTED_ABSENT = {
    "gpio29",
    "gpio41",
    "touch",
    "sd_card",
    "audio",
    "camera",
    "wireless",
    "usb_host",
}


def fail(message: str) -> None:
    raise SystemExit(f"display_diag preflash verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path.relative_to(ROOT)}: {error}")
    require(isinstance(value, dict), f"{path.relative_to(ROOT)} must contain an object")
    return value


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        fail(f"cannot hash {path}: {error}")
    return digest.hexdigest()


def checked_build_path(build_dir: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    candidate = (build_dir / relative).resolve()
    require(candidate.is_relative_to(build_dir), f"{label} path leaves build directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def strip_c_comments_and_strings(text: str) -> str:
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(" ", text)


def verify_source_scope() -> None:
    source_files = sorted((APP_DIR / "main").glob("*.c")) + sorted(
        (ROOT / "components/platform_display").rglob("*.c")
    )
    source_files += sorted((ROOT / "components/platform_display").rglob("*.h"))
    require(source_files, "no display diagnostic source files found")
    stripped = "\n".join(strip_c_comments_and_strings(path.read_text()) for path in source_files)
    require(
        re.search(r"\b(?:GPIO_NUM_29|GPIO_NUM_41|29|41)\b", stripped) is None,
        "GPIO29 or GPIO41 appears in executable display source",
    )
    component = (ROOT / "components/platform_display/src/platform_display.c").read_text()
    required_tokens = (
        "#define DISPLAY_DSI_BUS_ID 0",
        "#define DISPLAY_DSI_DATA_LANES 2",
        "#define DISPLAY_DPHY_LDO_CHANNEL 3",
        "#define DISPLAY_DPHY_LDO_MV 2500",
        "#define DISPLAY_PANEL_LDO_CHANNEL 4",
        "#define DISPLAY_PANEL_LDO_MV 3300",
        "#define DISPLAY_BACKLIGHT_GPIO GPIO_NUM_31",
        ".reset_gpio_num = GPIO_NUM_NC",
        ".lane_num = DISPLAY_DSI_DATA_LANES",
    )
    require(all(token in component for token in required_tokens), "display source resource constants changed")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-display-diag.py <build-dir> <flash-target>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    require(sys.argv[2] == "app-flash", "display_diag permits app-only flashing only")

    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "display_diag", "wrong app metadata")
    require(metadata.get("authorization_evidence") == EVIDENCE_REL, "wrong authorization evidence")
    require(metadata.get("flash_app_authorized") is True, "app-only flash is not authorized")
    require(metadata.get("flash_project_authorized") is False, "full-project flash must remain denied")
    require(metadata.get("flash_authorized") is False, "legacy broad flash flag must remain denied")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_METADATA_INTERFACES, "metadata interface scope changed")
    require(EXPECTED_ABSENT <= set(metadata.get("hardware_interfaces_explicitly_absent", [])), "metadata no longer denies every out-of-scope interface")

    profile = load_json(ROOT / "hardware/board-profile.json")
    display_auth = profile.get("peripheral_authorizations", {}).get("display", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(display_auth.get("authorized") is True, "board profile display scope is not authorized")
    require(display_auth.get("scope") == "display_only_cross_revision_v1_0_through_v1_2", "board profile display scope changed")
    require(display_auth.get("evidence") == EVIDENCE_REL, "board profile points to different display evidence")

    electrical = load_json(ROOT / EVIDENCE_REL)
    require(
        electrical.get("schema") == 1
        and electrical.get("classification") == "cross-revision-display-path"
        and electrical.get("result") == "pass"
        and electrical.get("scope") == "display_only",
        "display electrical evidence is not a passing scoped authorization",
    )
    source = electrical.get("official_source", {})
    require(source.get("model") == "DHE04310D", "display evidence model changed")
    require(source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"], "display revision coverage changed")
    authorization = electrical.get("authorization", {})
    require(authorization.get("authorized") is True, "electrical authorization is disabled")
    require(set(authorization.get("scope", [])) == EXPECTED_AUTHORIZED_RESOURCES, "electrical resource scope changed")
    denied = set(authorization.get("explicitly_not_authorized", []))
    require(
        {"gpio29_lcd_backlight_optional_power_gate", "gpio41_lcd_reset", "all_other_gpio"} <= denied,
        "electrical evidence no longer denies out-of-scope GPIOs",
    )

    build_evidence = load_json(BUILD_EVIDENCE)
    require(
        build_evidence.get("schema") == 1
        and build_evidence.get("classification") == "build-tested"
        and build_evidence.get("result") == "pass"
        and build_evidence.get("scope") == "display-only",
        "reviewed build evidence is not a passing display-only result",
    )
    require(build_evidence.get("authorization_evidence") == EVIDENCE_REL, "build evidence uses different authorization")

    toolchain = load_json(ROOT / "toolchain.lock.json")
    recorded_toolchain = build_evidence.get("toolchain", {})
    require(recorded_toolchain.get("esp_idf_version") == toolchain["esp_idf"]["version"], "recorded IDF version differs from lock")
    require(recorded_toolchain.get("esp_idf_commit") == toolchain["esp_idf"]["git_commit"], "recorded IDF commit differs from lock")
    recorded_build = build_evidence.get("build", {})
    target = toolchain["target"]
    require(recorded_build.get("target") == target["chip"], "recorded target differs from lock")
    require(recorded_build.get("minimum_revision_full") == target["min_revision_full"], "recorded minimum revision differs from lock")
    require(recorded_build.get("maximum_revision_full") == target["max_revision_full"], "recorded maximum revision differs from lock")
    require(
        recorded_build.get("reproducible_build") is True
        and recorded_build.get("compile_time_date_enabled") is False
        and recorded_build.get("independent_build_directories_compared", 0) >= 2
        and recorded_build.get("identical_binary") is True
        and recorded_build.get("identical_elf") is True,
        "reviewed evidence lacks two-build reproducibility proof",
    )
    require(recorded_build.get("scoped_display_authorization_enabled") is True, "build evidence lacks scoped authorization")
    require(recorded_build.get("global_pin_map_authorized") is False, "build evidence claims a global pin map")

    lock_path = APP_DIR / "dependencies.lock"
    require(sha256_file(lock_path) == EXPECTED_LOCK_SHA256, "dependency lock hash changed")
    deps = build_evidence.get("dependencies", {})
    require(deps.get("lock_file") == "apps/display_diag/dependencies.lock", "build evidence names a different lock")
    require(deps.get("lock_file_sha256") == EXPECTED_LOCK_SHA256, "recorded dependency lock hash changed")
    require(deps.get("esp_lcd_ek79007_version") == EXPECTED_EK79007_VERSION, "recorded EK79007 version changed")
    require(deps.get("esp_lcd_ek79007_component_hash") == EXPECTED_EK79007_HASH, "recorded EK79007 component hash changed")
    lock_text = lock_path.read_text()
    require(f"version: {EXPECTED_EK79007_VERSION}" in lock_text, "EK79007 1.0.2 is not locked")
    require(f"component_hash: {EXPECTED_EK79007_HASH}" in lock_text, "EK79007 component hash is not locked")

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True, "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True, "compile-time date is enabled")
    require(
        sdkconfig.get("PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
        "scoped display Kconfig is disabled",
    )
    require(sdkconfig.get("ESP_TASK_WDT_PANIC") is True, "DSI stall watchdog panic is disabled")

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs from lock")
    require(int(description.get("min_rev")) == target["min_revision_full"], "generated minimum revision differs from lock")
    require(int(description.get("max_rev")) == target["max_revision_full"], "generated maximum revision differs from lock")

    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == 0x10000, "application offset is not 0x10000")
    manifest = load_json(ROOT / "hardware/backups/manifest.json")
    factory_apps = [p for p in manifest.get("factory_partition_table", []) if p.get("type") == "app" and p.get("subtype") == "factory"]
    require(len(factory_apps) == 1 and int(factory_apps[0]["offset"], 0) == 0x10000, "factory application offset is not 0x10000")
    binary = checked_build_path(build_dir, app.get("file"), "application binary")
    elf = checked_build_path(build_dir, description.get("app_elf"), "application ELF")
    artifacts = build_evidence.get("artifacts", {})
    require(binary.stat().st_size == artifacts.get("app_binary_bytes"), "application binary size differs from reviewed artifact")
    require(sha256_file(binary) == artifacts.get("app_binary_sha256"), "application binary hash differs from reviewed artifact")
    require(elf.stat().st_size == artifacts.get("elf_bytes"), "application ELF size differs from reviewed artifact")
    require(sha256_file(elf) == artifacts.get("elf_sha256"), "application ELF hash differs from reviewed artifact")

    verify_source_scope()
    print(
        "display_diag preflash verification: PASS "
        f"mode=app-only offset=0x10000 bytes={binary.stat().st_size} "
        f"sha256={artifacts['app_binary_sha256']}"
    )


if __name__ == "__main__":
    main()
