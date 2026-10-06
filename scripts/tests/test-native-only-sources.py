#!/usr/bin/env python3
"""Keep retired game-creation sources out of the maintained repository."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for retired in (
    "game-platform", "scripts/lua", "third_party/lua-5.4.8",
    "third_party/lua-5.4.8.manifest", "components/p4_script_audio",
    "components/p4_script_renderer", "components/p4_content_catalog",
    ".agents/skills/develop-p4-script-games",
):
    path = ROOT / retired
    # Empty directories left by version control contain no authoring/runtime code.
    assert not path.is_symlink() and not path.is_file() and not (
        path.is_dir() and any(item.is_file() or item.is_symlink()
                             for item in path.rglob("*"))), retired

games = []
for path in sorted((ROOT / "games").glob("*/game.json")):
    manifest = json.loads(path.read_text())
    assert manifest["format"] == "p4-native-elf-v1", str(path)
    sources = manifest.get("sources", [manifest["component"] + ".c"])
    assert sources and all(source.endswith(".c") for source in sources), str(path)
    assert all((path.parent / "src" / source).is_file() for source in sources), str(path)
    assert not list(path.parent.rglob("*.lua")), str(path)
    if manifest.get("enabled"):
        games.append(manifest["id"])
for required in ("org.p4console.maze-chase", "org.p4console.byte-buddy", "org.p4console.lord"):
    assert required in games, required
assert "lua_5_4" not in json.loads((ROOT / "third_party/source-lock.json").read_text())["sources"]
print(json.dumps({"result": "native-only-source-audit-pass", "enabled_native_games": len(games)}))
