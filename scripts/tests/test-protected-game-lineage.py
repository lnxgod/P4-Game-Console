#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import struct
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/generate-protected-game-lineage.py"
SPEC = importlib.util.spec_from_file_location("protected_lineage", GENERATOR)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def package(game_id: str = "org.p4console.lord", save: bool = True) -> bytes:
    payload = b"deterministic-riscv-elf-fixture"
    header = bytearray(256)
    header[:8] = b"P4GAME1\0"
    struct.pack_into("<IIII", header, 8, 256, 256 + len(payload), 256,
                     len(payload))
    struct.pack_into("<II", header, 24, 1, 1)
    struct.pack_into("<I", header, 32, 112)
    struct.pack_into("<II", header, 36, 3, (1 << 6) if save else 0)
    header[48:80] = hashlib.sha256(payload).digest()
    encoded_id = game_id.encode("ascii")
    header[80:80 + len(encoded_id)] = encoded_id
    header[128:132] = b"LORD"
    header[176:191] = b"GAMES/ADVENTURE"
    header[208:213] = b"1.6.1"
    header[224:239] = b"LORD-PERMISSION"
    return bytes(header) + payload


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        path = pathlib.Path(temporary) / "LORD.P4G"
        path.write_bytes(package())
        digest = MODULE.inspect_package(path, "org.p4console.lord")
        assert digest == hashlib.sha256(
            b"deterministic-riscv-elf-fixture").digest()
        header = MODULE.render_header("org.p4console.lord", digest)
        assert "P4_PROTECTED_GAME_LINEAGE_COUNT" in header
        assert "org.p4console.lord" in header
        assert "UINT8_C(0x" in header

        wrong_id = pathlib.Path(temporary) / "WRONG.P4G"
        wrong_id.write_bytes(package("org.p4console.counterfeit"))
        try:
            MODULE.inspect_package(wrong_id, "org.p4console.lord")
            raise AssertionError("wrong ID was accepted")
        except MODULE.LineageError:
            pass

        no_save = pathlib.Path(temporary) / "NOSAVE.P4G"
        no_save.write_bytes(package(save=False))
        try:
            MODULE.inspect_package(no_save, "org.p4console.lord")
            raise AssertionError("missing save capability was accepted")
        except MODULE.LineageError:
            pass

        tampered = bytearray(package())
        tampered[-1] ^= 1
        path.write_bytes(tampered)
        try:
            MODULE.inspect_package(path, "org.p4console.lord")
            raise AssertionError("tampered payload was accepted")
        except MODULE.LineageError:
            pass


if __name__ == "__main__":
    main()
