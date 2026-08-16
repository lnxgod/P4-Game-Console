#!/usr/bin/env python3
"""Create the ESP32-P4 Quake common.c with its PAK directory off-stack."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


EXPECTED_COMMON_SHA256 = (
    "f25ad7f01fbccd2c8948eaf70cedb9e541c1e96e3f695a6f2e90ce813b50852f"
)


def replace_once(source: str, old: str, new: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"expected one upstream match, found {count}: {old!r}"
        )
    return source.replace(old, new, 1)


def prepare(source_path: Path) -> bytes:
    source_bytes = source_path.read_bytes()
    actual_hash = hashlib.sha256(source_bytes).hexdigest()
    if actual_hash != EXPECTED_COMMON_SHA256:
        raise RuntimeError(
            "refusing to patch an unpinned quakegeneric common.c: "
            f"expected {EXPECTED_COMMON_SHA256}, got {actual_hash}"
        )
    source = source_bytes.decode("utf-8")
    source = replace_once(
        source,
        "\tdpackfile_t             info[MAX_FILES_IN_PACK];",
        "\tdpackfile_t             *info;",
    )
    source = replace_once(
        source,
        "\tnumpackfiles = header.dirlen / sizeof(dpackfile_t);\n\n"
        "\tif (numpackfiles > MAX_FILES_IN_PACK)\n",
        "\tif (header.dirlen <= 0 || "
        "header.dirlen % sizeof(dpackfile_t) != 0)\n"
        "\t\tSys_Error (\"%s has an invalid directory length\", packfile);\n\n"
        "\tnumpackfiles = header.dirlen / sizeof(dpackfile_t);\n\n"
        "\tif (numpackfiles > MAX_FILES_IN_PACK)\n",
    )
    source = replace_once(
        source,
        "\tnewfiles = Hunk_AllocName "
        "(numpackfiles * sizeof(packfile_t), \"packfile\");\n\n"
        "\tSys_FileSeek (packhandle, header.dirofs);\n"
        "\tSys_FileRead (packhandle, (void *)info, header.dirlen);\n",
        "\tnewfiles = Hunk_AllocName "
        "(numpackfiles * sizeof(packfile_t), \"packfile\");\n"
        "\t// ESP32-P4: the upstream 128 KiB maximum local array overflows\n"
        "\t// app_main's 24 KiB stack. Keep this bounded workspace in the\n"
        "\t// existing 20 MiB Quake hunk, which resides in PSRAM.\n"
        "\tinfo = Hunk_AllocName (header.dirlen, \"packdir\");\n\n"
        "\tSys_FileSeek (packhandle, header.dirofs);\n"
        "\tif (Sys_FileRead (packhandle, (void *)info, header.dirlen) "
        "!= header.dirlen)\n"
        "\t\tSys_Error (\"%s has a truncated directory\", packfile);\n",
    )
    return source.encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = prepare(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != output:
        args.output.write_bytes(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
