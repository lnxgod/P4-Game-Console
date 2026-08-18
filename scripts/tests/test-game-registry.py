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
        "format": "p4-native-elf-v1",
        "api_version": 1,
        "version": "1.0.0",
        "package_file": f"{component.upper()}.P4G",
        "component": component,
        "entry_symbol": f"p4_{component}_game",
        "launcher_id": launcher_id,
        "id": f"org.example.{component.replace('_', '-')}",
        "title": component.upper(),
        "subtitle": "P4 GAME API V1",
        "folder": "GAMES/ARCADE",
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
            "--output-cmake", str(generated / "games.cmake"))
        assert result.returncode == 0, result.stderr
        cmake = (generated / "games.cmake").read_text()
        assert ('p4_add_seed_game("FIRST_GAME.P4G" "first_game" "" "")'
                in cmake)

        write_manifest(games, "duplicate", manifest("duplicate", 100))
        result = run("--games-root", str(games), "--check")
        assert result.returncode != 0
        assert "duplicate launcher_id" in result.stderr

        bad = manifest("bad_caps", 101)
        bad["optional_capabilities"] = ["network"]
        write_manifest(games, "bad_caps", bad)
        result = run("--games-root", str(games), "--check")
        assert result.returncode != 0

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        with_resource = manifest("dragon", 150)
        with_resource["resource_file"] = "DRAGON.P4R"
        with_resource["resource_payload"] = "assets/dragon.bin"
        with_resource["optional_capabilities"].append("storage")
        write_manifest(games, "dragon", with_resource)
        assets = games / "dragon/assets"
        assets.mkdir()
        (assets / "dragon.bin").write_bytes(b"dragon-art")
        generated = pathlib.Path(temporary) / "games.cmake"
        result = run("--games-root", str(games),
                     "--output-cmake", str(generated))
        assert result.returncode == 0, result.stderr
        assert ('p4_add_seed_game("DRAGON.P4G" "dragon" "DRAGON.P4R" '
                '"assets/dragon.bin")' in generated.read_text())

    invalid_resources = (
        {"resource_file": "BAD.P4R"},
        {"resource_payload": "assets/art.bin"},
        {"resource_file": "OTHER.P4R", "resource_payload": "assets/art.bin"},
        {"resource_file": "BAD_RESOURCE_3.P4R",
         "resource_payload": "assets/missing.bin"},
    )
    for index, fields in enumerate(invalid_resources):
        with tempfile.TemporaryDirectory() as temporary:
            games = pathlib.Path(temporary) / "games"
            games.mkdir()
            bad_resource = manifest(f"bad_resource_{index}", 160 + index)
            bad_resource.update(fields)
            bad_resource["optional_capabilities"].append("storage")
            write_manifest(games, f"bad_resource_{index}", bad_resource)
            result = run("--games-root", str(games), "--check")
            assert result.returncode != 0, fields

    invalid_folders = (
        None,
        "",
        "/GAMES",
        "GAMES/",
        "GAMES//ARCADE",
        "GAMES/ARCADE/MAZE",
        "games/arcade",
        "GAMES/THIS TITLE IS TOO LONG",
    )
    for index, folder in enumerate(invalid_folders):
        with tempfile.TemporaryDirectory() as temporary:
            games = pathlib.Path(temporary) / "games"
            games.mkdir()
            bad_folder = manifest(f"bad_folder_{index}", 200 + index)
            if folder is None:
                del bad_folder["folder"]
            else:
                bad_folder["folder"] = folder
            write_manifest(games, f"bad_folder_{index}", bad_folder)
            result = run("--games-root", str(games), "--check")
            assert result.returncode != 0, folder
            assert "folder" in result.stderr

    print("game registry tests passed")


if __name__ == "__main__":
    main()
