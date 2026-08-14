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
        assert manifest["format"] == "p4-native-static-v1"
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
            CREATOR, "Moon Run", "--games-root", str(games), "--dry-run")
        assert dry_run.returncode == 0, dry_run.stderr
        assert not (games / "moon_run").exists()
        assert json.loads(dry_run.stdout)["launcher_id"] == 101

        invalid = run(
            CREATOR, "Bad/Game", "--games-root", str(games))
        assert invalid.returncode != 0

    print("new game creator tests passed")


if __name__ == "__main__":
    main()
