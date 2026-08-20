#!/usr/bin/env python3

"""Build one deterministic ESP32-P4 ELF cartridge and wrap it as .P4G."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import tempfile
from typing import Any


MAGIC = b"P4GAME1\0"
HEADER_BYTES = 256
FORMAT = "p4-native-elf-v1"
API_VERSION = 1
CAPABILITIES = {
    "video": 1 << 0,
    "controls": 1 << 1,
    "audio-tone": 1 << 2,
    "audio-stream": 1 << 3,
    "storage": 1 << 4,
    "signal-scan": 1 << 5,
    "save": 1 << 6,
    "text-input": 1 << 7,
    "realm": 1 << 8,
    "multiplayer-session": 1 << 9,
    "module-handoff": 1 << 10,
    "vector-scenes": 1 << 11,
}
ID_RE = re.compile(r"[a-z][a-z0-9.-]{2,47}\Z")
SYMBOL_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
COMPONENT_RE = re.compile(r"[a-z][a-z0-9_]*\Z")
FOLDER_RE = re.compile(
    r"[A-Z0-9][A-Z0-9 -]{0,14}(?:/[A-Z0-9][A-Z0-9 -]{0,14})?\Z"
)
VERSION_RE = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?\Z")
ALLOWED_UNDEFINED_SYMBOLS = {
    "calloc", "free", "memcmp", "memcpy", "memset", "strcmp",
}


class PackageError(RuntimeError):
    pass


def validate_elf_imports(path: pathlib.Path, compiler: pathlib.Path) -> None:
    readelf = compiler.with_name("riscv32-esp-elf-readelf")
    if not readelf.is_file():
        raise PackageError(f"required ELF inspector is missing: {readelf}")
    inspected = subprocess.run(
        [str(readelf), "--wide", "--symbols", str(path)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    undefined: set[str] = set()
    for line in inspected.stdout.splitlines():
        fields = line.split()
        if "UND" not in fields or not fields or fields[-1] == "UND":
            continue
        undefined.add(fields[-1].split("@", 1)[0])
    unsupported = sorted(undefined - ALLOWED_UNDEFINED_SYMBOLS)
    if unsupported:
        raise PackageError(
            "ELF imports unsupported runtime symbols: " + ", ".join(unsupported)
        )


def text_field(manifest: dict[str, Any], key: str, width: int) -> bytes:
    value = manifest.get(key)
    if not isinstance(value, str) or not value:
        raise PackageError(f"{key} must be a non-empty string")
    encoded = value.encode("ascii", "strict")
    if len(encoded) >= width:
        raise PackageError(f"{key} must fit in {width - 1} ASCII bytes")
    return encoded + bytes(width - len(encoded))


def capability_mask(manifest: dict[str, Any], key: str) -> int:
    values = manifest.get(key)
    if not isinstance(values, list) or any(
        not isinstance(value, str) for value in values
    ):
        raise PackageError(f"{key} must be an array of strings")
    if len(values) != len(set(values)):
        raise PackageError(f"{key} contains a duplicate")
    unknown = set(values) - set(CAPABILITIES)
    if unknown:
        raise PackageError(f"{key} contains unknown values: {sorted(unknown)}")
    return sum(CAPABILITIES[value] for value in values)


def load_manifest(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError(f"cannot read manifest: {error}") from error
    if not isinstance(value, dict) or value.get("schema") != 1:
        raise PackageError("manifest schema must be 1")
    if value.get("format") != FORMAT or value.get("api_version") != API_VERSION:
        raise PackageError(f"manifest must target {FORMAT} API {API_VERSION}")
    if value.get("enabled") is not True:
        raise PackageError("manifest must describe an enabled game")
    component = value.get("component")
    symbol = value.get("entry_symbol")
    game_id = value.get("id")
    folder = value.get("folder")
    package_file = value.get("package_file")
    if not isinstance(component, str) or not COMPONENT_RE.fullmatch(component):
        raise PackageError("component is invalid")
    if not isinstance(symbol, str) or not SYMBOL_RE.fullmatch(symbol):
        raise PackageError("entry_symbol is invalid")
    if not isinstance(game_id, str) or not ID_RE.fullmatch(game_id):
        raise PackageError("id is invalid")
    if not isinstance(folder, str) or not FOLDER_RE.fullmatch(folder):
        raise PackageError("folder is invalid")
    if not isinstance(package_file, str) or not re.fullmatch(
        r"[A-Z0-9][A-Z0-9_-]{0,31}\.P4G", package_file
    ):
        raise PackageError("package_file must be a bounded uppercase .P4G name")
    version = value.get("version")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise PackageError("version must be a semantic version string")
    stack_frame_limit = value.get("stack_frame_limit_bytes")
    if (stack_frame_limit is not None and
            (isinstance(stack_frame_limit, bool) or
             not isinstance(stack_frame_limit, int) or
             not 128 <= stack_frame_limit <= 16 * 1024)):
        raise PackageError(
            "stack_frame_limit_bytes must be an integer in 128..16384"
        )
    launcher_id = value.get("launcher_id")
    if isinstance(launcher_id, bool) or not isinstance(launcher_id, int) or not (
        100 <= launcher_id <= 0xFFFFFFFF
    ):
        raise PackageError("launcher_id is invalid")
    accent = value.get("accent_rgb565")
    if not isinstance(accent, str) or not re.fullmatch(r"0x[0-9a-fA-F]{4}", accent):
        raise PackageError("accent_rgb565 is invalid")
    required = capability_mask(value, "required_capabilities")
    optional = capability_mask(value, "optional_capabilities")
    if not required & CAPABILITIES["video"] or required & optional:
        raise PackageError("video is required and capability sets must not overlap")
    for key, width in (
        ("id", 48), ("title", 16), ("subtitle", 32), ("folder", 32),
        ("version", 16), ("license", 16),
    ):
        text_field(value, key, width)
    value["_required_mask"] = required
    value["_optional_mask"] = optional
    return value


def build_elf(
    root: pathlib.Path,
    manifest: dict[str, Any],
    compiler: pathlib.Path,
    strip_tool: pathlib.Path,
    output: pathlib.Path,
) -> bytes:
    component = manifest["component"]
    game_dir = root / "games" / component
    source = game_dir / "src" / f"{component}.c"
    inputs = [
        root / "game-platform" / "runtime" / "cartridge_main.c",
        source,
        root / "components" / "p4_cp437" / "src" / "cp437.c",
        root / "components" / "p4_game_api" / "src" / "audio_pack.c",
        root / "components" / "p4_game_api" / "src" / "draw.c",
        root / "components" / "p4_game_api" / "src" / "feedback.c",
        root / "components" / "p4_game_api" / "src" / "game_runtime.c",
        root / "components" / "p4_game_api" / "src" / "input.c",
        root / "components" / "p4_game_api" / "src" / "visual.c",
    ]
    for item in inputs:
        if not item.is_file():
            raise PackageError(f"required source is missing: {item}")
    with tempfile.TemporaryDirectory(prefix="p4-cartridge-") as temporary:
        unstripped = pathlib.Path(temporary) / "game.app.elf"
        command = [
            str(compiler),
            "-std=gnu17",
            "-Os",
            "-march=rv32imafc_zicsr_zifencei",
            "-mabi=ilp32f",
            "-fPIC",
            "-shared",
            "-nostdlib",
            "-nostartfiles",
            "-static-libgcc",
            "-ffunction-sections",
            "-fdata-sections",
            "-fvisibility=hidden",
            "-fno-jump-tables",
            "-Wl,--gc-sections",
            "-Wl,--allow-shlib-undefined",
            "-Wl,--build-id=none",
            "-Wl,-e,app_main",
            *(
                [f"-Werror=frame-larger-than={manifest['stack_frame_limit_bytes']}"]
                if "stack_frame_limit_bytes" in manifest else []
            ),
            f"-DP4_GAME_ENTRY_SYMBOL={manifest['entry_symbol']}",
            f"-I{root / 'components' / 'p4_game_api' / 'include'}",
            f"-I{root / 'components' / 'p4_cp437' / 'include'}",
            f"-I{game_dir / 'include'}",
            f"-I{game_dir / 'src'}",
            *(str(item) for item in inputs),
            "-lgcc",
            "-o",
            str(unstripped),
        ]
        subprocess.run(command, check=True)
        subprocess.run(
            [
                str(strip_tool),
                "--strip-unneeded",
                "--remove-section=.comment",
                "--remove-section=.riscv.attributes",
                str(unstripped),
                "-o",
                str(output),
            ],
            check=True,
        )
        validate_elf_imports(output, compiler)
        return output.read_bytes()


def build_header(manifest: dict[str, Any], payload: bytes) -> bytes:
    if len(payload) == 0 or HEADER_BYTES + len(payload) > 512 * 1024:
        raise PackageError("ELF payload is outside the package size bound")
    header = bytearray(HEADER_BYTES)
    header[0:8] = MAGIC
    struct.pack_into(
        "<9I",
        header,
        8,
        HEADER_BYTES,
        HEADER_BYTES + len(payload),
        HEADER_BYTES,
        len(payload),
        1,
        API_VERSION,
        manifest["launcher_id"],
        manifest["_required_mask"],
        manifest["_optional_mask"],
    )
    struct.pack_into("<HH", header, 44, int(manifest["accent_rgb565"], 16), 1)
    header[48:80] = hashlib.sha256(payload).digest()
    for offset, key, width in (
        (80, "id", 48),
        (128, "title", 16),
        (144, "subtitle", 32),
        (176, "folder", 32),
        (208, "version", 16),
        (224, "license", 16),
    ):
        header[offset : offset + width] = text_field(manifest, key, width)
    return bytes(header)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--strip", dest="strip_tool", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest = load_manifest(args.manifest.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="p4-package-") as temporary:
        elf_path = pathlib.Path(temporary) / "payload.elf"
        payload = build_elf(
            root, manifest, args.compiler.resolve(), args.strip_tool.resolve(), elf_path
        )
        package = build_header(manifest, payload) + payload
    temporary_output = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary_output.write_bytes(package)
    temporary_output.replace(args.output)
    print(
        json.dumps(
            {
                "result": "p4-game-package-built",
                "file": manifest["package_file"],
                "id": manifest["id"],
                "stack_frame_limit_bytes": manifest.get(
                    "stack_frame_limit_bytes"
                ),
                "bytes": len(package),
                "payload_bytes": len(payload),
                "payload_sha256": hashlib.sha256(payload).hexdigest(),
                "package_sha256": hashlib.sha256(package).hexdigest(),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (PackageError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"game package build failed: {error}") from error
