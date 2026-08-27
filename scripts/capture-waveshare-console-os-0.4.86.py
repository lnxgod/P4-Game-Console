#!/usr/bin/env python3

"""Capture the exact Console OS 0.4.86 activation diagnostic."""

from __future__ import annotations

import os
import pathlib
import runpy


os.environ["P4_CAPTURE_VERSION"] = "0.4.86"
os.environ["P4_CAPTURE_APPLICATION_SHA256"] = (
    "0c11bec0a793c5a402a67889495905514a67ead6b6465df7697b0b6256efb12e"
)
runpy.run_path(
    str(pathlib.Path(__file__).with_name(
        "capture-waveshare-console-os-0.4.84.py")),
    run_name="__main__",
)
