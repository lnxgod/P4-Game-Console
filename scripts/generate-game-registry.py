#!/usr/bin/env python3

"""Validate monorepo P4 game manifests and generate the static registry."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import tempfile
from typing import Any


API_VERSION = 1
FORMAT = "p4-native-static-v1"
COMPONENT_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9.-]{2,47}$")
ACCENT_RE = re.compile(r"^0x[0-9a-fA-F]{4}$")
FOLDER_RE = re.compile(
    r"^[A-Z0-9][A-Z0-9 -]{0,14}(?:/[A-Z0-9][A-Z0-9 -]{0,14})?$")
CAPABILITIES = {
    "video": "P4_GAME_CAP_VIDEO",
    "controls": "P4_GAME_CAP_CONTROLS",
    "audio-tone": "P4_GAME_CAP_AUDIO_TONE",
    "audio-stream": "P4_GAME_CAP_AUDIO_STREAM",
    "storage": "P4_GAME_CAP_STORAGE",
}


class ManifestError(RuntimeError):
    pass


def fail(path: pathlib.Path, message: str) -> None:
    raise ManifestError(f"{path}: {message}")


def bounded_text(
    manifest: dict[str, Any], path: pathlib.Path, key: str, maximum: int,
) -> str:
    value = manifest.get(key)
    if not isinstance(value, str) or not value:
        fail(path, f"{key} must be a non-empty string")
    if len(value.encode("utf-8")) >= maximum:
        fail(path, f"{key} must fit in {maximum - 1} UTF-8 bytes")
    return value


def capability_list(
    manifest: dict[str, Any], path: pathlib.Path, key: str,
) -> list[str]:
    value = manifest.get(key)
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        fail(path, f"{key} must be an array of capability names")
    if len(value) != len(set(value)):
        fail(path, f"{key} contains a duplicate")
    unknown = sorted(set(value) - set(CAPABILITIES))
    if unknown:
        fail(path, f"{key} contains unknown capabilities: {unknown}")
    return value


def load_manifest(path: pathlib.Path) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        value = json.loads(raw.decode("utf-8", "strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(path, f"cannot read strict UTF-8 JSON: {error}")
    if not isinstance(value, dict):
        fail(path, "root must be a JSON object")
    if value.get("schema") != 1:
        fail(path, "schema must be 1")
    if value.get("format") != FORMAT:
        fail(path, f"format must be {FORMAT!r}")
    if value.get("api_version") != API_VERSION:
        fail(path, f"api_version must be {API_VERSION}")
    if not isinstance(value.get("enabled"), bool):
        fail(path, "enabled must be a boolean")
    component = value.get("component")
    symbol = value.get("entry_symbol")
    game_id = value.get("id")
    launcher_id = value.get("launcher_id")
    accent = value.get("accent_rgb565")
    if not isinstance(component, str) or not COMPONENT_RE.fullmatch(component):
        fail(path, "component must be a lowercase CMake component identifier")
    if not isinstance(symbol, str) or not SYMBOL_RE.fullmatch(symbol):
        fail(path, "entry_symbol must be a C identifier")
    if not isinstance(game_id, str) or not ID_RE.fullmatch(game_id):
        fail(path, "id must be a 3..48 byte lowercase reverse-domain style ID")
    if isinstance(launcher_id, bool) or not isinstance(launcher_id, int) or not (
        100 <= launcher_id <= 0xFFFFFFFF
    ):
        fail(path, "launcher_id must be an integer in 100..4294967295")
    if not isinstance(accent, str) or not ACCENT_RE.fullmatch(accent):
        fail(path, "accent_rgb565 must be a four-digit hexadecimal string")
    bounded_text(value, path, "title", 16)
    bounded_text(value, path, "subtitle", 32)
    folder = bounded_text(value, path, "folder", 32)
    if not FOLDER_RE.fullmatch(folder):
        fail(path, "folder must contain one or two uppercase 1..15 byte segments")
    required = capability_list(value, path, "required_capabilities")
    optional = capability_list(value, path, "optional_capabilities")
    overlap = sorted(set(required) & set(optional))
    if overlap:
        fail(path, f"required and optional capabilities overlap: {overlap}")
    if "video" not in required:
        fail(path, "video must be a required capability")
    bounded_text(value, path, "license", 64)
    bounded_text(value, path, "assets", 128)
    value["_path"] = path
    return value


def discover(games_root: pathlib.Path) -> list[dict[str, Any]]:
    if not games_root.is_dir():
        raise ManifestError(f"games root is not a directory: {games_root}")
    manifests = [load_manifest(path) for path in sorted(games_root.glob("*/game.json"))]
    enabled = [manifest for manifest in manifests if manifest["enabled"]]
    for key in ("component", "entry_symbol", "id", "launcher_id"):
        seen: dict[Any, pathlib.Path] = {}
        for manifest in enabled:
            value = manifest[key]
            if value in seen:
                fail(manifest["_path"], f"duplicate {key} also used by {seen[value]}")
            seen[value] = manifest["_path"]
    return enabled


def atomic_write(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def header_text() -> str:
    return """// Generated by scripts/generate-game-registry.py; do not edit.\n\
#ifndef P4_GENERATED_GAME_REGISTRY_H\n\
#define P4_GENERATED_GAME_REGISTRY_H\n\
\n\
#include <stddef.h>\n\
#include <stdint.h>\n\
\n\
#include \"p4/game.h\"\n\
\n\
extern const p4_game_descriptor_t *const p4_generated_games[];\n\
extern const char *const p4_generated_game_folders[];\n\
extern const size_t p4_generated_game_count;\n\
const p4_game_descriptor_t *p4_generated_game_by_launcher_id(uint32_t id);\n\
\n\
#endif\n"""


def source_text(manifests: list[dict[str, Any]]) -> str:
    declarations = "".join(
        f"extern const p4_game_descriptor_t {item['entry_symbol']};\n"
        for item in manifests
    )
    if manifests:
        entries = "".join(
            f"    &{item['entry_symbol']},\n" for item in manifests
        )
        folders = "".join(
            f"    {json.dumps(item['folder'])},\n" for item in manifests
        )
    else:
        entries = "    NULL,\n"
        folders = "    NULL,\n"
    count = len(manifests)
    return f"""// Generated by scripts/generate-game-registry.py; do not edit.\n
#include \"p4_game_registry.h\"\n
{declarations}\
const p4_game_descriptor_t *const p4_generated_games[] = {{\n
{entries}}};\n
const char *const p4_generated_game_folders[] = {{\n
{folders}}};\n
const size_t p4_generated_game_count = {count}U;\n
const p4_game_descriptor_t *p4_generated_game_by_launcher_id(uint32_t id)\n
{{\n
    for (size_t i = 0U; i < p4_generated_game_count; ++i) {{\n
        if (p4_generated_games[i] != NULL &&\n
            p4_generated_games[i]->launcher_id == id) {{\n
            return p4_generated_games[i];\n
        }}\n
    }}\n
    return NULL;\n
}}\n"""


def cmake_text(manifests: list[dict[str, Any]]) -> str:
    components = " ".join(item["component"] for item in manifests)
    return (
        "# Generated by scripts/generate-game-registry.py; do not edit.\n"
        f"set(P4_GENERATED_GAME_COMPONENTS {components})\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--games-root", type=pathlib.Path, default=pathlib.Path("games"))
    parser.add_argument("--output-c", type=pathlib.Path)
    parser.add_argument("--output-h", type=pathlib.Path)
    parser.add_argument("--output-cmake", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    manifests = discover(args.games_root.resolve())
    outputs = (args.output_c, args.output_h, args.output_cmake)
    if not args.check and any(path is None for path in outputs):
        parser.error("generation requires --output-c, --output-h, and --output-cmake")
    if args.output_h is not None:
        atomic_write(args.output_h.resolve(), header_text())
    if args.output_c is not None:
        atomic_write(args.output_c.resolve(), source_text(manifests))
    if args.output_cmake is not None:
        atomic_write(args.output_cmake.resolve(), cmake_text(manifests))
    print(json.dumps({
        "result": "p4-game-registry-valid",
        "format": FORMAT,
        "api_version": API_VERSION,
        "enabled_games": [item["id"] for item in manifests],
        "components": [item["component"] for item in manifests],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ManifestError as error:
        raise SystemExit(f"game registry generation failed: {error}") from error
