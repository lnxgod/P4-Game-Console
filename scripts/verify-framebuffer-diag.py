#!/usr/bin/env python3

"""Fail-closed preflash verification for the M2 framebuffer diagnostic."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/framebuffer_diag"
EVIDENCE_REL = "hardware/evidence/elecrow-10.1-display-path.json"
BUILD_EVIDENCE = ROOT / "test-runs/2026-08-12-framebuffer-m2-build.json"
EXPECTED_LOCK_SHA256 = "a444148460bc071992c1463794413b62b56f62bfe74bcef9154748cc0804fdd2"
EXPECTED_EK79007_HASH = "07c1afab7e9fd4dd2fd06ff9245e65327c5bbd5485efec199496e19a9304d47b"
EXPECTED_INTERFACES = {
    "mipi_dsi_bus_0",
    "internal_ldo_3_2500mv",
    "internal_ldo_4_3300mv",
    "gpio31_backlight_pwm",
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
EXPECTED_SOURCE_FILES = {
    "apps/framebuffer_diag/main/framebuffer_diag_main.c",
    "components/platform_display/src/platform_display.c",
    "components/platform_display/src/platform_display_layout.c",
    "components/platform_display/src/platform_display_layout.h",
    "components/platform_display/include/platform/display.h",
    "components/platform_display/Kconfig",
    "components/platform_display/CMakeLists.txt",
    "components/platform_display/tests/CMakeLists.txt",
    "components/platform_display/tests/test_platform_display_layout.c",
    "apps/framebuffer_diag/sdkconfig.defaults",
    "apps/framebuffer_diag/main/idf_component.yml",
    "apps/framebuffer_diag/CMakeLists.txt",
    "apps/framebuffer_diag/main/CMakeLists.txt",
}
EXPECTED_LINKED_FEATURES = {
    "platform_display_init",
    "platform_display_submit_rgb565",
    "platform_display_get_stats",
    "platform_display_set_brightness",
    "esp_lcd_new_panel_ek79007",
}


def fail(message: str) -> None:
    raise SystemExit(f"framebuffer_diag preflash verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
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
    require(candidate.is_relative_to(build_dir), f"{label} leaves build directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def verify_source(evidence: dict) -> None:
    recorded = evidence.get("source_files")
    require(isinstance(recorded, dict), "source hash inventory is missing")
    require(set(recorded) == EXPECTED_SOURCE_FILES, "reviewed source inventory changed")
    for relative, expected_hash in recorded.items():
        require(isinstance(relative, str), "source inventory path is not a string")
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT), f"source path leaves repository: {relative}")
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source changed: {relative}")

    display = (ROOT / "components/platform_display/src/platform_display.c").read_text()
    layout = (ROOT / "components/platform_display/src/platform_display_layout.c").read_text()
    diagnostic = (APP_DIR / "main/framebuffer_diag_main.c").read_text()
    required_display = (
        "#define DISPLAY_DSI_BUS_ID 0",
        "#define DISPLAY_DSI_DATA_LANES 2",
        "#define DISPLAY_DSI_LANE_RATE_MBPS 900",
        "#define DISPLAY_DPI_CLOCK_MHZ 51",
        "#define DISPLAY_DPHY_LDO_CHANNEL 3",
        "#define DISPLAY_DPHY_LDO_MV 2500",
        "#define DISPLAY_PANEL_LDO_CHANNEL 4",
        "#define DISPLAY_PANEL_LDO_MV 3300",
        "#define DISPLAY_BACKLIGHT_GPIO GPIO_NUM_31",
        ".reset_gpio_num = GPIO_NUM_NC",
        ".lane_num = DISPLAY_DSI_DATA_LANES",
        "const uint32_t refresh_baseline",
        "wait_for_refresh_after(refresh_baseline",
    )
    require(all(token in display for token in required_display), "display contract changed")
    require("esp_lcd_panel_draw_bitmap" in display, "DPI draw path is absent")
    require(display.index("esp_lcd_panel_draw_bitmap") < display.index("const uint32_t refresh_baseline"),
            "refresh baseline is no longer captured after the CPU framebuffer copy")
    require("GPIO_NUM_29" not in display and "GPIO_NUM_41" not in display,
            "forbidden display GPIO appears in executable source")
    require(
        "SOURCE_WIDTH = 320" in layout
        and "SOURCE_HEIGHT = 200" in layout
        and "SCALE = 3" in layout
        and "DESTINATION_WIDTH = 1024" in layout
        and "DESTINATION_HEIGHT = 600" in layout,
        "framebuffer layout geometry changed",
    )
    required_diagnostic = (
        "#define RGB565_RED UINT16_C(0xf800)",
        "#define RGB565_GREEN UINT16_C(0x07e0)",
        "#define RGB565_BLUE UINT16_C(0x001f)",
        "#define RGB565_WHITE UINT16_C(0xffff)",
        "#define RGB565_BLACK UINT16_C(0x0000)",
        "P4_FRAMEBUFFER M2 FIRST_FRAME",
        "P4_FRAMEBUFFER M2 %s frame=",
        "platform_display_submit_rgb565",
        "platform_display_set_brightness(25)",
    )
    require(all(token in diagnostic for token in required_diagnostic),
            "framebuffer acceptance pattern changed")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-framebuffer-diag.py <build-dir> <flash-target>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    require(sys.argv[2] == "app-flash", "framebuffer_diag permits app-only flashing only")

    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "framebuffer_diag",
            "wrong app metadata")
    require(metadata.get("authorization_evidence") == EVIDENCE_REL,
            "wrong electrical authorization evidence")
    require(metadata.get("flash_app_authorized") is True,
            "app-only flash is not authorized")
    require(metadata.get("flash_project_authorized") is False,
            "full-project flash must remain denied")
    require(metadata.get("flash_authorized") is False,
            "legacy broad flash flag must remain denied")
    require(metadata.get("runtime_supported") is False,
            "pre-run metadata cannot claim runtime support")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES,
            "metadata interface scope changed")
    require(EXPECTED_ABSENT <= set(metadata.get("hardware_interfaces_explicitly_absent", [])),
            "metadata no longer denies every out-of-scope interface")

    old_display = load_json(ROOT / "apps/display_diag/app-metadata.json")
    require(old_display.get("flash_app_authorized") is False,
            "historical display_diag must not remain flash-authorized after component changes")

    profile = load_json(ROOT / "hardware/board-profile.json")
    display_auth = profile.get("peripheral_authorizations", {}).get("display", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(display_auth.get("authorized") is True, "display scope is not authorized")
    require(display_auth.get("scope") == "display_only_cross_revision_v1_0_through_v1_2",
            "display authorization scope changed")
    require(display_auth.get("evidence") == EVIDENCE_REL,
            "profile points to different display evidence")

    electrical = load_json(ROOT / EVIDENCE_REL)
    require(
        electrical.get("schema") == 1
        and electrical.get("classification") == "cross-revision-display-path"
        and electrical.get("result") == "pass"
        and electrical.get("scope") == "display_only",
        "display electrical evidence is not a passing scoped authorization",
    )
    require(electrical.get("official_source", {}).get("hardware_revisions_compared")
            == ["V1.0", "V1.1", "V1.2"], "display revision coverage changed")
    require(electrical.get("authorization", {}).get("authorized") is True,
            "electrical display authorization is disabled")

    evidence = load_json(BUILD_EVIDENCE)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification") == "build-tested"
        and evidence.get("result") == "pass"
        and evidence.get("scope") == "display-framebuffer-only",
        "reviewed build evidence is not a passing framebuffer-only result",
    )
    require(evidence.get("authorization_evidence") == EVIDENCE_REL,
            "build evidence uses different electrical evidence")
    build = evidence.get("build", {})
    require(
        build.get("reproducible_build") is True
        and build.get("compile_time_date_enabled") is False
        and build.get("independent_build_directories_compared", 0) >= 2
        and build.get("identical_binary") is True
        and build.get("identical_elf") is True
        and build.get("canonical_build_matches") is True,
        "reviewed evidence lacks exact two-build reproducibility proof",
    )

    toolchain = load_json(ROOT / "toolchain.lock.json")
    target = toolchain["target"]
    recorded_toolchain = evidence.get("toolchain", {})
    require(recorded_toolchain.get("esp_idf_version") == toolchain["esp_idf"]["version"],
            "recorded IDF version differs from lock")
    require(recorded_toolchain.get("esp_idf_commit") == toolchain["esp_idf"]["git_commit"],
            "recorded IDF commit differs from lock")
    require(build.get("target") == target["chip"], "recorded target differs from lock")
    require(build.get("minimum_revision_full") == target["min_revision_full"],
            "recorded minimum revision differs from lock")
    require(build.get("maximum_revision_full") == target["max_revision_full"],
            "recorded maximum revision differs from lock")

    lock_path = APP_DIR / "dependencies.lock"
    require(sha256_file(lock_path) == EXPECTED_LOCK_SHA256, "dependency lock changed")
    lock_text = lock_path.read_text()
    require("version: 1.0.2" in lock_text, "EK79007 1.0.2 is not locked")
    require(f"component_hash: {EXPECTED_EK79007_HASH}" in lock_text,
            "EK79007 component hash changed")
    verify_source(evidence)

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True,
            "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True,
            "compile-time date is enabled")
    require(sdkconfig.get("PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
            "scoped display Kconfig is disabled")
    require(sdkconfig.get("ESP_TASK_WDT_PANIC") is True,
            "DSI stall watchdog panic is disabled")

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs")
    require(int(description.get("min_rev")) == target["min_revision_full"],
            "generated minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"],
            "generated maximum revision differs")
    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == 0x10000,
            "application offset is not 0x10000")
    # Installed layout is checked directly by the flash route; no recovery image is needed.
    binary = checked_build_path(build_dir, app.get("file"), "application binary")
    elf = checked_build_path(build_dir, description.get("app_elf"), "application ELF")
    artifacts = evidence.get("artifacts", {})
    require(binary.stat().st_size == artifacts.get("app_binary_bytes"),
            "application binary size differs from reviewed artifact")
    require(sha256_file(binary) == artifacts.get("app_binary_sha256"),
            "application binary hash differs from reviewed artifact")
    require(elf.stat().st_size == artifacts.get("elf_bytes"),
            "application ELF size differs from reviewed artifact")
    require(sha256_file(elf) == artifacts.get("elf_sha256"),
            "application ELF hash differs from reviewed artifact")

    symbols = subprocess.run(["nm", "-g", str(elf)], check=True,
                             capture_output=True, text=True).stdout
    linked_features = evidence.get("linked_features")
    require(isinstance(linked_features, list) and set(linked_features) == EXPECTED_LINKED_FEATURES,
            "required linked-feature inventory changed")
    for symbol in linked_features:
        require(re.search(rf"\b{re.escape(symbol)}$", symbols, re.MULTILINE) is not None,
                f"required linked symbol is missing: {symbol}")

    print(
        "framebuffer_diag preflash verification: PASS "
        f"mode=app-only offset=0x10000 bytes={binary.stat().st_size} "
        f"sha256={artifacts['app_binary_sha256']}"
    )


if __name__ == "__main__":
    main()
