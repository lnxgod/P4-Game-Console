#!/usr/bin/env python3
"""Hermetic adversarial tests for the E5 exclusive install transaction."""

from __future__ import annotations

import hashlib
import importlib.util
import os
import pathlib
import struct
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/doom-e5-install.py"
SPEC = importlib.util.spec_from_file_location("doom_e5_install_test", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
INSTALL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALL)

ARTIFACT = bytes((index * 29 + 7) & 0xFF for index in range(32_000))
SPAN = 32_768
MAC_A = (0x02, 0, 0, 0, 0, 1)
MAC_B = (0x02, 0, 0, 0, 0, 2)


def entry(name: str, ptype: int, subtype: int, offset: int, size: int) -> bytes:
    label = name.encode("ascii")
    return struct.pack(
        "<2sBBII16sI", INSTALL.RESTORE.ENTRY_MAGIC, ptype, subtype, offset,
        size, label + b"\x00" * (16 - len(label)), 0,
    )


def table(factory_size: int = 0xB00000) -> bytes:
    body = b"".join((
        entry("nvs", 1, 2, 0x9000, 0x6000),
        entry("phy_init", 1, 1, 0xF000, 0x1000),
        entry("factory", 0, 0, 0x10000, factory_size),
    ))
    result = body + INSTALL.RESTORE.MD5_RECORD_PREFIX + hashlib.md5(body).digest()
    return result + b"\xff" * (INSTALL.RESTORE.PARTITION_TABLE_BYTES - len(result))


class Device:
    def __init__(self, root: pathlib.Path, mode: str) -> None:
        self.path = root / "uart"
        self.path.write_bytes(b"")
        self.fd = os.open(self.path, os.O_RDWR)
        self.is_open = True
        self.exclusive = True
        self.dtr = False
        self.rts = False
        self.mode = mode
        self.events: list[object] = []

    def fileno(self) -> int:
        return self.fd

    def close(self) -> None:
        if self.is_open:
            os.close(self.fd)
            self.is_open = False

    def swap_descriptor(self) -> None:
        replacement = self.path.parent / "uart-b"
        replacement.write_bytes(b"")
        old = self.fd
        self.fd = os.open(replacement, os.O_RDWR)
        os.close(old)
        self.events.append("descriptor-swapped")


class LoaderReset:
    def __init__(self, device: Device) -> None:
        self.device = device

    def reset(self) -> None:
        self.device.events.append("loader-entry-reset")


class LaunchReset:
    def __init__(self, device: Device, uses_usb: bool) -> None:
        assert uses_usb is False
        self.device = device

    def reset(self) -> None:
        assert self.device.dtr is False
        self.device.events.append("direct-hard-reset-launch")


class ROM:
    CHIP_NAME = "ESP32-P4"
    IMAGE_CHIP_ID = 18

    def __init__(self, device: Device, baud: int, trace: bool) -> None:
        assert (baud, trace) == (115200, False)
        self._port = device
        self.device = device
        self.mac = MAC_A
        self.flash = bytearray(0x90000)
        self.flash[0x8000:0x8C00] = table()
        self.device.events.append("rom-created")

    def connect(self, mode: str, attempts: int, warnings: bool) -> None:
        assert (mode, attempts, warnings) == ("no_reset", 1, False)
        self.device.events.append("connect-no-reset-once")

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
        if self.device.mode == "security-after-write" and "stub-finish-sync-false" in self.device.events:
            return {"parsed_flags": {"SECURE_BOOT_EN": True}, "flash_crypt_cnt": 0}
        return {"parsed_flags": {"SECURE_BOOT_EN": False}, "flash_crypt_cnt": 0}

    def get_secure_boot_enabled(self) -> bool:
        return False

    def get_flash_encryption_enabled(self) -> bool:
        return False

    def run_stub(self, image):
        assert image == "fake-stub"
        self.device.events.append("run-stub")
        return Stub(self)


class Stub(ROM):
    IS_STUB = True
    FLASH_WRITE_SIZE = 0x4000

    def __init__(self, rom: ROM) -> None:
        self._port = rom._port
        self.device = rom.device
        self.mac = rom.mac
        self.flash = rom.flash
        self.cache = {"flash_id": None}
        self.read_count = 0
        self.write_offset = 0

    def flash_id(self) -> int:
        if self.cache["flash_id"] is None:
            if self.device.mode == "density-before-write" and self.device.events.count("physical-flash-id") >= 3:
                self.cache["flash_id"] = 0x1740C8
            elif self.device.mode == "high-byte-padded" and self.device.events.count("physical-flash-id") >= 1:
                self.cache["flash_id"] = 0xFF1840C8
            elif self.device.mode == "bad-prepare-final-high" and self.device.events.count("physical-flash-id") == 1:
                self.cache["flash_id"] = 0x7F1840C8
            elif self.device.mode == "bad-prepare-final-low" and self.device.events.count("physical-flash-id") == 1:
                self.cache["flash_id"] = 0x001740C8
            elif self.device.mode == "bad-xmc-retry-high" and self.device.events.count("physical-flash-id") == 1:
                self.cache["flash_id"] = 0x7F1840C8
            else:
                self.cache["flash_id"] = 0x1840C8
            self.device.events.append("physical-flash-id")
        return self.cache["flash_id"]

    def read_spiflash_sfdp(self, address: int, length: int) -> int:
        assert (address, length) == (0x10, 8)
        self.device.events.append("read-sfdp-manufacturer")
        return 0x20 if self.device.mode == "bad-xmc-retry-high" else 0xC8

    def run_spiflash_command(self, command: int) -> None:
        self.device.events.append(("spi-command", command))

    def flash_set_parameters(self, size: int) -> None:
        assert size == 16 * 1024 * 1024
        self.device.events.append("flash-size-16MiB")

    def read_flash(self, offset: int, length: int, progress_fn=None) -> bytes:
        assert progress_fn is None
        self.read_count += 1
        self.device.events.append(("read", self.read_count, offset, length))
        payload = bytes(self.flash[offset:offset + length])
        if self.device.mode == "short-read" and self.read_count == 3:
            return payload[:-1]
        if self.device.mode == "port-swap" and self.read_count == 2:
            self._port = object()
        if self.device.mode == "descriptor-swap" and self.read_count == 2:
            self.device.swap_descriptor()
        if self.device.mode == "identity-swap" and self.read_count == 2:
            self.mac = MAC_B
        if self.device.mode == "tail-corrupt" and self.read_count >= 3 and offset == 0x10000:
            payload = payload[:-1] + bytes((payload[-1] ^ 1,))
        return payload

    def flash_begin(self, size: int, offset: int, encrypted_write: bool = False) -> int:
        assert encrypted_write is False
        self.device.events.append(("flash-begin", size, offset))
        self.write_offset = offset
        return size // self.FLASH_WRITE_SIZE

    def flash_block(self, block: bytes, sequence: int, encrypted: bool = False) -> None:
        assert encrypted is False and len(block) == self.FLASH_WRITE_SIZE
        if self.device.mode == "partial-write" and sequence == 1:
            self.device.events.append("partial-write-failure")
            raise OSError("injected partial write")
        self.device.events.append(("flash-block", sequence))
        start = self.write_offset + sequence * self.FLASH_WRITE_SIZE
        self.flash[start:start + len(block)] = block

    def flash_finish(self, reboot: bool = True) -> None:
        assert reboot is False
        self.device.events.append("stub-finish-sync-false")
        if self.device.mode == "partition-corrupt-on-write":
            self.flash[0x8000] ^= 1

    def run(self, *args, **kwargs):
        raise AssertionError("esptool run is prohibited")

    def soft_reset(self, *args, **kwargs):
        raise AssertionError("soft reset is prohibited")

    def hard_reset(self, *args, **kwargs):
        raise AssertionError("configurable stub hard reset is prohibited")


class InstallTests(unittest.TestCase):
    def setUp(self) -> None:
        self.saved = {
            name: getattr(INSTALL, name) for name in (
                "EXPECTED_BYTES", "EXPECTED_SHA256", "EXPECTED_SPAN_BYTES",
                "EXPECTED_DEVICE_SHA256",
            )
        }
        INSTALL.EXPECTED_BYTES = len(ARTIFACT)
        INSTALL.EXPECTED_SHA256 = hashlib.sha256(ARTIFACT).hexdigest()
        INSTALL.EXPECTED_SPAN_BYTES = SPAN
        INSTALL.EXPECTED_DEVICE_SHA256 = hashlib.sha256(b"020000000001").hexdigest()
        self.old_stub_hash = INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256
        INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256 = hashlib.sha256(b"stub").hexdigest()

    def tearDown(self) -> None:
        for name, value in self.saved.items():
            setattr(INSTALL, name, value)
        INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256 = self.old_stub_hash

    def case(self, mode: str = "ok"):
        temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(temporary.name)
        artifact = root / "app.bin"
        artifact.write_bytes(ARTIFACT)
        artifact.chmod(0o400)
        stub = root / "stub.json"
        stub.write_bytes(b"stub")
        device = Device(root, mode)
        runtime = INSTALL.Runtime(
            ROM, LoaderReset, LaunchReset, lambda _rom, _path: "fake-stub",
            {"legacy_rev1_stub": {"path": str(stub), "sha256": hashlib.sha256(b"stub").hexdigest()}},
        )
        return temporary, artifact, device, runtime

    def run_case(self, mode: str = "ok"):
        temporary, artifact, device, runtime = self.case(mode)
        try:
            result = INSTALL.install_same_handle(
                device=device, artifact_path=artifact, runtime=runtime,
                readback_chunk_bytes=0x4000,
            )
            return result, list(device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_success_order_and_exact_counts(self) -> None:
        result, events = self.run_case()
        self.assertEqual(result["mutation_span_bytes"], SPAN)
        self.assertEqual(result["readback_sha256"], result["mutation_span_sha256"])
        self.assertEqual(result["exclusive_transaction_loader_entry_reset_count"], 1)
        self.assertEqual(result["exclusive_transaction_application_launch_reset_count"], 1)
        self.assertEqual(result["application_launch_count"], 1)
        self.assertEqual(events.count("loader-entry-reset"), 1)
        self.assertEqual(events.count("direct-hard-reset-launch"), 1)
        self.assertEqual(events.count("stub-finish-sync-false"), 1)
        write = events.index(("flash-begin", SPAN, 0x10000))
        sync = events.index("stub-finish-sync-false")
        launch = events.index("direct-hard-reset-launch")
        final_read = max(i for i, value in enumerate(events) if isinstance(value, tuple) and value[0] == "read")
        self.assertLess(write, sync)
        self.assertLess(sync, final_read)
        self.assertLess(final_read, launch)

    def assert_fails_without_launch(self, mode: str, message: str) -> None:
        temporary, artifact, device, runtime = self.case(mode)
        try:
            with self.assertRaisesRegex(Exception, message):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, runtime=runtime,
                    readback_chunk_bytes=0x4000,
                )
            self.assertNotIn("direct-hard-reset-launch", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_transport_swap_fails(self) -> None:
        self.assert_fails_without_launch("port-swap", "left the exclusive UART")

    def test_descriptor_swap_fails(self) -> None:
        self.assert_fails_without_launch("descriptor-swap", "handle changed")

    def test_identity_swap_fails_before_write(self) -> None:
        temporary, artifact, device, runtime = self.case("identity-swap")
        try:
            with self.assertRaisesRegex(Exception, "sealed device identity"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, runtime=runtime,
                    readback_chunk_bytes=0x4000,
                )
            self.assertFalse(any(
                isinstance(event, tuple) and event[0] == "flash-begin"
                for event in device.events
            ))
            self.assertNotIn("direct-hard-reset-launch", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_short_read_fails(self) -> None:
        self.assert_fails_without_launch("short-read", "wrong byte count")

    def test_partial_write_never_launches(self) -> None:
        self.assert_fails_without_launch("partial-write", "injected partial write")

    def test_partition_table_change_never_launches(self) -> None:
        self.assert_fails_without_launch(
            "partition-corrupt-on-write", "partition table changed during app-only write"
        )

    def test_full_span_ff_tail_corruption_never_launches(self) -> None:
        self.assert_fails_without_launch("tail-corrupt", "full mutation-span readback")

    def test_security_change_after_write_never_launches(self) -> None:
        self.assert_fails_without_launch("security-after-write", "secure boot is enabled")

    def test_density_change_before_write_never_launches(self) -> None:
        temporary, artifact, device, runtime = self.case("density-before-write")
        try:
            with self.assertRaisesRegex(Exception, "exact flash JEDEC check failed at write-boundary"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, runtime=runtime,
                    readback_chunk_bytes=0x4000,
                )
            self.assertFalse(any(
                isinstance(event, tuple) and event[0] == "flash-begin"
                for event in device.events
            ))
            self.assertNotIn("direct-hard-reset-launch", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_high_byte_padded_exact_jedec_is_canonicalized(self) -> None:
        result, events = self.run_case("high-byte-padded")
        self.assertEqual(result["initial_flash_id"], "0x1840c8")
        self.assertEqual(result["prewrite_flash_id"], "0x1840c8")
        self.assertEqual(result["write_boundary_flash_id"], "0x1840c8")
        self.assertEqual(result["prelaunch_flash_id"], "0x1840c8")
        self.assertGreaterEqual(events.count("physical-flash-id"), 5)

    def test_prepare_final_rdid_rejects_wrong_identity_before_write(self) -> None:
        for mode in ("bad-prepare-final-high", "bad-prepare-final-low"):
            with self.subTest(mode=mode):
                temporary, artifact, device, runtime = self.case(mode)
                try:
                    with self.assertRaisesRegex(
                        INSTALL.InstallError,
                        "exact flash JEDEC check failed at post-66-99-final-rdid",
                    ):
                        INSTALL.install_same_handle(
                            device=device, artifact_path=artifact, runtime=runtime,
                            readback_chunk_bytes=0x4000,
                        )
                    self.assertEqual(device.events.count("physical-flash-id"), 2)
                    self.assertIn(("spi-command", 0x66), device.events)
                    self.assertIn(("spi-command", 0x99), device.events)
                    self.assertFalse(any(
                        isinstance(event, tuple) and event[0] == "flash-begin"
                        for event in device.events
                    ))
                finally:
                    device.close()
                    temporary.cleanup()

    def test_xmc_retry_rdid_is_checked_before_policy_use(self) -> None:
        temporary, artifact, device, runtime = self.case("bad-xmc-retry-high")
        try:
            with self.assertRaisesRegex(
                INSTALL.InstallError,
                "exact flash JEDEC check failed at post-xmc-startup-rdid",
            ):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, runtime=runtime,
                    readback_chunk_bytes=0x4000,
                )
            self.assertEqual(device.events.count("physical-flash-id"), 2)
            for command in (0xB9, 0x79, 0xFF, 0xAB):
                self.assertIn(("spi-command", command), device.events)
            self.assertFalse(any(
                isinstance(event, tuple) and event[0] == "flash-begin"
                for event in device.events
            ))
        finally:
            device.close()
            temporary.cleanup()

    def test_exact_jedec_rejects_wrong_low24_and_high_extension(self) -> None:
        for value in (0x001740C8, 0x001840EF, 0x7F1840C8, 0x1001840C8, -1, True):
            with self.subTest(value=value):
                with self.assertRaisesRegex(INSTALL.InstallError, "exact flash JEDEC check failed"):
                    INSTALL._canonical_exact_flash_id(value, "unit")

    def test_runtime_hash_drift_is_rejected(self) -> None:
        binding = {
            name: {"sha256": expected}
            for name, expected in INSTALL.EXPECTED_RUNTIME_HASHES.items()
        }
        binding["legacy_rev1_stub"] = {
            "sha256": INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256
        }
        binding["reset"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(INSTALL.InstallError, "runtime changed: reset"):
            INSTALL._validate_runtime_binding(binding)

    def test_serial_runtime_hash_drift_is_rejected(self) -> None:
        original = INSTALL.EXPECTED_SERIAL_RUNTIME
        with tempfile.TemporaryDirectory() as temp:
            paths = {}
            expected = {}
            for index, name in enumerate(original):
                path = pathlib.Path(temp) / name
                contents = bytes((index, index + 1, index + 2))
                path.write_bytes(contents)
                paths[name] = path
                expected[name] = {
                    "path": str(path.resolve()),
                    "bytes": len(contents),
                    "sha256": hashlib.sha256(contents).hexdigest(),
                }
            expected["termios"]["sha256"] = "0" * 64
            INSTALL.EXPECTED_SERIAL_RUNTIME = expected
            try:
                with self.assertRaisesRegex(
                    INSTALL.InstallError, "serial runtime changed: termios"
                ):
                    INSTALL._validate_serial_runtime_paths(paths)
            finally:
                INSTALL.EXPECTED_SERIAL_RUNTIME = original

    def test_source_bans_reopen_and_ambiguous_launch_paths(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        transaction = source[source.index("def install_same_handle("):source.index("def _write_result(")]
        self.assertNotIn("soft_reset(", transaction)
        self.assertNotIn(".run(", transaction)
        self.assertNotIn("flash_finish(reboot=True)", transaction)
        self.assertNotIn(".open()", transaction)
        self.assertNotIn(".connect(", transaction[transaction.index('rom.connect("no_reset"') + 25:])
        self.assertIn("selected.launch_reset_class(device, uses_usb=False).reset()", transaction)

    def test_result_path_is_rejected_before_serial_open(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            parent = pathlib.Path(temp) / "result-parent"
            parent.mkdir(mode=0o755)
            with self.assertRaisesRegex(INSTALL.InstallError, "exact 0700"):
                INSTALL._validate_result_path(parent / "result.json")


if __name__ == "__main__":
    unittest.main()
