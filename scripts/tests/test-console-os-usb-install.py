#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import pathlib
import tempfile
import types
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/console-os-usb-install.py"
SPEC = importlib.util.spec_from_file_location("console_usb_install", PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FakeStub:
    def flash_set_parameters(self, size: int) -> None:
        assert size == MODULE.FLASH_BYTES


class FakeReset:
    def __init__(self, resets: list[str]) -> None:
        self.resets = resets

    def reset(self) -> None:
        self.resets.append("launch")


class FakeRuntime:
    def __init__(self, resets: list[str]) -> None:
        self.binding = {"runtime": "fake"}
        self.resets = resets

    def launch_reset_class(self, _device: Any, *, uses_usb: bool) -> FakeReset:
        assert uses_usb is False
        return FakeReset(self.resets)


def sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sealed(path: pathlib.Path, payload: bytes) -> None:
    path.write_bytes(payload)
    path.chmod(0o400)


def targets(directory: pathlib.Path) -> tuple[MODULE.TargetSegment, ...]:
    values = (
        ("game_data", MODULE.GAME_DATA_OFFSET, b"game"),
        ("application", MODULE.APP_OFFSET, b"app"),
        ("partition_table", MODULE.PARTITION_TABLE_OFFSET, b"table"),
    )
    result = []
    for name, offset, payload in values:
        path = directory / f"{name}.bin"
        sealed(path, payload)
        span = payload + b"\xff" * (MODULE.RESTORE_BLOCK_BYTES - len(payload))
        result.append(MODULE.TargetSegment(
            name, path, offset, payload, span, MODULE.RESTORE_BLOCK_BYTES
        ))
    return tuple(result)


def run_case(*, accept: bool, prefix_match: bool = True) -> None:
    with tempfile.TemporaryDirectory(prefix="console-usb-install-test-") as temp:
        root = pathlib.Path(temp)
        recovery = root / "recovery"
        recovery.mkdir(mode=0o700)
        selected = targets(root)
        bootloader = b"bootloader-test"
        memory = bytearray(b"\xa5" * MODULE.FLASH_BYTES)
        memory[
            MODULE.BOOTLOADER_OFFSET:
            MODULE.BOOTLOADER_OFFSET + len(bootloader)
        ] = bootloader
        predecessor = bytes(memory[
            MODULE.PARTITION_TABLE_OFFSET:
            MODULE.PARTITION_TABLE_OFFSET + MODULE.PARTITION_TABLE_PAYLOAD_BYTES
        ])
        original = bytes(memory)
        old_predecessor_hash = MODULE.EXPECTED_PREDECESSOR_PARTITION_SHA256
        old_prefix_hash = MODULE.EXPECTED_PREDECESSOR_PREFIX_SHA256
        MODULE.EXPECTED_PREDECESSOR_PARTITION_SHA256 = sha(predecessor)
        MODULE.EXPECTED_PREDECESSOR_PREFIX_SHA256 = (
            sha(bytes(memory[:MODULE.PARTITION_TABLE_OFFSET]))
            if prefix_match else "0" * 64
        )

        writes: list[str] = []
        resets: list[str] = []
        saved = {
            "load_stub": MODULE.TRANSPORT._load_stub,
            "read_exact": MODULE.TRANSPORT._read_exact,
            "write_span": MODULE.TRANSPORT._write_span,
            "validate_live": MODULE.TRANSPORT.RESTORE._validate_live_rom,
            "flash_id": MODULE.TRANSPORT.E5._fresh_exact_flash_id,
            "handle": MODULE.TRANSPORT.E5._handle_binding,
            "assert_handle": MODULE.TRANSPORT.E5._assert_same_handle,
            "controls": MODULE.TRANSPORT.E5._set_controls_false,
            "restore_baud": MODULE.TRANSPORT._restore_app_uart_baud,
            "capture": MODULE.CAPTURE.capture_open_handle,
        }

        def read_exact(
            _stub: Any, _device: Any, _handle: Any, offset: int,
            count: int, _chunk: int = MODULE.READBACK_CHUNK_BYTES,
        ) -> bytes:
            return bytes(memory[offset:offset + count])

        def write_span(
            _stub: Any, _device: Any, _handle: Any, offset: int,
            payload: bytes, _block: int,
        ) -> None:
            name = next(
                target.name for target in selected if target.offset == offset
            )
            phase = "install" if bytes(memory[offset:offset + len(payload)]) != payload \
                and payload == next(
                    target.span for target in selected if target.offset == offset
                ) else "restore"
            writes.append(f"{phase}:{name}")
            memory[offset:offset + len(payload)] = payload

        try:
            MODULE.TRANSPORT._load_stub = lambda *_args: FakeStub()
            MODULE.TRANSPORT._read_exact = read_exact
            MODULE.TRANSPORT._write_span = write_span
            MODULE.TRANSPORT.RESTORE._validate_live_rom = lambda *_args: None
            MODULE.TRANSPORT.E5._fresh_exact_flash_id = lambda *_args: 0x1840C8
            MODULE.TRANSPORT.E5._handle_binding = lambda _device: (1, 2, 3, 4, 5)
            MODULE.TRANSPORT.E5._assert_same_handle = lambda *_args: None
            MODULE.TRANSPORT.E5._set_controls_false = lambda *_args: None
            MODULE.TRANSPORT._restore_app_uart_baud = lambda *_args: None
            MODULE.CAPTURE.capture_open_handle = lambda *_args, **_kwargs: (
                b"startup", {"schema": 1, "result": "pass" if accept else "fail"}
            )
            runtime = FakeRuntime(resets)
            if not prefix_match:
                try:
                    MODULE.install_same_handle(
                        device=types.SimpleNamespace(), runtime=runtime,
                        targets=selected, recovery_directory=recovery,
                        factory_backup_binding={"sha256": "f" * 64},
                        authorization_sha256="a" * 64,
                        capture_seconds=30.0,
                    )
                except MODULE.InstallError as error:
                    assert "unexpected preserved boot prefix" in str(error)
                else:
                    raise AssertionError("unexpected predecessor prefix was accepted")
                assert writes == []
                assert resets == []
                preimage = recovery / MODULE.FULL_PREIMAGE_NAME
                assert preimage.stat().st_size == MODULE.FLASH_BYTES
                assert sha(preimage.read_bytes()) == sha(original)
                ledger = json.loads((recovery / MODULE.LEDGER_NAME).read_text())
                assert ledger["phase"] == "full-preimage-sealed"
                assert ledger["restore_required"] is False
                return
            if accept:
                result = MODULE.install_same_handle(
                    device=types.SimpleNamespace(), runtime=runtime,
                    targets=selected, recovery_directory=recovery,
                    factory_backup_binding={"sha256": "f" * 64},
                    authorization_sha256="a" * 64,
                    capture_seconds=30.0,
                )
                assert result["restore_required"] is False
                assert writes == [
                    "install:game_data", "install:application",
                    "install:partition_table",
                ]
                for target in selected:
                    assert bytes(memory[
                        target.offset:target.offset + len(target.span)
                    ]) == target.span
                assert resets == ["launch"]
            else:
                try:
                    MODULE.install_same_handle(
                        device=types.SimpleNamespace(), runtime=runtime,
                        targets=selected, recovery_directory=recovery,
                        factory_backup_binding={"sha256": "f" * 64},
                        authorization_sha256="a" * 64,
                        capture_seconds=30.0,
                    )
                except MODULE.InstallError as error:
                    assert "predecessor was restored" in str(error)
                else:
                    raise AssertionError("failed startup was not rejected")
                assert writes == [
                    "install:game_data", "install:application",
                    "install:partition_table", "restore:game_data",
                    "restore:application", "restore:partition_table",
                ]
                for target in selected:
                    assert bytes(memory[
                        target.offset:target.offset + len(target.span)
                    ]) == original[
                        target.offset:target.offset + len(target.span)
                    ]
                assert resets == ["launch", "launch"]

            preimage = recovery / MODULE.FULL_PREIMAGE_NAME
            assert preimage.stat().st_size == MODULE.FLASH_BYTES
            assert sha(preimage.read_bytes()) == sha(original)
            ledger = json.loads((recovery / MODULE.LEDGER_NAME).read_text())
            assert ledger["restore_required"] is False
            assert ledger["phase"] == (
                "runtime-accepted" if accept
                else "predecessor-restored-and-launched"
            )
        finally:
            MODULE.EXPECTED_PREDECESSOR_PARTITION_SHA256 = old_predecessor_hash
            MODULE.EXPECTED_PREDECESSOR_PREFIX_SHA256 = old_prefix_hash
            MODULE.TRANSPORT._load_stub = saved["load_stub"]
            MODULE.TRANSPORT._read_exact = saved["read_exact"]
            MODULE.TRANSPORT._write_span = saved["write_span"]
            MODULE.TRANSPORT.RESTORE._validate_live_rom = saved["validate_live"]
            MODULE.TRANSPORT.E5._fresh_exact_flash_id = saved["flash_id"]
            MODULE.TRANSPORT.E5._handle_binding = saved["handle"]
            MODULE.TRANSPORT.E5._assert_same_handle = saved["assert_handle"]
            MODULE.TRANSPORT.E5._set_controls_false = saved["controls"]
            MODULE.TRANSPORT._restore_app_uart_baud = saved["restore_baud"]
            MODULE.CAPTURE.capture_open_handle = saved["capture"]


def main() -> None:
    run_case(accept=True)
    run_case(accept=False)
    run_case(accept=False, prefix_match=False)
    print("Console OS USB installer transaction tests passed")


if __name__ == "__main__":
    main()
