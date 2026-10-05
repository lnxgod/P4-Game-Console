#!/usr/bin/env python3

"""Validate declared Console OS P4 Cart seeds and generate CMake calls."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
import pathlib
import re
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_METADATA = ROOT / "apps/console_os/app-metadata.json"
DEFAULT_TEMPLATES = ROOT / "game-platform/templates"
SEED_PATH_RE = re.compile(
    r"^P4/GAMES/[A-Z0-9][A-Z0-9-]{0,30}\.P4CART$"
)


class SeedRegistryError(RuntimeError):
    pass


@dataclass(frozen=True)
class SeedCart:
    relative_path: pathlib.PurePosixPath
    output_name: str
    template_name: str
    template_directory: pathlib.Path


def _read_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SeedRegistryError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise SeedRegistryError(f"{path} must contain a JSON object")
    return value


def load_seed_carts(
    metadata_path: pathlib.Path = DEFAULT_METADATA,
    templates_root: pathlib.Path = DEFAULT_TEMPLATES,
) -> tuple[SeedCart, ...]:
    metadata = _read_json(metadata_path)
    legacy = metadata.get("legacy_p4cart")
    if not isinstance(legacy, dict):
        raise SeedRegistryError("legacy_p4cart metadata is missing")
    paths = legacy.get("seed_carts")
    if (not isinstance(paths, list) or not paths or len(paths) > 16 or
            any(not isinstance(path, str) for path in paths)):
        raise SeedRegistryError("seed_carts must contain 1..16 paths")
    if len(paths) != len(set(paths)):
        raise SeedRegistryError("seed_carts contains a duplicate path")

    available_templates: set[str] = set()
    for manifest_path in sorted(templates_root.glob("*/p4.json")):
        manifest = _read_json(manifest_path)
        game = manifest.get("game")
        template_name = manifest_path.parent.name
        if (manifest.get("format") != "p4-cart-source-v1" or
                not isinstance(game, dict) or
                game.get("slug") != template_name):
            raise SeedRegistryError(
                f"{manifest_path} does not define {template_name!r}"
            )
        available_templates.add(template_name)

    seeds: list[SeedCart] = []
    for value in paths:
        if not SEED_PATH_RE.fullmatch(value):
            raise SeedRegistryError(f"unsafe P4 Cart seed path: {value!r}")
        relative = pathlib.PurePosixPath(value)
        output_name = relative.name
        template_name = output_name.removesuffix(".P4CART").lower()
        template = templates_root / template_name
        manifest_path = template / "p4.json"
        manifest = _read_json(manifest_path)
        game = manifest.get("game")
        if (manifest.get("format") != "p4-cart-source-v1" or
                not isinstance(game, dict) or
                game.get("slug") != template_name):
            raise SeedRegistryError(
                f"{manifest_path} does not define {template_name!r}"
            )
        seeds.append(SeedCart(relative, output_name, template_name, template))

    declared_templates = {seed.template_name for seed in seeds}
    undeclared_templates = sorted(available_templates - declared_templates)
    if undeclared_templates:
        raise SeedRegistryError(
            "source cartridge templates are not declared in seed_carts: " +
            ", ".join(undeclared_templates)
        )

    first = legacy.get("seed_cart")
    if first is not None and first != paths[0]:
        raise SeedRegistryError("legacy seed_cart must match seed_carts[0]")
    return tuple(seeds)


def cmake_text(seeds: tuple[SeedCart, ...]) -> str:
    calls = "".join(
        f'p4_add_script_cart_seed("{seed.output_name}" '
        f'"{seed.template_name}")\n'
        for seed in seeds
    )
    return "# Generated; do not edit.\n" + calls


def atomic_write(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=path.name + ".", dir=path.parent
    )
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metadata", type=pathlib.Path,
                        default=DEFAULT_METADATA)
    parser.add_argument("--templates-root", type=pathlib.Path,
                        default=DEFAULT_TEMPLATES)
    parser.add_argument("--output-cmake", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    if not arguments.check and arguments.output_cmake is None:
        parser.error("generation requires --output-cmake")
    seeds = load_seed_carts(
        arguments.metadata.resolve(), arguments.templates_root.resolve()
    )
    if arguments.output_cmake is not None:
        atomic_write(arguments.output_cmake.resolve(), cmake_text(seeds))
    print(json.dumps({
        "result": "p4cart-seed-registry-valid",
        "seed_carts": [str(seed.relative_path) for seed in seeds],
        "templates": [seed.template_name for seed in seeds],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SeedRegistryError as error:
        raise SystemExit(f"P4 Cart seed registry failed: {error}") from error
