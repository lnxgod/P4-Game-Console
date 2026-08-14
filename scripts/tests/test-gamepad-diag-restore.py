#!/usr/bin/env python3

"""Hermetic adversarial tests for the gamepad diagnostic restore helper."""

from __future__ import annotations

import hashlib
import importlib.util
import os
import pathlib
import stat
import struct
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/gamepad-diag-restore.py"
SPEC = importlib.util.spec_from_file_location("gamepad_diag_restore_test", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
RESTORE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESTORE)


def expect_error(fn, contains: str) -> None:
    try:
        fn()
    except RESTORE.RestoreError as error:
        assert contains in str(error), (contains, str(error))
    else:
        raise AssertionError(f"expected RestoreError containing {contains!r}")


def entry(name: str, ptype: int, subtype: int, offset: int, size: int, flags: int = 0) -> bytes:
    encoded = name.encode("ascii")
    assert 0 < len(encoded) <= 15
    return struct.pack(
        "<2sBBII16sI",
        RESTORE.ENTRY_MAGIC,
        ptype,
        subtype,
        offset,
        size,
        encoded + b"\x00" * (16 - len(encoded)),
        flags,
    )


def table(*records: bytes) -> bytes:
    body = b"".join(records)
    result = body + RESTORE.MD5_RECORD_PREFIX + hashlib.md5(body).digest()
    return result + b"\xff" * (RESTORE.PARTITION_TABLE_BYTES - len(result))


VALID_TABLE = table(
    entry("nvs", 1, 2, 0x9000, 0x6000),
    entry("phy_init", 1, 1, 0xF000, 0x1000),
    entry("factory", 0, 0, 0x10000, 0xB00000),
    entry("storage", 1, 0x82, 0xB10000, 0x400000),
)


def binding_with_extras(path: pathlib.Path, byte_count: int, label: str, **extras):
    value = RESTORE.file_binding(path, byte_count, label)
    value.update(extras)
    return value


class FakeDevice:
    def __init__(self) -> None:
        self.is_open = True
        self.dtr = False
        self.rts = False
        self.events: list[object] = []


class FakeReset:
    def __init__(self, device: FakeDevice) -> None:
        self.device = device

    def __call__(self) -> None:
        raise AssertionError("retrying ResetStrategy.__call__ must never be used")

    def reset(self) -> None:
        self.device.events.append("reset")
        self.device.dtr = False
        self.device.rts = False


class FakeStub:
    IS_STUB = True
    FLASH_WRITE_SIZE = 0x4000

    def __init__(self, device: FakeDevice, flash: bytearray) -> None:
        self._port = device
        self.device = device
        self.flash = flash
        self._offset = 0
        self._length = 0
        self.cache = {"flash_id": None}
        self._physical_flash_id_reads = 0

    def flash_id(self) -> int:
        if self.cache["flash_id"] is None:
            self._physical_flash_id_reads += 1
            self.device.events.append("flash-id-physical")
            self.cache["flash_id"] = 0x1840EF
        else:
            self.device.events.append("flash-id-cached")
        return self.cache["flash_id"]

    def read_spiflash_sfdp(self, address: int, bits: int) -> int:
        assert (address, bits) == (0x10, 8)
        self.device.events.append(("sfdp", address, bits))
        return 0xEF

    def run_spiflash_command(self, command: int, *args, **kwargs):
        assert not args and not kwargs
        self.device.events.append(("spi-command", command))

    def flash_set_parameters(self, size: int) -> None:
        assert size == RESTORE.FLASH_BYTES
        self.device.events.append(("flash-set-parameters", size))

    def flash_begin(self, size: int, offset: int, encrypted_write: bool = False) -> int:
        assert encrypted_write is False
        self.device.events.append(("flash_begin", offset, size))
        self._offset = offset
        self._length = size
        return (size + self.FLASH_WRITE_SIZE - 1) // self.FLASH_WRITE_SIZE

    def flash_block(self, block: bytes, sequence: int, encrypted: bool = False) -> None:
        assert encrypted is False
        self.device.events.append(("flash_block", sequence, len(block)))
        start = self._offset + sequence * self.FLASH_WRITE_SIZE
        end = min(start + len(block), self._offset + self._length)
        self.flash[start:end] = block[: end - start]

    def flash_finish(self, reboot: bool = True) -> None:
        assert reboot is False
        self.device.events.append(("flash_finish", reboot))

    def read_flash(self, offset: int, length: int, progress_fn=None) -> bytes:
        assert progress_fn is None
        self.device.events.append(("read_flash", offset, length))
        return bytes(self.flash[offset : offset + length])


class FakeROM:
    CHIP_NAME = "ESP32-P4"
    IMAGE_CHIP_ID = 18
    flash = bytearray(2 * 1024 * 1024)
    mac = (0x02, 0x00, 0x00, 0x00, 0x00, 0x01)

    def __init__(self, device: FakeDevice, baud: int, trace: bool) -> None:
        assert baud == 115200 and trace is False
        self._port = device
        self.device = device
        self.device.events.append("rom-created")

    def connect(self, mode: str, attempts: int, warnings: bool) -> None:
        assert (mode, attempts, warnings) == ("no_reset", 1, False)
        self.device.events.append(("connect", mode, attempts))

    def uses_usb_otg(self) -> bool:
        return False

    def get_chip_revision(self) -> int:
        return 103

    def read_mac(self):
        return self.mac

    def get_security_info(self, cache: bool):
        assert cache is False
        return {
            "parsed_flags": {"SECURE_BOOT_EN": False},
            "flash_crypt_cnt": 0,
        }

    def get_secure_boot_enabled(self) -> bool:
        return False

    def get_flash_encryption_enabled(self) -> bool:
        return False

    def run_stub(self, stub_image):
        assert stub_image == "fake-stub-image"
        self.device.events.append("run-stub")
        return FakeStub(self.device, self.flash)


def runtime_with_stub(
    stub_class,
    binding,
    reset_class=FakeReset,
):
    class SelectedROM(FakeROM):
        def run_stub(self, stub_image):
            assert stub_image == "fake-stub-image"
            self.device.events.append("run-stub")
            return stub_class(self.device, self.flash)

    return RESTORE.RestoreRuntime(
        SelectedROM,
        reset_class,
        lambda rom, path: "fake-stub-image",
        binding,
    )


def make_private_file(directory: pathlib.Path, name: str, payload: bytes) -> pathlib.Path:
    path = directory / name
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    return path


def test_sector_span_and_partition_parser() -> None:
    assert RESTORE.sector_span(1) == 0x1000
    assert RESTORE.sector_span(0x1000) == 0x1000
    assert RESTORE.sector_span(0x1001) == 0x2000
    expect_error(lambda: RESTORE.sector_span(True), "positive integers")
    parsed = RESTORE.parse_partition_table(VALID_TABLE, mutation_bytes=0x4A000)
    assert parsed["validated_md5"] is True
    assert parsed["entry_count"] == 4
    assert parsed["md5_record_offset"] == 128
    assert parsed["factory"] == {
        "name": "factory",
        "type": 0,
        "subtype": 0,
        "offset": 0x10000,
        "size": 0xB00000,
        "flags": 0,
    }

    bad_md5 = bytearray(VALID_TABLE)
    bad_md5[0x90] ^= 1
    expect_error(lambda: RESTORE.parse_partition_table(bytes(bad_md5)), "MD5")
    bad_reserved = bytearray(VALID_TABLE)
    bad_reserved[0x82] = 0
    expect_error(lambda: RESTORE.parse_partition_table(bytes(bad_reserved)), "noncanonical")
    bad_tail = bytearray(VALID_TABLE)
    bad_tail[0xA0] = 0
    expect_error(lambda: RESTORE.parse_partition_table(bytes(bad_tail)), "data after")
    no_md5 = VALID_TABLE[:128] + b"\xff" * (RESTORE.PARTITION_TABLE_BYTES - 128)
    expect_error(lambda: RESTORE.parse_partition_table(no_md5), "before its MD5")
    duplicate_factory = table(
        entry("factory", 0, 0, 0x10000, 0x100000),
        entry("factory2", 0, 0, 0x200000, 0x100000),
    )
    expect_error(lambda: RESTORE.parse_partition_table(duplicate_factory), "exactly one")
    containing_wrong_offset = table(entry("factory", 0, 0, 0, 0xB10000))
    expect_error(
        lambda: RESTORE.parse_partition_table(
            containing_wrong_offset, mutation_offset=0x10000, mutation_bytes=0x3000
        ),
        "exact 0x10000",
    )
    beyond_flash = table(entry("factory", 0, 0, 0x10000, 0x1000000))
    expect_error(lambda: RESTORE.parse_partition_table(beyond_flash), "16 MiB")
    too_small = table(entry("factory", 0, 0, 0x10000, 0x2000))
    expect_error(
        lambda: RESTORE.parse_partition_table(too_small, mutation_bytes=0x3000),
        "does not contain",
    )
    overlap = table(
        entry("factory", 0, 0, 0x10000, 0x4000),
        entry("overlap", 1, 2, 0x12000, 0x4000),
    )
    expect_error(lambda: RESTORE.parse_partition_table(overlap), "overlap")


def test_seal_and_binding_are_private_exclusive_and_durable() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        os.chmod(root, 0o700)
        payload = b"P" * 0x2000
        binding = RESTORE.seal_snapshot(
            root, "preimage.bin", payload, expected_bytes=len(payload)
        )
        path = pathlib.Path(binding["path"])
        assert stat.S_IMODE(path.stat().st_mode) == 0o600
        assert RESTORE.validate_binding(binding, "preimage") == path
        expect_error(
            lambda: RESTORE.seal_snapshot(
                root, "preimage.bin", payload, expected_bytes=len(payload)
            ),
            "refusing to overwrite",
        )
        path.chmod(0o640)
        expect_error(lambda: RESTORE.validate_binding(binding, "preimage"), "0600")
        path.chmod(0o600)
        path.write_bytes(b"Q" * len(payload))
        path.chmod(0o600)
        expect_error(lambda: RESTORE.validate_binding(binding, "preimage"), "sealed binding")
        root.chmod(0o750)
        expect_error(
            lambda: RESTORE.seal_snapshot(
                root, "new.bin", payload, expected_bytes=len(payload)
            ),
            "0700",
        )


def test_restore_same_handle_exact_order_and_no_arm_or_final_reset() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        os.chmod(root, 0o700)
        span = 0xC000
        preimage = bytes((index * 17) & 0xFF for index in range(span))
        preimage_path = make_private_file(root, "preimage.bin", preimage)
        table_path = make_private_file(root, "partition.bin", VALID_TABLE)
        stub_path = make_private_file(root, "esp32p4-rev1.json", b"stub")
        runtime_binding = {
            "legacy_rev1_stub": {
                "path": str(stub_path),
                "sha256": hashlib.sha256(b"stub").hexdigest(),
            },
            "test_runtime": True,
        }
        # The production constant remains strict; hermetic runtime uses the same
        # code path by temporarily binding its own exact fake stub digest.
        original_stub_sha = RESTORE.LEGACY_REV1_STUB_SHA256
        RESTORE.LEGACY_REV1_STUB_SHA256 = hashlib.sha256(b"stub").hexdigest()
        try:
            runtime = RESTORE.RestoreRuntime(
                FakeROM,
                FakeReset,
                lambda rom, path: "fake-stub-image",
                runtime_binding,
            )
            parsed = RESTORE.parse_partition_table(
                VALID_TABLE, mutation_offset=0x10000, mutation_bytes=span
            )
            table_binding = binding_with_extras(
                table_path,
                len(VALID_TABLE),
                "live table",
                **parsed,
                restore_runtime=runtime_binding,
                restore_tool={
                    "path": str(pathlib.Path(RESTORE.__file__).resolve()),
                    "sha256": RESTORE._sha256_file(
                        pathlib.Path(RESTORE.__file__).resolve()
                    ),
                },
            )
            changed_tool = dict(table_binding)
            changed_tool["restore_tool"] = dict(table_binding["restore_tool"])
            changed_tool["restore_tool"]["sha256"] = "0" * 64
            expect_error(
                lambda: RESTORE.validate_partition_binding(
                    changed_tool,
                    expected_offset=0x10000,
                    expected_span=span,
                    runtime=runtime,
                ),
                "restore helper",
            )
            changed_path = dict(table_binding)
            changed_path["restore_tool"] = dict(table_binding["restore_tool"])
            changed_path["restore_tool"]["path"] = str(root / "substitute.py")
            expect_error(
                lambda: RESTORE.validate_partition_binding(
                    changed_path,
                    expected_offset=0x10000,
                    expected_span=span,
                    runtime=runtime,
                ),
                "restore helper",
            )
            preimage_binding = binding_with_extras(
                preimage_path,
                span,
                "preimage",
                offset=0x10000,
                sector_bytes=0x1000,
                install_write_block_bytes=0x4000,
                mutation_span_bytes=span,
            )
            device = FakeDevice()
            FakeROM.flash[:] = b"\xff" * len(FakeROM.flash)
            expected_identity = hashlib.sha256(b"020000000001").hexdigest()

            def durable_marker() -> None:
                device.events.append("durable-write-attempt")

            result = RESTORE.restore_same_handle(
                device,
                preimage_binding,
                table_binding,
                expected_identity,
                runtime=runtime,
                mark_write_attempt=durable_marker,
                readback_chunk_bytes=0x4000,
            )
            assert result["offset"] == 0x10000
            assert result["bytes"] == span
            assert result["readback_sha256"] == hashlib.sha256(preimage).hexdigest()
            assert result["readback_chunks"] == 3
            assert result["download_reset_count"] == 1
            assert result["connect_no_reset_attempts"] == 1
            assert result["post_restore_reset_count"] == 0
            assert result["arm_transmitted_bytes"] == 0
            assert device.is_open and not device.dtr and not device.rts
            assert bytes(FakeROM.flash[0x10000 : 0x10000 + span]) == preimage
            assert FakeROM.flash[0x10000 + span] == 0xFF
            writes = [
                event for event in device.events
                if isinstance(event, tuple) and event[0] == "flash_block"
            ]
            assert len(writes) == span // RESTORE.SECTOR_BYTES
            assert all(event[2] == RESTORE.SECTOR_BYTES for event in writes)
            assert device.events.count("reset") == 1
            assert device.events.count("durable-write-attempt") == 1
            assert device.events.index("durable-write-attempt") < device.events.index(
                ("flash_begin", 0x10000, span)
            )
            assert device.events[:11] == [
                "reset",
                "rom-created",
                ("connect", "no_reset", 1),
                "run-stub",
                "flash-id-physical",
                "flash-id-cached",
                ("sfdp", 0x10, 8),
                ("spi-command", 0x66),
                ("spi-command", 0x99),
                "flash-id-physical",
                ("flash-set-parameters", RESTORE.FLASH_BYTES),
            ]
            assert device.events[11] == "durable-write-attempt"
            assert device.events.count("flash-id-physical") == 2
            assert not any(
                isinstance(event, tuple) and event[0] == "read_flash" and event[2] > 0x4000
                for event in device.events
            )

            legacy_span = 0x4A000
            legacy_path = make_private_file(
                root, "legacy-sector-span.bin", b"L" * legacy_span
            )
            legacy_binding = binding_with_extras(
                legacy_path,
                legacy_span,
                "legacy preimage",
                offset=0x10000,
                sector_bytes=0x1000,
                install_write_block_bytes=0x4000,
                mutation_span_bytes=legacy_span,
            )
            legacy_device = FakeDevice()
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    legacy_device,
                    legacy_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime,
                ),
        "install-block-rounded mutation span",
            )
            assert legacy_device.events == []

            class BadFlashIdStub(FakeStub):
                def flash_id(self):
                    self.device.events.append("flash-id-invalid")
                    self.cache["flash_id"] = 0xFFFFFF
                    return self.cache["flash_id"]

            bad_flash_device = FakeDevice()
            bad_flash_markers: list[str] = []
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    bad_flash_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(BadFlashIdStub, runtime_binding),
                    mark_write_attempt=lambda: bad_flash_markers.append("attempt"),
                ),
                "flash chip connection",
            )
            assert bad_flash_markers == []
            assert not any(
                isinstance(event, tuple) and event[0] == "flash_begin"
                for event in bad_flash_device.events
            )

            class ResetCommandFailureStub(FakeStub):
                def run_spiflash_command(self, command, *args, **kwargs):
                    super().run_spiflash_command(command, *args, **kwargs)
                    if command == 0x99:
                        raise OSError("injected flash reset failure")

            flash_reset_device = FakeDevice()
            flash_reset_markers: list[str] = []
            try:
                RESTORE.restore_same_handle(
                    flash_reset_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(ResetCommandFailureStub, runtime_binding),
                    mark_write_attempt=lambda: flash_reset_markers.append("attempt"),
                )
            except OSError as error:
                assert str(error) == "injected flash reset failure"
            else:
                raise AssertionError("flash reset failure returned false success")
            assert flash_reset_markers == []

            wrong_identity = "0" * 64
            bad_device = FakeDevice()
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    bad_device,
                    preimage_binding,
                    table_binding,
                    wrong_identity,
                    runtime=runtime,
                ),
                "sealed device identity",
            )
            assert not any(
                isinstance(event, tuple) and event[0] == "flash_begin"
                for event in bad_device.events
            )

            mismatched_runtime = RESTORE.RestoreRuntime(
                FakeROM,
                FakeReset,
                lambda rom, path: "fake-stub-image",
                {**runtime_binding, "test_runtime": "changed"},
            )
            mismatch_device = FakeDevice()
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    mismatch_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=mismatched_runtime,
                ),
                "runtime/stub binding changed",
            )
            assert mismatch_device.events == []

            stub_path.write_bytes(b"changed")
            stub_path.chmod(0o600)
            changed_stub_device = FakeDevice()
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    changed_stub_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime,
                ),
                "pinned legacy rev1 stub changed",
            )
            assert changed_stub_device.events == [
                "reset",
                "rom-created",
                ("connect", "no_reset", 1),
            ]
            assert not changed_stub_device.dtr and not changed_stub_device.rts
            stub_path.write_bytes(b"stub")
            stub_path.chmod(0o600)

            class FailingReset(FakeReset):
                def reset(self) -> None:
                    self.device.events.append("reset")
                    self.device.dtr = True
                    self.device.rts = True
                    raise RuntimeError("injected reset failure")

            reset_device = FakeDevice()
            try:
                RESTORE.restore_same_handle(
                    reset_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(
                        FakeStub, runtime_binding, reset_class=FailingReset
                    ),
                )
            except RuntimeError as error:
                assert str(error) == "injected reset failure"
            else:
                raise AssertionError("reset failure returned false success")
            assert reset_device.events == ["reset"]
            assert reset_device.is_open and not reset_device.dtr and not reset_device.rts

            class WrongBlockCountStub(FakeStub):
                def flash_begin(self, size, offset, encrypted_write=False):
                    return super().flash_begin(size, offset, encrypted_write) + 1

            wrong_blocks_device = FakeDevice()
            wrong_blocks_markers: list[str] = []

            def wrong_blocks_marker() -> None:
                wrong_blocks_markers.append("attempt")
                wrong_blocks_device.events.append("durable-write-attempt")

            expect_error(
                lambda: RESTORE.restore_same_handle(
                    wrong_blocks_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(WrongBlockCountStub, runtime_binding),
                    mark_write_attempt=wrong_blocks_marker,
                ),
                "unexpected flash block count",
            )
            assert wrong_blocks_markers == ["attempt"]
            assert wrong_blocks_device.events.index("durable-write-attempt") < (
                wrong_blocks_device.events.index(("flash_begin", 0x10000, span))
            )
            assert not wrong_blocks_device.dtr and not wrong_blocks_device.rts

            class PartialWriteStub(FakeStub):
                def flash_block(self, block, sequence, encrypted=False):
                    if sequence == 1:
                        self.device.events.append(("flash_block_failed", sequence))
                        raise OSError("injected partial write")
                    super().flash_block(block, sequence, encrypted)

            partial_device = FakeDevice()
            partial_markers: list[str] = []
            try:
                RESTORE.restore_same_handle(
                    partial_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(PartialWriteStub, runtime_binding),
                    mark_write_attempt=lambda: partial_markers.append("attempt"),
                )
            except OSError as error:
                assert str(error) == "injected partial write"
            else:
                raise AssertionError("partial write returned false success")
            assert partial_markers == ["attempt"]
            assert ("flash_block", 0, RESTORE.SECTOR_BYTES) in partial_device.events
            assert ("flash_block_failed", 1) in partial_device.events
            assert not any(
                isinstance(event, tuple) and event[0] in {"flash_finish", "read_flash"}
                for event in partial_device.events
            )
            assert not partial_device.dtr and not partial_device.rts

            class ShortReadStub(FakeStub):
                def read_flash(self, offset, length, progress_fn=None):
                    return super().read_flash(offset, length, progress_fn)[:-1]

            short_device = FakeDevice()
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    short_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(ShortReadStub, runtime_binding),
                    mark_write_attempt=lambda: short_device.events.append(
                        "durable-write-attempt"
                    ),
                ),
                "wrong byte count",
            )
            assert short_device.events.count("durable-write-attempt") == 1
            assert not short_device.dtr and not short_device.rts

            class CorruptReadStub(FakeStub):
                def read_flash(self, offset, length, progress_fn=None):
                    payload = bytearray(super().read_flash(offset, length, progress_fn))
                    payload[0] ^= 1
                    return bytes(payload)

            corrupt_device = FakeDevice()
            expect_error(
                lambda: RESTORE.restore_same_handle(
                    corrupt_device,
                    preimage_binding,
                    table_binding,
                    expected_identity,
                    runtime=runtime_with_stub(CorruptReadStub, runtime_binding),
                    mark_write_attempt=lambda: corrupt_device.events.append(
                        "durable-write-attempt"
                    ),
                    readback_chunk_bytes=0x4000,
                ),
                "differs from the sealed preimage",
            )
            assert corrupt_device.events.count("durable-write-attempt") == 1
            reads = [
                event
                for event in corrupt_device.events
                if isinstance(event, tuple) and event[0] == "read_flash"
            ]
            assert reads == [
                ("read_flash", 0x10000, 0x4000),
                ("read_flash", 0x14000, 0x4000),
                ("read_flash", 0x18000, 0x4000),
            ]
            assert not corrupt_device.dtr and not corrupt_device.rts
        finally:
            RESTORE.LEGACY_REV1_STUB_SHA256 = original_stub_sha


def main() -> None:
    test_sector_span_and_partition_parser()
    test_seal_and_binding_are_private_exclusive_and_durable()
    test_restore_same_handle_exact_order_and_no_arm_or_final_reset()
    print("gamepad diagnostic restore tests: PASS")


if __name__ == "__main__":
    main()
