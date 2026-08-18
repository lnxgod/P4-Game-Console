#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILDER = ROOT / "scripts/build-game-resource.py"


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        manifest = root / "game.json"
        payload = root / "art.bin"
        output = root / "DRAGON.P4R"
        manifest.write_text(json.dumps({
            "schema": 1,
            "id": "org.example.dragon",
            "resource_file": "DRAGON.P4R",
        }), encoding="utf-8")
        payload.write_bytes(bytes(range(251)) * 7)
        command = [
            "python3", str(BUILDER),
            "--manifest", str(manifest),
            "--payload", str(payload),
            "--output", str(output),
        ]
        first = subprocess.run(
            command, cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        assert first.returncode == 0, first.stderr
        package = output.read_bytes()
        assert package[:8] == b"P4RES01\0"
        assert struct.unpack_from("<6I", package, 8) == (
            128, len(package), 128, len(package) - 128, 1, 0,
        )
        assert package[32:64] == hashlib.sha256(package[128:]).digest()
        game_id, padding = package[64:112].split(b"\0", 1)
        assert game_id == b"org.example.dragon" and not any(padding)
        assert not any(package[112:128])

        second = subprocess.run(
            command, cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        assert second.returncode == 0, second.stderr
        assert output.read_bytes() == package

        wrong_name = subprocess.run(
            command[:-1] + [str(root / "OTHER.P4R")],
            cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        assert wrong_name.returncode != 0
        assert "output basename differs" in wrong_name.stderr

    print("game resource builder tests passed")


if __name__ == "__main__":
    main()
