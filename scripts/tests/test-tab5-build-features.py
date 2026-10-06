#!/usr/bin/env python3
"""The verifier and install authorization distinguish host-on and host-off images."""
import importlib.util
from pathlib import Path
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
