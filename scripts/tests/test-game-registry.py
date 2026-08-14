#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/generate-game-registry.py"


def manifest(component: str, launcher_id: int) -> dict:
    return {
        "schema": 1,
        "format": "p4-native-static-v1",
        "api_version": 1,
        "component": component,
        "entry_symbol": f"p4_{component}_game",
        "launcher_id": launcher_id,
        "id": f"org.example.{component.replace('_', '-')}",
        "title": component.upper(),
        "subtitle": "P4 GAME API V1",
        "accent_rgb565": "0x5fea",
        "required_capabilities": ["video", "controls"],
        "optional_capabilities": ["audio-tone"],
        "license": "MIT",
        "assets": "original-shapes-only",
        "enabled": True,
    }


def write_manifest(root: pathlib.Path, name: str, value: dict) -> None:
    directory = root / name
    directory.mkdir()
    (directory / "game.json").write_text(
        json.dumps(value), encoding="utf-8")


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["python3", str(GENERATOR), *arguments],
        cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        write_manifest(games, "first_game", manifest("first_game", 100))
        generated = pathlib.Path(temporary) / "generated"
        result = run(
            "--games-root", str(games),
            "--output-c", str(generated / "registry.c"),
            "--output-h", str(generated / "registry.h"),
            "--output-cmake", str(generated / "games.cmake"))
        assert result.returncode == 0, result.stderr
        assert "p4_first_game_game" in (generated / "registry.c").read_text()
        assert "first_game" in (generated / "games.cmake").read_text()

        write_manifest(games, "duplicate", manifest("duplicate", 100))
        result = run("--games-root", str(games), "--check")
        assert result.returncode != 0
        assert "duplicate launcher_id" in result.stderr

        bad = manifest("bad_caps", 101)
        bad["optional_capabilities"] = ["network"]
        write_manifest(games, "bad_caps", bad)
        result = run("--games-root", str(games), "--check")
        assert result.returncode != 0

    print("game registry tests passed")


if __name__ == "__main__":
    main()
