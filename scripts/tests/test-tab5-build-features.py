#!/usr/bin/env python3
"""The verifier and install authorization distinguish host-on and host-off images."""
import importlib.util
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("verify_tab5",root/"scripts/verify-console-os-tab5.py")
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
components={"platform_usb_host","platform_gamepad_usb","platform_gamepad","doom_gamepad_input"}
sources={"key_merge.c","usb_power_control.c","platform_usb_host.c","platform_gamepad_usb.c"}
symbols="\n".join("0000 T "+name for name in ("platform_tab5_usb_host_power","platform_usb_host_start","platform_usb_host_enable_root_port","platform_gamepad_usb_start","doom_gamepad_input_update","doom_key_merge_update"))
v.verify_usb_host(False,set(),set(),"")
v.verify_usb_host(True,components,sources,symbols)
for enabled,c,s,n in ((False,components,sources,symbols),(False,set(),set(),symbols),(True,set(),sources,symbols),(True,components,set(),symbols),(True,components,sources,"")):
    try:v.verify_usb_host(enabled,c,s,n)
    except ValueError:pass
    else:raise AssertionError("mismatched USB feature accepted")
print("Tab5 USB host feature gates PASS")
