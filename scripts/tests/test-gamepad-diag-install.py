#!/usr/bin/env python3

"""Hermetic adversarial tests for the exclusive gamepad install transaction."""

from __future__ import annotations

import hashlib
import importlib.util
import os
import pathlib
import stat
import struct
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/gamepad-diag-install.py"
SPEC = importlib.util.spec_from_file_location("gamepad_diag_install_test", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
INSTALL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALL)


def entry(name: str, ptype: int, subtype: int, offset: int, size: int) -> bytes:
    encoded = name.encode("ascii")
    return struct.pack(
        "<2sBBII16sI",
        INSTALL.RESTORE.ENTRY_MAGIC,
        ptype,
        subtype,
        offset,
        size,
        encoded + b"\x00" * (16 - len(encoded)),
        0,
    )


def table(*records: bytes) -> bytes:
    body = b"".join(records)
    result = (
        body
        + INSTALL.RESTORE.MD5_RECORD_PREFIX
        + hashlib.md5(body).digest()
    )
    return result + b"\xff" * (INSTALL.RESTORE.PARTITION_TABLE_BYTES - len(result))


VALID_TABLE = table(
    entry("nvs", 1, 2, 0x9000, 0x6000),
    entry("phy_init", 1, 1, 0xF000, 0x1000),
    entry("factory", 0, 0, 0x10000, 0xB00000),
)
ARTIFACT = bytes((index * 29 + 7) & 0xFF for index in range(301328))
PREIMAGE = bytes((index * 17 + 3) & 0xFF for index in range(311296))
MAC_A = (0x02, 0x00, 0x00, 0x00, 0x00, 0x01)
MAC_B = (0x02, 0x00, 0x00, 0x00, 0x00, 0x02)


class FakeDevice:
    def __init__(self, directory: pathlib.Path, name: str = "uart-a") -> None:
        self.path = directory / name
        self.path.write_bytes(b"")
        self.fd = os.open(self.path, os.O_RDWR)
        self.is_open = True
        self.exclusive = True
        self.dtr = False
        self.rts = False
        self.events: list[object] = []

    def fileno(self) -> int:
        return self.fd

    def replace_descriptor(self, path: pathlib.Path) -> None:
        replacement = path / "uart-replacement"
        replacement.write_bytes(b"")
        old = self.fd
        self.fd = os.open(replacement, os.O_RDWR)
        os.close(old)
        self.events.append("descriptor-replaced")

    def close(self) -> None:
        if self.is_open:
            os.close(self.fd)
            self.is_open = False


class FakeReset:
    def __init__(self, device: FakeDevice) -> None:
        self.device = device

    def __call__(self) -> None:
        raise AssertionError("retrying reset call is forbidden")

    def reset(self) -> None:
        self.device.events.append("reset")


class FakeStub:
    CHIP_NAME = "ESP32-P4"
    IMAGE_CHIP_ID = 18
    IS_STUB = True
    FLASH_WRITE_SIZE = 0x4000

    def __init__(self, rom: "FakeROM") -> None:
        self._port = rom._port
        self.device = rom.device
        self.flash = rom.flash
        self.mac = rom.mac
        self.cache = {"flash_id": None}
        self.write_offset = 0
        self.write_size = 0
        self.read_count = 0
        self.swap_mode = rom.swap_mode

    def uses_usb_otg(self) -> bool:
        return False

    def get_chip_revision(self) -> int:
        self.device.events.append("revision")
        return 103

    def read_mac(self):
        self.device.events.append(("identity", self.mac))
        return self.mac

    def get_security_info(self, cache: bool = False):
        assert cache is False
        self.device.events.append("security")
        return {"parsed_flags": {"SECURE_BOOT_EN": False}, "flash_crypt_cnt": 0}

    def get_secure_boot_enabled(self) -> bool:
        return False

    def get_flash_encryption_enabled(self) -> bool:
        return False

    def flash_id(self) -> int:
        if self.cache["flash_id"] is None:
            self.cache["flash_id"] = 0x1840EF
            self.device.events.append("flash-id-physical")
        else:
            self.device.events.append("flash-id-cached")
        return self.cache["flash_id"]

    def read_spiflash_sfdp(self, address: int, bits: int) -> int:
        assert (address, bits) == (0x10, 8)
        return 0xEF

    def run_spiflash_command(self, command: int) -> None:
        self.device.events.append(("spi", command))

    def flash_set_parameters(self, size: int) -> None:
        assert size == 16 * 1024 * 1024
        self.device.events.append(("flash-size", size))

    def read_flash(self, offset: int, length: int, progress_fn=None) -> bytes:
        assert progress_fn is None
        self.read_count += 1
        self.device.events.append(("read", self.read_count, offset, length))
        payload = bytes(self.flash[offset : offset + length])
        if self.swap_mode == "short-initial" and self.read_count == 1:
            return payload[:-1]
        if self.swap_mode == "transport-port" and self.read_count == 3:
            self._port = object()
        if self.swap_mode == "descriptor" and self.read_count == 3:
            self.device.replace_descriptor(self.device.path.parent)
        if self.swap_mode == "identity" and self.read_count == 2:
            self.mac = MAC_B
        if self.swap_mode == "preimage" and self.read_count == 2:
            self.flash[0x10000] ^= 1
        if (
            self.swap_mode == "corrupt-tail"
            and ("flash-finish", False) in self.device.events
            and offset + length == 0x5C000
        ):
            payload = payload[:-1] + bytes([payload[-1] ^ 1])
        return payload

    def flash_begin(self, size: int, offset: int, encrypted_write: bool = False) -> int:
        assert encrypted_write is False
        self.device.events.append(("flash-begin", offset, size))
        self.write_offset = offset
        self.write_size = size
        return size // self.FLASH_WRITE_SIZE

    def flash_block(self, block: bytes, sequence: int, encrypted: bool = False) -> None:
        assert encrypted is False and len(block) == self.FLASH_WRITE_SIZE
        if self.swap_mode == "partial-write" and sequence == 2:
            self.device.events.append(("flash-block-failed", sequence))
            raise OSError("injected partial install write")
        self.device.events.append(("flash-block", sequence))
        start = self.write_offset + sequence * self.FLASH_WRITE_SIZE
        self.flash[start : start + len(block)] = block

    def flash_finish(self, reboot: bool = True) -> None:
        assert reboot is False
        self.device.events.append(("flash-finish", reboot))


class FakeROM:
    CHIP_NAME = "ESP32-P4"
    IMAGE_CHIP_ID = 18

    def __init__(self, device: FakeDevice, baud: int, trace: bool) -> None:
        assert baud == 115200 and trace is False
        self._port = device
        self.device = device
        self.flash = bytearray(2 * 1024 * 1024)
        self.flash[0x8000 : 0x8000 + len(VALID_TABLE)] = VALID_TABLE
        self.flash[0x10000 : 0x10000 + len(PREIMAGE)] = PREIMAGE
        self.mac = MAC_A
        self.swap_mode = getattr(device, "swap_mode", "none")
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

    def get_security_info(self, cache: bool = False):
        assert cache is False
        return {"parsed_flags": {"SECURE_BOOT_EN": False}, "flash_crypt_cnt": 0}

    def get_secure_boot_enabled(self) -> bool:
        return False

    def get_flash_encryption_enabled(self) -> bool:
        return False

    def run_stub(self, image):
        assert image == "fake-stub"
        self.device.events.append("run-stub")
        return FakeStub(self)


class FakeState:
    EXPECTED_OFFSET = "0x10000"
    EXPECTED_BYTES = len(ARTIFACT)
    EXPECTED_SHA256 = hashlib.sha256(ARTIFACT).hexdigest()
    EXPECTED_DEVICE_SHA256 = hashlib.sha256(b"020000000001").hexdigest()

    def __init__(self, events: list[object]) -> None:
        self.events = events
        self.installed_verified_args = None

    @staticmethod
    def mutation_span(byte_count: int) -> int:
        return ((byte_count + 0x3FFF) // 0x4000) * 0x4000

    def read_owner_token(self, path: pathlib.Path) -> str:
        self.events.append("owner-read")
        return path.read_text(encoding="ascii").strip()

    def stage_arm_secret(self, source, destination, authorization, project_root) -> None:
        self.events.append("secret-stage")
        payload = source.read_bytes()
        descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        source.unlink()

    def bind_preimage(self, *args) -> None:
        self.events.append("preimage-bind")

    def mark_install_write_attempt(self, *args) -> None:
        self.events.append("durable-install-attempt")

    def mark_installed_verified(self, *args) -> None:
        self.installed_verified_args = args
        self.events.append("installed-verified")


class InstallTransactionTests(unittest.TestCase):
    def run_case(self, mode: str = "none"):
        temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(temporary.name)
        recovery = root / "recovery"
        recovery.mkdir(mode=0o700)
        artifact = root / "artifact.bin"
        artifact.write_bytes(ARTIFACT)
        artifact.chmod(0o400)
        arm = root / "arm-token-source"
        arm.write_bytes(b"a" * 64 + b"\n")
        arm.chmod(0o600)
        owner = recovery / "owner-token"
        owner.write_text("b" * 64 + "\n", encoding="ascii")
        owner.chmod(0o600)
        stub_path = root / "stub.json"
        stub_path.write_bytes(b"stub")
        stub_path.chmod(0o600)
        device = FakeDevice(root)
        device.swap_mode = mode
        state = FakeState(device.events)
        runtime = INSTALL.RESTORE.RestoreRuntime(
            FakeROM,
            FakeReset,
            lambda rom, path: "fake-stub",
            {
                "legacy_rev1_stub": {
                    "path": str(stub_path),
                    "sha256": hashlib.sha256(b"stub").hexdigest(),
                },
                "test": True,
            },
        )
        original = INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256
        INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256 = hashlib.sha256(b"stub").hexdigest()
        kwargs = dict(
            device=device,
            artifact_path=artifact,
            arm_token_source=arm,
            recovery_dir=recovery,
            state_path=root / "state.json",
            authorization_path=root / "auth.json",
            project_root=root,
            owner_token_file=owner,
            runtime=runtime,
            state_api=state,
            readback_chunk_bytes=0x20000,
        )
        return temporary, root, recovery, device, kwargs, original

    def finish_case(self, temporary, device, original) -> None:
        INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256 = original
        device.close()
        temporary.cleanup()

    def test_success_keeps_one_handle_and_exact_callback_order(self) -> None:
        case = self.run_case()
        temporary, root, recovery, device, kwargs, original = case
        try:
            result = INSTALL.install_same_handle(**kwargs)
            self.assertTrue(result["same_uart_handle"])
            self.assertEqual(result["mutation_span_bytes"], 311296)
            self.assertEqual(result["readback_sha256"], hashlib.sha256(ARTIFACT).hexdigest())
            self.assertEqual(device.events.count("reset"), 1)
            self.assertEqual(device.events.count(("connect", "no_reset", 1)), 1)
            mark = device.events.index("durable-install-attempt")
            write = next(
                index for index, event in enumerate(device.events)
                if isinstance(event, tuple) and event[0] == "flash-begin"
            )
            installed = device.events.index("installed-verified")
            self.assertLess(device.events.index("preimage-bind"), mark)
            self.assertLess(mark, write)
            self.assertLess(write, installed)
            self.assertEqual(
                result["install_span_sha256"],
                result["install_span_readback_sha256"],
            )
            self.assertEqual(
                kwargs["state_api"].installed_verified_args[-1],
                result["install_span_readback_sha256"],
            )
            self.assertEqual(
                (recovery / "live-partition-table.bin").read_bytes(), VALID_TABLE
            )
            self.assertEqual(
                (recovery / "preinstall-mutation-span.bin").read_bytes(), PREIMAGE
            )
            self.assertFalse((root / "arm-token-source").exists())
            self.assertTrue((recovery / "arm-token").is_file())
        finally:
            self.finish_case(temporary, device, original)

    def assert_swap_refused_before_mark(self, mode: str, message: str) -> None:
        case = self.run_case(mode)
        temporary, _root, _recovery, device, kwargs, original = case
        try:
            with self.assertRaisesRegex(
                (INSTALL.InstallError, INSTALL.RESTORE.RestoreError), message
            ):
                INSTALL.install_same_handle(**kwargs)
            self.assertNotIn("durable-install-attempt", device.events)
            self.assertFalse(any(
                isinstance(event, tuple) and event[0] == "flash-begin"
                for event in device.events
            ))
        finally:
            self.finish_case(temporary, device, original)

    def test_transport_object_swap_is_refused(self) -> None:
        self.assert_swap_refused_before_mark("transport-port", "left the exclusive UART")

    def test_same_object_descriptor_swap_is_refused(self) -> None:
        self.assert_swap_refused_before_mark("descriptor", "handle changed")

    def test_second_device_identity_is_refused(self) -> None:
        self.assert_swap_refused_before_mark("identity", "sealed device identity")

    def test_cross_device_preimage_change_is_refused(self) -> None:
        self.assert_swap_refused_before_mark("preimage", "recovery bytes changed")

    def test_short_snapshot_read_is_refused(self) -> None:
        self.assert_swap_refused_before_mark("short-initial", "wrong byte count")

    def test_partial_write_stays_restore_required_and_never_receipts(self) -> None:
        case = self.run_case("partial-write")
        temporary, _root, _recovery, device, kwargs, original = case
        try:
            with self.assertRaisesRegex(OSError, "partial install write"):
                INSTALL.install_same_handle(**kwargs)
            self.assertEqual(device.events.count("durable-install-attempt"), 1)
            self.assertNotIn("installed-verified", device.events)
        finally:
            self.finish_case(temporary, device, original)

    def test_corrupt_ff_tail_fails_full_span_verification(self) -> None:
        case = self.run_case("corrupt-tail")
        temporary, _root, _recovery, device, kwargs, original = case
        try:
            with self.assertRaisesRegex(INSTALL.InstallError, "full-span readback"):
                INSTALL.install_same_handle(**kwargs)
            self.assertEqual(device.events.count("durable-install-attempt"), 1)
            self.assertNotIn("installed-verified", device.events)
        finally:
            self.finish_case(temporary, device, original)

    def test_receipt_and_result_are_after_verified_uart_close_in_main(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        install = source.index("result = install_same_handle(")
        close = source.index("device.close()", install)
        close_check = source.index('getattr(device, "is_open", False)', close)
        receipt = source.index("capture.emit_receipt(namespace)", close_check)
        publish = source.index("_write_result(args.result, result)", receipt)
        self.assertLess(install, close)
        self.assertLess(close, close_check)
        self.assertLess(close_check, receipt)
        self.assertLess(receipt, publish)


if __name__ == "__main__":
    unittest.main()
