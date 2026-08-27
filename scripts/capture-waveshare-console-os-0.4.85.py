#!/usr/bin/env python3

"""Capture the exact Console OS 0.4.85 persistent-save candidate."""

from __future__ import annotations

import os
import pathlib
import runpy


os.environ["P4_CAPTURE_VERSION"] = "0.4.85"
os.environ["P4_CAPTURE_APPLICATION_SHA256"] = (
    "71db139c36ce043b900e71e40ff5ca2ea431811da2ab60a0cef30d4ff0e5dfda"
)
runpy.run_path(
    str(pathlib.Path(__file__).with_name(
        "capture-waveshare-console-os-0.4.84.py")),
    run_name="__main__",
)
