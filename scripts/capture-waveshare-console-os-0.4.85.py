#!/usr/bin/env python3

"""Capture the exact Console OS 0.4.85 persistent-save candidate."""

from __future__ import annotations

import os
import pathlib
import runpy


os.environ["P4_CAPTURE_VERSION"] = "0.4.85"
os.environ["P4_CAPTURE_APPLICATION_SHA256"] = (
    "efb827cb202f3d35e8b456b05039a2a4e7a2f928eec69e7de78faffade503cf5"
)
runpy.run_path(
    str(pathlib.Path(__file__).with_name(
        "capture-waveshare-console-os-0.4.84.py")),
    run_name="__main__",
)
