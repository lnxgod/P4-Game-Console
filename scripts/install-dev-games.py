#!/usr/bin/env python3
"""Build and explicitly install one held-back Tab5 game, preserving saves."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, help="source directory, e.g. byte_buddy")
    parser.add_argument("--port", required=True, help="explicit Tab5 native USB port")
    parser.add_argument("--protected-payload-sha256",
                        help="Red Dragon payload digest from the installed OS's frozen lineage evidence")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("registry", ROOT / "scripts/generate-game-registry.py")
    registry = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(registry)
    candidates = {m["component"]: m for m in registry.discover(ROOT / "games", dev_only=True)}
    if args.game not in candidates:
        parser.error("choose a held-back game: " + ", ".join(sorted(candidates)))
    manifest = candidates[args.game]
    if args.game == "lord" and not re.fullmatch("[0-9a-f]{64}", args.protected_payload_sha256 or ""):
        parser.error("Red Dragon requires --protected-payload-sha256 from the installed OS lineage")
    environment = dict(os.environ, P4_TAB5_DEV_GAMES_ONLY="1")
    subprocess.run([str(ROOT / "scripts/build.sh"), "console_os", "m5stack-tab5"],
                   cwd=ROOT, env=environment, check=True)
    source = ROOT / "apps/console_os/build-tab5/dev-games/GAMES"
    if args.game == "lord":
        payload_digest = (source / manifest["package_file"]).read_bytes()[48:80].hex()
        if payload_digest != args.protected_payload_sha256:
            parser.error("Red Dragon payload differs from the installed OS lineage; install a paired OS first")
    # Use the Python selected by the pinned SDK (it includes pyserial), not an
    # unrelated system Python that may have started make.
    cache = (source.parents[1] / "CMakeCache.txt").read_text()
    python = re.search(r"^PYTHON:[^=]+=(.+)$", cache, re.M)
    if python is None:
        parser.error("SDK build did not record its Python runtime")
    # Use an isolated bundle so this command installs only the named game.
    with tempfile.TemporaryDirectory(prefix="p4-install-dev-") as temporary:
        bundle = Path(temporary)
        (bundle / "GAMES").mkdir()
        for key in ("resource_file", "package_file"):
            if manifest.get(key):
                shutil.copyfile(source / manifest[key], bundle / "GAMES" / manifest[key])
        subprocess.run([python.group(1), str(ROOT / "scripts/p4-transfer.py"),
                        "push-bundle", str(bundle), "--include-dev", "--port", args.port],
                       cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
