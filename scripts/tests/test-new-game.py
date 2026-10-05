#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
CREATOR = ROOT / "scripts/new-game.py"
GENERATOR = ROOT / "scripts/generate-game-registry.py"


def run(script: pathlib.Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["python3", str(script), *arguments], cwd=ROOT, check=False,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary) / "games"
        games.mkdir()
        result = run(CREATOR, "Star Hop", "--games-root", str(games))
        assert result.returncode == 0, result.stderr
        report = json.loads(result.stdout)
        assert report["component"] == "star_hop"
        assert report["launcher_id"] == 100
        created = games / "star_hop"
        assert (created / "CMakeLists.txt").is_file()
        assert (created / "src/star_hop.c").is_file()
        manifest = json.loads((created / "game.json").read_text())
        assert manifest["title"] == "Star Hop"
        assert manifest["enabled"] is False
        assert manifest["subtitle"] == "WORK IN PROGRESS"
        assert manifest["format"] == "p4-native-elf-v1"
        assert manifest["version"] == "1.0.0"
        assert manifest["package_file"] == "STAR_HOP.P4G"
        assert manifest["folder"] == "GAMES/ARCADE"
        assert "GAMES/ARCADE" in (created / "README.md").read_text()
        compile_result = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
             "-Wconversion", "-Wshadow", "-Werror",
             "-I", str(ROOT / "components/p4_game_api/include"),
             "-c", str(created / "src/star_hop.c"),
             "-o", str(pathlib.Path(temporary) / "star_hop.o")],
            cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert compile_result.returncode == 0, compile_result.stderr

        check = run(GENERATOR, "--games-root", str(games), "--check")
        assert check.returncode == 0, check.stderr
        duplicate = run(CREATOR, "Star Hop", "--games-root", str(games))
        assert duplicate.returncode != 0

        dry_run = run(
            CREATOR, "Moon Run", "--optional-capability", "audio-tone",
            "--optional-capability", "save",
            "--games-root", str(games), "--dry-run")
        assert dry_run.returncode == 0, dry_run.stderr
        assert not (games / "moon_run").exists()
        dry_report = json.loads(dry_run.stdout)
        assert dry_report["launcher_id"] == 101
        assert dry_report["optional_capabilities"] == ["audio-tone", "save"]

        capability_created = run(
            CREATOR, "Save Test", "--optional-capability", "save",
            "--games-root", str(games))
        assert capability_created.returncode == 0, capability_created.stderr
        capability_source = (games / "save_test/src/save_test.c").read_text()
        assert "P4_GAME_CAP_SAVE" in capability_source
        assert "P4_GAME_CAP_AUDIO_TONE" not in capability_source

        duplicate_capability = run(
            CREATOR, "Bad Save", "--optional-capability", "save",
            "--optional-capability", "save",
            "--games-root", str(games), "--dry-run")
        assert duplicate_capability.returncode != 0

        folder_dry_run = run(
            CREATOR, "Puzzle Box", "--folder", "GAMES/PUZZLE",
            "--games-root", str(games), "--dry-run")
        assert folder_dry_run.returncode == 0, folder_dry_run.stderr
        assert json.loads(folder_dry_run.stdout)["folder"] == "GAMES/PUZZLE"

        multiplayer_dry_run = run(
            CREATOR, "Dice Link", "--multiplayer", "turn-based",
            "--games-root", str(games), "--dry-run")
        assert multiplayer_dry_run.returncode == 0, multiplayer_dry_run.stderr
        multiplayer_report = json.loads(multiplayer_dry_run.stdout)
        assert multiplayer_report["multiplayer"]["style"] == "turn-based"
        assert "multiplayer-session" in multiplayer_report[
            "optional_capabilities"]

        multiplayer_created = run(
            CREATOR, "Net Dash", "--multiplayer", "realtime",
            "--games-root", str(games))
        assert multiplayer_created.returncode == 0, multiplayer_created.stderr
        multiplayer_manifest = json.loads(
            (games / "net_dash/game.json").read_text())
        assert multiplayer_manifest["multiplayer"] == {
            "schema": 1,
            "style": "realtime",
            "min_players": 2,
            "max_players": 2,
            "protocol": 1,
        }
        multiplayer_compile = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
             "-Wconversion", "-Wshadow", "-Werror",
             "-I", str(ROOT / "components/p4_game_api/include"),
             "-c", str(games / "net_dash/src/net_dash.c"),
             "-o", str(pathlib.Path(temporary) / "net_dash.o")],
            cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert multiplayer_compile.returncode == 0, multiplayer_compile.stderr
        multiplayer_check = run(
            GENERATOR, "--games-root", str(games), "--check")
        assert multiplayer_check.returncode == 0, multiplayer_check.stderr

        high_res_created = run(
            CREATOR, "Card Table", "--high-res",
            "--games-root", str(games))
        assert high_res_created.returncode == 0, high_res_created.stderr
        high_res_manifest = json.loads(
            (games / "card_table/game.json").read_text())
        assert "video-highres" in high_res_manifest[
            "optional_capabilities"]
        high_res_source = (games / "card_table/src/card_table.c").read_text()
        assert "P4_GAME_CAP_VIDEO_HIGH_RES" in high_res_source
        high_res_compile = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
             "-Wconversion", "-Wshadow", "-Werror",
             "-I", str(ROOT / "components/p4_game_api/include"),
             "-c", str(games / "card_table/src/card_table.c"),
             "-o", str(pathlib.Path(temporary) / "card_table.o")],
            cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert high_res_compile.returncode == 0, high_res_compile.stderr
        high_res_check = run(
            GENERATOR, "--games-root", str(games), "--check")
        assert high_res_check.returncode == 0, high_res_check.stderr

        invalid_folder = run(
            CREATOR, "Bad Folder", "--folder", "GAMES/TOO/DEEP",
            "--games-root", str(games), "--dry-run")
        assert invalid_folder.returncode != 0

        invalid = run(
            CREATOR, "Bad/Game", "--games-root", str(games))
        assert invalid.returncode != 0

    with tempfile.TemporaryDirectory() as temporary:
        games = pathlib.Path(temporary)
        (games / "retired.json").write_text(json.dumps({"schema": 1, "games": [{
            "id": "org.p4console.asteroids", "launcher_id": 103,
            "package_file": "ASTEROID.P4G"}]}))
        retired = run(CREATOR, "Asteroids", "--games-root", str(games))
        assert retired.returncode != 0 and "reserved by a retired game" in retired.stderr
        assert not (games / "asteroids").exists()

    print("new game creator tests passed")


if __name__ == "__main__":
    main()
