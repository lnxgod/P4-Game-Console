#!/usr/bin/env python3

"""Validate monorepo P4 game manifests and generate package build calls."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import tempfile
from typing import Any

from p4_multiplayer_manifest import normalize_multiplayer_profile
from p4_game_release import development_only


API_VERSION = 1
FORMAT = "p4-native-elf-v1"
COMPONENT_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SOURCE_RE = re.compile(r"^[a-z][a-z0-9_]*\.c$")
SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9.-]{2,47}$")
ACCENT_RE = re.compile(r"^0x[0-9a-fA-F]{4}$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?$")
PACKAGE_RE = re.compile(r"^[A-Z0-9][A-Z0-9_-]{0,31}\.P4G$")
RESOURCE_RE = re.compile(r"^[A-Z0-9][A-Z0-9_-]{0,31}\.P4R$")
RESOURCE_PAYLOAD_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_./-]{0,254}$")
FOLDER_RE = re.compile(
    r"^[A-Z0-9][A-Z0-9 -]{0,14}(?:/[A-Z0-9][A-Z0-9 -]{0,14})?$")
CAPABILITIES = {
    "video": "P4_GAME_CAP_VIDEO",
    "controls": "P4_GAME_CAP_CONTROLS",
    "audio-tone": "P4_GAME_CAP_AUDIO_TONE",
    "audio-stream": "P4_GAME_CAP_AUDIO_STREAM",
    "storage": "P4_GAME_CAP_STORAGE",
    "signal-scan": "P4_GAME_CAP_SIGNAL_SCAN",
    "save": "P4_GAME_CAP_SAVE",
    "text-input": "P4_GAME_CAP_TEXT_INPUT",
    "realm": "P4_GAME_CAP_REALM",
    "multiplayer-session": "P4_GAME_CAP_MULTIPLAYER_SESSION",
    "module-handoff": "P4_GAME_CAP_MODULE_HANDOFF",
    "vector-scenes": "P4_GAME_CAP_VECTOR_SCENES",
    "dice-accessory": "P4_GAME_CAP_DICE_ACCESSORY",
    "motion": "P4_GAME_CAP_MOTION",
    "video-highres": "P4_GAME_CAP_VIDEO_HIGH_RES",
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
    version = value.get("version")
    package_file = value.get("package_file")
    if not isinstance(component, str) or not COMPONENT_RE.fullmatch(component):
        fail(path, "component must be a lowercase CMake component identifier")
    sources = value.get("sources", [f"{component}.c"])
    if (not isinstance(sources, list) or not sources or len(sources) > 16 or
            any(not isinstance(source, str) or
                not SOURCE_RE.fullmatch(source) for source in sources)):
        fail(path, "sources must be an array of 1..16 safe C source basenames")
    if len(sources) != len(set(sources)):
        fail(path, "sources contains a duplicate")
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
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        fail(path, "version must be a bounded semantic version string")
    if len(version.encode("ascii")) >= 16:
        fail(path, "version must fit in 15 ASCII bytes")
    stack_frame_limit = value.get("stack_frame_limit_bytes")
    if (stack_frame_limit is not None and
            (isinstance(stack_frame_limit, bool) or
             not isinstance(stack_frame_limit, int) or
             not 128 <= stack_frame_limit <= 16 * 1024)):
        fail(path, "stack_frame_limit_bytes must be an integer in 128..16384")
    if (not isinstance(package_file, str) or
            not PACKAGE_RE.fullmatch(package_file)):
        fail(path, "package_file must be an uppercase .P4G basename")
    resource_file = value.get("resource_file")
    resource_payload = value.get("resource_payload")
    if (resource_file is None) != (resource_payload is None):
        fail(path, "resource_file and resource_payload must appear together")
    if resource_file is not None:
        if (not isinstance(resource_file, str) or
                not RESOURCE_RE.fullmatch(resource_file) or
                resource_file != package_file[:-1] + "R"):
            fail(path, "resource_file must be the matching uppercase .P4R basename")
        if (not isinstance(resource_payload, str) or
                not RESOURCE_PAYLOAD_RE.fullmatch(resource_payload)):
            fail(path, "resource_payload must be a safe relative file path")
        payload_path = path.parent / resource_payload
        try:
            payload_path.resolve().relative_to(path.parent.resolve())
        except ValueError:
            fail(path, "resource_payload escapes the game directory")
        if not payload_path.is_file():
            fail(path, f"resource_payload is missing: {resource_payload}")
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
    if resource_file is not None and "storage" not in required + optional:
        fail(path, "a resource sidecar requires the storage capability")
    try:
        value["_multiplayer_profile"] = normalize_multiplayer_profile(
            value, required + optional
        )
    except ValueError as error:
        fail(path, str(error))
    bounded_text(value, path, "license", 64)
    bounded_text(value, path, "assets", 128)
    value["_path"] = path
    value["_sources"] = sources
    return value


def discover(
    games_root: pathlib.Path, *, dev_only: bool = False,
    require_native_resolution: bool = False,
) -> list[dict[str, Any]]:
    if not games_root.is_dir():
        raise ManifestError(f"games root is not a directory: {games_root}")
    manifests = [load_manifest(path) for path in sorted(games_root.glob("*/game.json"))]
    if require_native_resolution:
        for manifest in manifests:
            # The maintained release library requires native rendering. Keep
            # disabled/WIP source and generic legacy ABI validation available.
            if (not development_only(manifest) and
                    "video-highres" not in manifest["required_capabilities"]):
                fail(manifest["_path"],
                     "maintained games must require video-highres for native "
                     "768x480 rendering; optional video-highres is insufficient")
    retired_path = games_root / "retired.json"
    if retired_path.exists():
        try:
            retired = json.loads(retired_path.read_text(encoding="utf-8"))
            if not isinstance(retired, dict) or retired.get("schema") != 1 or not isinstance(retired.get("games"), list):
                fail(retired_path, "invalid retirement registry")
            for entry in retired["games"]:
                if not isinstance(entry, dict) or any(
                    key not in entry for key in ("id", "launcher_id", "package_file")
                ):
                    fail(retired_path, "retired games must reserve id, launcher_id and package_file")
                for manifest in manifests:
                    for key in ("id", "launcher_id", "package_file"):
                        if manifest[key] == entry[key]:
                            fail(manifest["_path"], f"{key} is reserved by retired game {entry['id']}")
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            fail(retired_path, f"cannot read retirement registry: {error}")
    for key in (
        "component", "entry_symbol", "id", "launcher_id", "package_file",
        "resource_file",
    ):
        seen: dict[Any, pathlib.Path] = {}
        for manifest in manifests:
            value = manifest.get(key)
            if value is None:
                continue
            if value in seen:
                fail(manifest["_path"], f"duplicate {key} also used by {seen[value]}")
            seen[value] = manifest["_path"]
    return [manifest for manifest in manifests
            if development_only(manifest) == dev_only]


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


def cmake_text(manifests: list[dict[str, Any]], *, dev_only: bool = False) -> str:
    calls = "".join(
        f"p4_add_seed_game({json.dumps(item['package_file'])} "
        f"{json.dumps(item['component'])} "
        f"{json.dumps(item.get('resource_file', ''))} "
        f"{json.dumps(item.get('resource_payload', ''))}"
        f"{' DEV' if dev_only else ''})\n"
        for item in manifests
    )
    return "# Generated; do not edit.\n" + calls


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--games-root", type=pathlib.Path, default=pathlib.Path("games"))
    parser.add_argument("--output-cmake", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--dev-only", action="store_true",
                        help="select held-back games for the separate developer build")
    parser.add_argument(
        "--require-native-resolution", action="store_true",
        help="require video-highres in the maintained release library")
    args = parser.parse_args()
    manifests = discover(
        args.games_root.resolve(), dev_only=args.dev_only,
        require_native_resolution=args.require_native_resolution)
    if not args.check and args.output_cmake is None:
        parser.error("generation requires --output-cmake")
    if args.output_cmake is not None:
        atomic_write(args.output_cmake.resolve(), cmake_text(manifests, dev_only=args.dev_only))
    print(json.dumps({
        "result": "p4-game-manifests-valid",
        "profile": "development" if args.dev_only else "standard",
        "format": FORMAT,
        "api_version": API_VERSION,
        "enabled_games": [item["id"] for item in manifests],
        "components": [item["component"] for item in manifests],
        "packages": [item["package_file"] for item in manifests],
        "resources": [item["resource_file"] for item in manifests
                      if item.get("resource_file")],
        "multiplayer_games": [item["id"] for item in manifests
                              if item.get("_multiplayer_profile")],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ManifestError as error:
        raise SystemExit(f"game registry generation failed: {error}") from error
