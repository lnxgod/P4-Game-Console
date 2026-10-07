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
    installed = run("--games-root", str(ROOT / "games"), "--check",
                    "--require-native-resolution")
    assert installed.returncode == 0, installed.stderr
    installed_report = json.loads(installed.stdout)
    assert "org.p4console.p4-yahtzee" in \
        installed_report["multiplayer_games"]

    for optional in ([], ["video-highres"]):
        with tempfile.TemporaryDirectory() as temporary:
            games = pathlib.Path(temporary) / "games"
            games.mkdir()
            low_resolution = manifest("low_resolution", 101)
            low_resolution["optional_capabilities"] = optional
            write_manifest(games, "low_resolution", low_resolution)
            # Generic validation still supports the explicit legacy ABI.
            generic = run("--games-root", str(games), "--check")
            assert generic.returncode == 0, generic.stderr
            maintained = run("--games-root", str(games), "--check",
                             "--require-native-resolution")
            assert maintained.returncode != 0
            assert "must require video-highres" in maintained.stderr

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        native = manifest("native", 102)
        native["required_capabilities"].append("video-highres")
        write_manifest(games, "native", native)
        disabled = manifest("disabled", 103)
        disabled["enabled"] = False
        write_manifest(games, "disabled", disabled)
        draft = manifest("draft", 104)
        draft["folder"] = "GAMES/WIP"
        write_manifest(games, "draft", draft)
        maintained = run("--games-root", str(games), "--check",
                         "--require-native-resolution")
        assert maintained.returncode == 0, maintained.stderr
        assert json.loads(maintained.stdout)["enabled_games"] == [native["id"]]
        development = run("--games-root", str(games), "--check", "--dev-only",
                          "--require-native-resolution")
        assert development.returncode == 0, development.stderr
        assert set(json.loads(development.stdout)["enabled_games"]) == {
            disabled["id"], draft["id"]}

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
        extended = manifest("extended_caps", 120)
        extended["optional_capabilities"] = [
            "audio-tone",
            "save",
            "text-input",
            "realm",
            "multiplayer-session",
            "module-handoff",
            "vector-scenes",
            "video-highres",
        ]
        write_manifest(games, "extended_caps", extended)
        result = run("--games-root", str(games), "--check")
        assert result.returncode == 0, result.stderr

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        multiplayer = manifest("multiplayer", 121)
        multiplayer["optional_capabilities"].append("multiplayer-session")
        multiplayer["multiplayer"] = {
            "schema": 1,
            "style": "turn-based",
        }
        write_manifest(games, "multiplayer", multiplayer)
        result = run("--games-root", str(games), "--check")
        assert result.returncode == 0, result.stderr
        report = json.loads(result.stdout)
        assert report["multiplayer_games"] == [multiplayer["id"]]

    invalid_multiplayer = (
        {"schema": 2, "style": "turn-based"},
        {"schema": 1, "style": "cloud"},
        {"schema": 1, "style": "realtime", "min_players": 3,
         "max_players": 2},
        {"schema": 1, "style": "turn-based", "message_bytes": 65},
        {"schema": 1, "style": "realtime", "input_delay_ticks": 1},
        {"schema": 1, "style": "lockstep", "surprise": True},
    )
    for index, profile in enumerate(invalid_multiplayer):
        with tempfile.TemporaryDirectory() as temporary:
            games = pathlib.Path(temporary) / "games"
            games.mkdir()
            invalid = manifest(f"bad_mp_{index}", 130 + index)
            invalid["optional_capabilities"].append("multiplayer-session")
            invalid["multiplayer"] = profile
            write_manifest(games, f"bad_mp_{index}", invalid)
            result = run("--games-root", str(games), "--check")
            assert result.returncode != 0, profile
            assert "multiplayer" in result.stderr

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        missing_capability = manifest("missing_mp", 140)
        missing_capability["multiplayer"] = {
            "schema": 1,
            "style": "turn-based",
        }
        write_manifest(games, "missing_mp", missing_capability)
        result = run("--games-root", str(games), "--check")
        assert result.returncode != 0
        assert "multiplayer-session" in result.stderr

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

    invalid_stack_limits = (True, 0, 127, 16385, "512")
    for index, limit in enumerate(invalid_stack_limits):
        with tempfile.TemporaryDirectory() as temporary:
            games = pathlib.Path(temporary) / "games"
            games.mkdir()
            bad_stack = manifest(f"bad_stack_{index}", 240 + index)
            bad_stack["stack_frame_limit_bytes"] = limit
            write_manifest(games, f"bad_stack_{index}", bad_stack)
            result = run("--games-root", str(games), "--check")
            assert result.returncode != 0, limit
            assert "stack_frame_limit_bytes" in result.stderr

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        bounded_stack = manifest("bounded_stack", 250)
        bounded_stack["stack_frame_limit_bytes"] = 512
        write_manifest(games, "bounded_stack", bounded_stack)
        result = run("--games-root", str(games), "--check")
        assert result.returncode == 0, result.stderr

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        multi_source = manifest("multi_source", 251)
        multi_source["sources"] = ["multi_source.c", "rules.c", "network.c"]
        write_manifest(games, "multi_source", multi_source)
        result = run("--games-root", str(games), "--check")
        assert result.returncode == 0, result.stderr

    invalid_sources = ([], ["../escape.c"], ["game.cpp"], ["game.c", "game.c"])
    for index, sources in enumerate(invalid_sources):
        with tempfile.TemporaryDirectory() as temporary:
            games = pathlib.Path(temporary) / "games"
            games.mkdir()
            bad_sources = manifest(f"bad_sources_{index}", 260 + index)
            bad_sources["sources"] = sources
            write_manifest(games, f"bad_sources_{index}", bad_sources)
            result = run("--games-root", str(games), "--check")
            assert result.returncode != 0, sources
            assert "sources" in result.stderr

    for key in ("id", "launcher_id", "package_file"):
        for enabled in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                games = pathlib.Path(temporary)
                draft = manifest("new_game", 300)
                draft["enabled"] = enabled
                retired = {"id": "org.example.retired", "launcher_id": 301,
                           "package_file": "OLD.P4G"}
                retired[key] = draft[key]
                write_manifest(games, "new_game", draft)
                (games / "retired.json").write_text(json.dumps({"schema": 1, "games": [retired]}))
                result = run("--games-root", str(games), "--check")
                assert result.returncode != 0, (key, enabled)
                assert f"{key} is reserved by retired game" in result.stderr

    print("game registry tests passed")


if __name__ == "__main__":
    main()
