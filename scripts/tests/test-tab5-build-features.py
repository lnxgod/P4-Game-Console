#!/usr/bin/env python3
"""The verifier and install authorization distinguish host-on and host-off images."""
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("verify_tab5",root/"scripts/verify-console-os-tab5.py")
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
components={"platform_usb_host","platform_gamepad_usb","platform_gamepad_xusb","platform_gamepad","doom_gamepad_input"}
sources={"key_merge.c","usb_power_control.c","platform_usb_host.c","platform_gamepad_usb.c","xusb.c","xusb_host.c"}
symbols="\n".join("0000 T "+name for name in ("platform_tab5_usb_host_power","platform_usb_host_start","platform_usb_host_enable_root_port","platform_gamepad_usb_start","doom_gamepad_input_update","doom_key_merge_update","platform_gamepad_xusb_start","gamepad_xusb_decode"))
symbols += "\n48000000 b s_report_buffers"
v.verify_usb_host(False,set(),set(),"")
v.verify_usb_host(True,components,sources,symbols)
for enabled,c,s,n in ((False,components,sources,symbols),(False,set(),set(),symbols),(True,set(),sources,symbols),(True,components,set(),symbols),(True,components,sources,"")):
    try:v.verify_usb_host(enabled,c,s,n)
    except ValueError:pass
    else:raise AssertionError("mismatched USB feature accepted")
for missing_component in ("platform_gamepad_xusb",):
    try: v.verify_usb_host(True, components-{missing_component}, sources, symbols)
    except ValueError: pass
    else: raise AssertionError("missing XUSB component accepted")
for missing_source in ("xusb.c", "xusb_host.c"):
    try: v.verify_usb_host(True, components, sources-{missing_source}, symbols)
    except ValueError: pass
    else: raise AssertionError("missing XUSB source accepted")
for missing_symbol in ("platform_gamepad_xusb_start", "gamepad_xusb_decode"):
    reduced="\n".join(line for line in symbols.splitlines() if not line.endswith(" "+missing_symbol))
    try: v.verify_usb_host(True, components, sources, reduced)
    except ValueError: pass
    else: raise AssertionError("missing XUSB linked symbol accepted")
for address in ("4ff80000", "4a000000", "00000000"):
    try: v.verify_usb_host(True, components, sources, symbols.replace("48000000",address))
    except ValueError: pass
    else: raise AssertionError("HID buffers outside Tab5 PSRAM accepted")
try: v.verify_usb_host(True, components, sources, symbols.replace("48000000 b s_report_buffers",""))
except ValueError: pass
else: raise AssertionError("missing HID PSRAM allocation accepted")
print("Tab5 USB host feature gates PASS")

# Native scheduling and transfer services have no script runtime dependency.
v.verify_native_only({"p4_frame_scheduler", "p4_usb_content_transfer"},
                     "0000 T p4_tick_scheduler_advance\n0000 T app_main")
for retired in ("p4_lua_runtime", "p4_script_renderer", "p4_script_audio",
                "p4_game_runtime_core", "p4_content_catalog", "lua"):
    try: v.verify_native_only({retired}, "")
    except ValueError: pass
    else: raise AssertionError("retired Lua component accepted")
for name in ("lua_newstate", "luaL_loadbufferx", "luaV_execute",
             "p4_lua_runtime_create", "run_script_cart",
             "p4_content_validate_cart_file", "p4_content_load_cart_source",
             "p4_content_catalog_scan"):
    try: v.verify_native_only(set(), "0000 T " + name)
    except ValueError: pass
    else: raise AssertionError("retired Lua symbol accepted")
print("Tab5 native-only execution gates PASS")

# Reject old incremental-build configs before accepting a build candidate.
seek_config = ("CONFIG_FATFS_USE_FASTSEEK=y\n"
               "CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y\n"
               "CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE=112456\n")
v.verify_storage_seek(seek_config)
for invalid in ("", seek_config.replace("=y", "=n"),
                seek_config.replace("=112456", "=64"),
                seek_config.replace("=112456", "=16384"),
                seek_config.replace("=112456", "=112457"),
                seek_config.replace("CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y\n", ""),
                seek_config.replace("CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE=112456\n", "")):
    try: v.verify_storage_seek(invalid)
    except ValueError: pass
    else: raise AssertionError("missing or unbounded Tab5 seek config accepted")
defaults = (root / "hardware/boards/m5stack-tab5/sdkconfig.defaults").read_text()
v.verify_storage_seek(defaults)
mailbox_config = "CONFIG_LWIP_UDP_RECVMBOX_SIZE=32\n"
v.verify_udp_mailbox(mailbox_config)
v.verify_udp_mailbox(defaults)
for invalid in ("", mailbox_config.replace("=32", "=6"),
                mailbox_config.replace("=32", "=64"),
                mailbox_config.replace("=32", "=0"),
                mailbox_config.replace("=32", "=320"),
                "# " + mailbox_config, mailbox_config + mailbox_config):
    try: v.verify_udp_mailbox(invalid)
    except ValueError: pass
    else: raise AssertionError("missing, stale or unbounded UDP mailbox accepted")
print("Tab5 UDP mailbox verifier gates PASS (2 valid, 7 rejected)")
assert re.findall(r"^CONFIG_LWIP_UDP_RECVMBOX_SIZE=(.*)$", defaults, re.M) == ["32"], \
    "Tab5 UDP receive mailbox must keep the reviewed 32-datagram bound"

# Every cluster may be a separate fragment on an existing card. The mount's
# formatting allocation-unit hint does not constrain existing FAT geometry.
# FatFs needs two header/terminator words plus two words per fragment.
manifest = json.loads((root / "third_party/game-data.json").read_text())
wad_bytes = max(entry["size_bytes"] for entry in
                manifest["game_changers_ai_bundle"]["files"] +
                [g for g in manifest["game_data"] if g["id"] == "freedoom-0.13.0-phase-2"]
                if entry["filename"].upper().endswith(".WAD"))
words = int(re.search(r"^CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE=(\d+)$",
                      defaults, re.M)[1])
assert "CONFIG_FATFS_SECTOR_512=y" in (root / "apps/console_os/sdkconfig.defaults").read_text().splitlines()
assert words == 2 + 2 * ((wad_bytes + 511) // 512), "map must cover full fragmentation of pinned Arena and original campaign WADs"
storage = (root / "components/platform_game_storage/src/platform_game_storage.c").read_text()
max_files = int(re.search(r"GAME_STORAGE_MAX_FILES = (\d+)", storage)[1])
assert words * 4 <= 450000, "per-file map exceeded reviewed PSRAM budget"
assert words * 4 * max_files <= 3600000, "all-open-file map budget exceeded"

# Execute the real regeneration branch with isolated output and mocked build
# commands: changing defaults alone must also refresh an existing sdkconfig.
build_script = (root / "scripts/build.sh").read_text()
regen = build_script.split("# sdkconfig defaults do not override", 1)[1]
regen = regen.split("# Finder and interrupted", 1)[0]
regen = "# sdkconfig defaults do not override" + regen
with tempfile.TemporaryDirectory() as temporary:
    build = Path(temporary)
    sdkconfig = build / "sdkconfig"
    features = "CONFIG_P4_TAB5_USB_HOST=y\nCONFIG_P4_TAB5_BLE_MULTIPLAYER=y\n"
    cache_config = ("CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS=y\n"
                    "CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS_MAX_LEN=0x8000\n")
    assert all(line in defaults.splitlines() for line in cache_config.splitlines())
    complete_config = seek_config + mailbox_config + cache_config
    cases = (
        ("missing-defaults", "console_os", "m5stack-tab5", "", True, True),
        ("small-seek-map", "console_os", "m5stack-tab5", complete_config.replace("=112456", "=64"), True, True),
        ("missing-psram", "console_os", "m5stack-tab5", complete_config.replace("CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y\n", ""), True, True),
        ("missing-mailbox", "console_os", "m5stack-tab5", seek_config + cache_config, True, True),
        ("old-mailbox", "console_os", "m5stack-tab5", complete_config.replace("=32", "=6"), True, True),
        ("zero-mailbox", "console_os", "m5stack-tab5", complete_config.replace("=32", "=0"), True, True),
        ("oversized-mailbox", "console_os", "m5stack-tab5", complete_config.replace("=32", "=64"), True, True),
        ("commented-mailbox", "console_os", "m5stack-tab5", seek_config + cache_config + "# " + mailbox_config, True, True),
        ("malformed-mailbox", "console_os", "m5stack-tab5", complete_config.replace("=32", "=320"), True, True),
        ("missing-cache-policy", "console_os", "m5stack-tab5", seek_config + mailbox_config, True, True),
        ("disabled-cache-chunks", "console_os", "m5stack-tab5", complete_config.replace("CHUNKED_OPS=y", "CHUNKED_OPS=n"), True, True),
        ("missing-cache-chunk-bound", "console_os", "m5stack-tab5", complete_config.replace("CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS_MAX_LEN=0x8000\n", ""), True, True),
        ("oversized-cache-chunks", "console_os", "m5stack-tab5", complete_config.replace("MAX_LEN=0x8000", "MAX_LEN=0x10000"), True, True),
        ("zero-cache-chunks", "console_os", "m5stack-tab5", complete_config.replace("MAX_LEN=0x8000", "MAX_LEN=0"), True, True),
        ("exact-defaults", "console_os", "m5stack-tab5", complete_config, True, False),
        ("legacy-board", "console_os", "elecrow-crowpanel-advanced-10", "", True, False),
        ("other-app", "bringup", "m5stack-tab5", "", True, False),
        ("no-cache", "console_os", "m5stack-tab5", "", False, False),
    )
    for name, app, board, extra, exists, expected in cases:
        if exists:
            sdkconfig.write_text(features + extra)
        else:
            sdkconfig.unlink(missing_ok=True)
        script = '''set -eu
P4_APP=$1
P4_BOARD_PROFILE=$2
P4_BUILD_DIR=$3
P4_TAB5_USB_HOST=1
P4_TAB5_USB_CONFIG=CONFIG_P4_TAB5_USB_HOST=y
P4_TAB5_BLE_CONFIG=CONFIG_P4_TAB5_BLE_MULTIPLAYER=y
cmake() { [ "$1" = -E ] && [ "$2" = remove ] && rm -- "$3"; }
p4_idf_action() { [ "$1" = reconfigure ] && printf 'RECONFIGURED\\n'; }
''' + regen
        result = subprocess.run(["sh", "-c", script, "tab5-config-test", app, board, str(build)],
                                check=True, text=True, capture_output=True)
        assert result.stdout.count("RECONFIGURED") == int(expected), name
        assert sdkconfig.exists() == (exists and not expected), name
print(f"Tab5 bounded fast-seek/UDP mailbox/cache chunks and incremental-config gates PASS ({len(cases)} regeneration cases)")
