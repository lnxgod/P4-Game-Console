#!/usr/bin/env python3
"""Structural tests for the acyclic E6 issuance trust root."""

from __future__ import annotations

import hashlib
import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
ROUTE_PATH = ROOT / "scripts/doom-e6-authorized-route.py"
INSTALLER_PATH = ROOT / "scripts/doom-e6-install.py"


def load_route():
    spec = importlib.util.spec_from_file_location("doom_e6_route_test", ROUTE_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


route = load_route()
source = ROUTE_PATH.read_text(encoding="utf-8")
installer_source = INSTALLER_PATH.read_text(encoding="utf-8")

# The independently issued digest is intentionally absent. The wrapper refuses
# before it loads or executes any installer/runtime byte.
assert route.ISSUED_AUTH_SHA256 == "0" * 64
try:
    route._require_issued()
except route.RouteError as error:
    assert "has not been independently issued" in str(error)
else:
    raise AssertionError("unissued E6 route passed")
assert source.index("issued_digest = _require_issued()") < source.index(
    "installer = _load_frozen_installer()"
)

# The outer route—not a CLI argument—owns both trust anchors.
assert "--authorization-sha256" not in source
assert route.EXPECTED_INSTALLER_SHA256 == hashlib.sha256(
    INSTALLER_PATH.read_bytes()
).hexdigest()
assert 'ISSUED_AUTH_SHA256 = "0" * 64' in source

# Direct installer execution is recovery-only; the generic caller cannot
# choose its own authorization digest and reach mutation.
assert 'choices=("recover",)' in installer_source
assert 'add_parser("install")' not in installer_source
assert "def install_from_trust_anchor(" in installer_source

# Any installer drift is rejected by the wrapper before import/execution.
original = route.EXPECTED_INSTALLER_SHA256
route.EXPECTED_INSTALLER_SHA256 = "f" * 64
try:
    route._load_frozen_installer()
except route.RouteError as error:
    assert "installer bytes changed" in str(error)
else:
    raise AssertionError("mutated installer binding passed")
finally:
    route.EXPECTED_INSTALLER_SHA256 = original

print("E6 acyclic authorized-route tests PASS")
