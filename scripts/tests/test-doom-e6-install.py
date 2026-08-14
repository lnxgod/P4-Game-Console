#!/usr/bin/env python3
"""Hermetic fail/crash/restore tests for the E6 retained-UART installer."""

from __future__ import annotations

import base64
import hashlib
import importlib.util
import json
import os
import pathlib
import struct
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/doom-e6-install.py"
SPEC = importlib.util.spec_from_file_location("doom_e6_install_test", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
INSTALL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALL)

ARTIFACT = bytes((index * 31 + 11) & 0xFF for index in range(32_000))
SPAN = 32_768
ORIGINAL = bytes((index * 7 + 3) & 0xFF for index in range(SPAN))
MAC = (0x02, 0, 0, 0, 0, 1)
DEVICE_SHA = hashlib.sha256(b"020000000001").hexdigest()
STUB_JSON = (json.dumps({
    "entry": 0x4FF00100,
    "text": base64.b64encode(b"trusted-stub-text").decode("ascii"),
    "text_start": 0x4FF00000,
    "data": base64.b64encode(b"trusted-stub-data").decode("ascii"),
    "data_start": 0x4FF10000,
    "bss_start": 0x4FF11000,
}, separators=(",", ":")) + "\n").encode()


def entry(name: str, ptype: int, subtype: int, offset: int, size: int) -> bytes:
    encoded = name.encode("ascii")
    return struct.pack(
        "<2sBBII16sI", INSTALL.RESTORE.ENTRY_MAGIC, ptype, subtype,
        offset, size, encoded + b"\x00" * (16 - len(encoded)), 0,
    )


def partition_table() -> bytes:
    body = b"".join((
        entry("nvs", 1, 2, 0x9000, 0x6000),
        entry("phy_init", 1, 1, 0xF000, 0x1000),
        entry("factory", 0, 0, 0x10000, 0xB00000),
    ))
    value = body + INSTALL.RESTORE.MD5_RECORD_PREFIX + hashlib.md5(body).digest()
    return value + b"\xff" * (INSTALL.RESTORE.PARTITION_TABLE_BYTES - len(value))


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
        self.flash = bytearray(0x90000)
        self.flash[0x8000:0x8C00] = partition_table()
        self.flash[0x10000:0x10000 + SPAN] = ORIGINAL
        self.mac = MAC
        self.read_count = 0

    def fileno(self) -> int:
        return self.fd

    def read(self, count: int) -> bytes:
        self.events.append(("capture-read", count))
        return b""

    def close(self) -> None:
        if self.is_open:
            os.close(self.fd)
            self.is_open = False


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
        assert not self.device.dtr
        self.device.events.append("direct-hard-reset-launch")


class ROM:
    CHIP_NAME = "ESP32-P4"
    IMAGE_CHIP_ID = 18

    def __init__(self, device: Device, baud: int, trace: bool) -> None:
        assert (baud, trace) == (115200, False)
        self._port = device
        self.device = device

    def connect(self, mode: str, attempts: int, warnings: bool) -> None:
        assert (mode, attempts, warnings) == ("no_reset", 1, False)
        self.device.events.append("connect-no-reset")

    def uses_usb_otg(self) -> bool:
        return False

    def get_chip_revision(self) -> int:
        return 103

    def read_mac(self):
        self.device.events.append("identity")
        return self.device.mac

    def get_security_info(self, cache: bool = False):
        assert cache is False
        return {"parsed_flags": {"SECURE_BOOT_EN": False}, "flash_crypt_cnt": 0}

    def get_secure_boot_enabled(self) -> bool:
        return False

    def get_flash_encryption_enabled(self) -> bool:
        return False

    def run_stub(self, image):
        assert image.text == b"trusted-stub-text"
        assert image.data == b"trusted-stub-data"
        assert image.entry == 0x4FF00100
        self.device.events.append("run-stub")
        return Stub(self.device)


class Stub(ROM):
    IS_STUB = True
    FLASH_WRITE_SIZE = 0x4000

    def __init__(self, device: Device) -> None:
        self._port = device
        self.device = device
        self.cache = {"flash_id": None}
        self.write_offset = 0

    def flash_id(self) -> int:
        if self.cache["flash_id"] is None:
            self.cache["flash_id"] = 0xFF1840C8
            self.device.events.append("physical-flash-id")
        return self.cache["flash_id"]

    def read_spiflash_sfdp(self, address: int, length: int) -> int:
        assert (address, length) == (0x10, 8)
        return 0xC8

    def run_spiflash_command(self, command: int) -> None:
        self.device.events.append(("spi", command))

    def flash_set_parameters(self, size: int) -> None:
        assert size == 16 * 1024 * 1024

    def read_flash(self, offset: int, length: int, progress_fn=None) -> bytes:
        assert progress_fn is None
        self.device.read_count += 1
        self.device.events.append(("read", offset, length))
        payload = bytes(self.device.flash[offset:offset + length])
        if (
            self.device.mode == "corrupt-e6-readback"
            and self.device.events.count("loader-entry-reset") == 1
            and any(isinstance(event, tuple) and event[0] == "flash-block"
                    for event in self.device.events)
            and offset == 0x10000
        ):
            return bytes((payload[0] ^ 1,)) + payload[1:]
        return payload

    def flash_begin(self, size: int, offset: int, encrypted_write: bool = False) -> int:
        assert encrypted_write is False
        self.device.events.append(("flash-begin", size, offset, self.FLASH_WRITE_SIZE))
        self.write_offset = offset
        return size // self.FLASH_WRITE_SIZE

    def flash_block(self, block: bytes, sequence: int, encrypted: bool = False) -> None:
        assert encrypted is False and len(block) == self.FLASH_WRITE_SIZE
        restore_phase = self.device.events.count("loader-entry-reset") >= 2
        if self.device.mode == "partial-e6-write" and not restore_phase and sequence == 1:
            raise OSError("injected E6 partial write")
        if self.device.mode == "restore-write-fail" and restore_phase and sequence == 0:
            raise OSError("injected restore write failure")
        self.device.events.append(("flash-block", sequence, len(block), restore_phase))
        start = self.write_offset + sequence * self.FLASH_WRITE_SIZE
        self.device.flash[start:start + len(block)] = block

    def flash_finish(self, reboot: bool = True) -> None:
        assert reboot is False
        self.device.events.append("flash-finish-sync")


class Capture:
    @staticmethod
    def capture_open_handle(device: Device, seconds: float, min_stats: int = 2):
        assert seconds == 10.0 and min_stats == 2
        assert device.is_open and device.exclusive and not device.dtr and not device.rts
        device.events.append("capture-retained-handle")
        result = "fail" if device.mode in {"capture-fail", "restore-write-fail"} else "pass"
        return b"exact-startup\n", {"schema": 1, "result": result}


class InstallTests(unittest.TestCase):
    def setUp(self) -> None:
        self.old_device = INSTALL.EXPECTED_DEVICE_SHA256
        self.old_stub_hash = INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256
        INSTALL.EXPECTED_DEVICE_SHA256 = DEVICE_SHA
        self.old_e5_bytes = INSTALL.INSTALLED_E5_BYTES
        self.old_e5_hash = INSTALL.INSTALLED_E5_SHA256
        self.old_e5_padded_bytes = INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES
        self.old_e5_padded_hash = INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256
        INSTALL.INSTALLED_E5_BYTES = len(ORIGINAL) - 16
        INSTALL.INSTALLED_E5_SHA256 = hashlib.sha256(ORIGINAL[:-16]).hexdigest()
        INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES = len(ORIGINAL)
        INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256 = hashlib.sha256(ORIGINAL).hexdigest()
        INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256 = hashlib.sha256(STUB_JSON).hexdigest()

    def tearDown(self) -> None:
        INSTALL.EXPECTED_DEVICE_SHA256 = self.old_device
        INSTALL.INSTALLED_E5_BYTES = self.old_e5_bytes
        INSTALL.INSTALLED_E5_SHA256 = self.old_e5_hash
        INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES = self.old_e5_padded_bytes
        INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256 = self.old_e5_padded_hash
        INSTALL.RESTORE.LEGACY_REV1_STUB_SHA256 = self.old_stub_hash

    def case(self, mode: str = "ok"):
        temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(temporary.name)
        artifact = root / "app.bin"
        artifact.write_bytes(ARTIFACT)
        artifact.chmod(0o400)
        recovery = root / "recovery"
        recovery.mkdir(mode=0o700)
        stub = root / "stub.json"
        stub.write_bytes(STUB_JSON)
        device = Device(root, mode)

        def forbidden_stub_factory(_rom, _path):
            raise AssertionError("stub path was reopened through runtime factory")

        runtime = INSTALL.E5.Runtime(
            ROM, LoaderReset, LaunchReset, forbidden_stub_factory,
            {"legacy_rev1_stub": {
                "path": str(stub),
                "sha256": hashlib.sha256(STUB_JSON).hexdigest(),
            }},
        )
        contract = INSTALL.ArtifactContract(
            0x10000, len(ARTIFACT), hashlib.sha256(ARTIFACT).hexdigest(), SPAN
        )
        return temporary, artifact, recovery, device, runtime, contract

    def run_case(self, mode: str = "ok"):
        case = self.case(mode)
        temporary, artifact, recovery, device, runtime, contract = case
        try:
            result = INSTALL.install_same_handle(
                device=device, artifact_path=artifact, contract=contract,
                recovery_directory=recovery, runtime=runtime,
                capture_module=Capture, capture_seconds=10.0,
                readback_chunk_bytes=0x4000,
            )
            ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
            return result, ledger, list(device.events), bytes(device.flash[0x10000:0x10000 + SPAN])
        finally:
            device.close()
            temporary.cleanup()

    def test_success_is_one_handle_and_durably_accepted(self) -> None:
        result, ledger, events, installed = self.run_case()
        expected = ARTIFACT + b"\xff" * (SPAN - len(ARTIFACT))
        self.assertEqual(installed, expected)
        self.assertEqual(result["readback_sha256"], hashlib.sha256(expected).hexdigest())
        self.assertEqual(ledger["phase"], "e6-runtime-accepted")
        self.assertFalse(ledger["restore_required"])
        self.assertEqual(events.count("loader-entry-reset"), 1)
        self.assertEqual(events.count("direct-hard-reset-launch"), 1)
        self.assertEqual(events.count("capture-retained-handle"), 1)
        self.assertLess(events.index("direct-hard-reset-launch"), events.index("capture-retained-handle"))

    def test_capture_failure_automatically_restores_and_relaunches_e5(self) -> None:
        temporary, artifact, recovery, device, runtime, contract = self.case("capture-fail")
        try:
            with self.assertRaisesRegex(INSTALL.InstallError, "exact E5 was restored"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, contract=contract,
                    recovery_directory=recovery, runtime=runtime,
                    capture_module=Capture, capture_seconds=10.0,
                    readback_chunk_bytes=0x4000,
                )
            ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
            self.assertEqual(ledger["phase"], "restored-e5-launched")
            self.assertFalse(ledger["restore_required"])
            self.assertEqual(ledger["startup_capture_result"], "fail")
            self.assertEqual(
                ledger["startup_capture_rejection_reason"],
                "retained-uart-startup-summary-result-not-pass",
            )
            for binding in ledger["startup_capture"].values():
                bound = pathlib.Path(binding["path"])
                self.assertTrue(bound.is_file())
                self.assertEqual(INSTALL._sha256_file(bound), binding["sha256"])
            self.assertEqual(bytes(device.flash[0x10000:0x10000 + SPAN]), ORIGINAL)
            self.assertEqual(device.events.count("loader-entry-reset"), 2)
            self.assertEqual(device.events.count("direct-hard-reset-launch"), 2)
            self.assertIn("capture-retained-handle", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_partial_write_and_corrupt_readback_both_restore(self) -> None:
        for mode in ("partial-e6-write", "corrupt-e6-readback"):
            with self.subTest(mode=mode):
                temporary, artifact, recovery, device, runtime, contract = self.case(mode)
                try:
                    with self.assertRaisesRegex(INSTALL.InstallError, "exact E5 was restored"):
                        INSTALL.install_same_handle(
                            device=device, artifact_path=artifact, contract=contract,
                            recovery_directory=recovery, runtime=runtime,
                            capture_module=Capture, capture_seconds=10.0,
                            readback_chunk_bytes=0x4000,
                        )
                    self.assertEqual(bytes(device.flash[0x10000:0x10000 + SPAN]), ORIGINAL)
                    ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
                    self.assertFalse(ledger["restore_required"])
                finally:
                    device.close()
                    temporary.cleanup()

    def test_restore_failure_stays_durably_required(self) -> None:
        temporary, artifact, recovery, device, runtime, contract = self.case("restore-write-fail")
        try:
            with self.assertRaisesRegex(INSTALL.InstallError, "recovery remains required"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, contract=contract,
                    recovery_directory=recovery, runtime=runtime,
                    capture_module=Capture, capture_seconds=10.0,
                    readback_chunk_bytes=0x4000,
                )
            ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
            self.assertEqual(ledger["phase"], "restore-failed")
            self.assertTrue(ledger["restore_required"])
            self.assertGreaterEqual(ledger["restore_attempt_count"], 1)
            self.assertGreaterEqual(ledger["restore_write_attempt_count"], 1)
        finally:
            device.close()
            temporary.cleanup()

    def test_standalone_recovery_consumes_crash_ledger(self) -> None:
        temporary, artifact, recovery, device, runtime, contract = self.case()
        del artifact
        try:
            table = bytes(device.flash[0x8000:0x8C00])
            table_binding = INSTALL._table_binding(recovery, table, contract, runtime)
            preimage_binding = INSTALL._preimage_binding(recovery, ORIGINAL, contract)
            device.flash[0x10000:0x10000 + SPAN] = b"X" * SPAN
            ledger = {
                "schema": 1, "phase": "install-write-attempted",
                "transition_count": 3, "restore_required": True,
                "device_identity_sha256": DEVICE_SHA,
                "offset": contract.offset, "artifact_bytes": contract.artifact_bytes,
                "artifact_sha256": contract.artifact_sha256,
                "mutation_span_bytes": contract.mutation_span_bytes,
                "partition_table_binding": table_binding,
                "preimage_binding": preimage_binding,
                "restore_attempt_count": 0, "restore_write_attempt_count": 0,
                "recovery_inventory": {
                    str(path.relative_to(ROOT)): INSTALL._sha256_file(path)
                    for path in (
                        INSTALL.SCRIPT_PATH,
                        INSTALL.CAPTURE_PATH.resolve(strict=True),
                        INSTALL.E5_INSTALL_PATH.resolve(strict=True),
                        INSTALL.E5.RESTORE_PATH.resolve(strict=True),
                    )
                },
            }
            INSTALL._atomic_ledger(recovery / INSTALL.LEDGER_NAME, ledger)
            stale = recovery / f".{INSTALL.LEDGER_NAME}.next"
            INSTALL._write_new_private(stale, b'{"truncated":')
            result = INSTALL.recover_same_handle(
                device=device, recovery_directory=recovery, runtime=runtime
            )
            self.assertEqual(result["sha256"], hashlib.sha256(ORIGINAL).hexdigest())
            self.assertEqual(bytes(device.flash[0x10000:0x10000 + SPAN]), ORIGINAL)
            final = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
            self.assertEqual(final["phase"], "restored-e5-launched")
            self.assertFalse(final["restore_required"])
            self.assertEqual(device.events.count("direct-hard-reset-launch"), 1)
            self.assertFalse(stale.exists())
        finally:
            device.close()
            temporary.cleanup()

    def test_stale_ledger_replacement_rejects_symlink_or_unsafe_state(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(temporary.name)
        recovery = root / "recovery"
        recovery.mkdir(mode=0o700)
        ledger = recovery / INSTALL.LEDGER_NAME
        stale = recovery / f".{INSTALL.LEDGER_NAME}.next"
        target = root / "target"
        target.write_bytes(b"do-not-delete")
        stale.symlink_to(target)
        try:
            with self.assertRaisesRegex(INSTALL.InstallError, "owned 0600"):
                INSTALL._discard_uncommitted_ledger_replacement(
                    ledger, {"restore_required": True}
                )
            self.assertTrue(stale.is_symlink())
            stale.unlink()
            INSTALL._write_new_private(stale, b"uncommitted")
            with self.assertRaisesRegex(INSTALL.InstallError, "without a restore-required"):
                INSTALL._discard_uncommitted_ledger_replacement(
                    ledger, {"restore_required": False}
                )
            self.assertTrue(stale.exists())
        finally:
            temporary.cleanup()

    def test_recovery_rejects_changed_helper_inventory_before_reset(self) -> None:
        temporary, _artifact, recovery, device, runtime, contract = self.case()
        try:
            table = bytes(device.flash[0x8000:0x8C00])
            state = {
                "schema": 1, "phase": "install-write-attempted",
                "transition_count": 3, "restore_required": True,
                "device_identity_sha256": DEVICE_SHA,
                "offset": contract.offset, "artifact_bytes": contract.artifact_bytes,
                "artifact_sha256": contract.artifact_sha256,
                "mutation_span_bytes": contract.mutation_span_bytes,
                "partition_table_binding": INSTALL._table_binding(
                    recovery, table, contract, runtime
                ),
                "preimage_binding": INSTALL._preimage_binding(
                    recovery, ORIGINAL, contract
                ),
                "restore_attempt_count": 0, "restore_write_attempt_count": 0,
                "recovery_inventory": {
                    "scripts/doom-e6-install.py": "0" * 64,
                    "scripts/capture-doom-e6-runtime.py": "0" * 64,
                    "scripts/doom-e5-install.py": "0" * 64,
                    "scripts/gamepad-diag-restore.py": "0" * 64,
                },
            }
            INSTALL._atomic_ledger(recovery / INSTALL.LEDGER_NAME, state)
            with self.assertRaisesRegex(INSTALL.InstallError, "frozen source"):
                INSTALL.recover_same_handle(
                    device=device, recovery_directory=recovery,
                    runtime=runtime,
                )
            self.assertNotIn("loader-entry-reset", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_restore_requires_exact_installed_e5_prefix(self) -> None:
        temporary, _artifact, recovery, device, runtime, contract = self.case()
        try:
            table = bytes(device.flash[0x8000:0x8C00])
            wrong = b"Z" * SPAN
            state = {
                "schema": 1, "phase": "install-write-attempted",
                "transition_count": 3, "restore_required": True,
                "device_identity_sha256": DEVICE_SHA,
                "offset": contract.offset, "artifact_bytes": contract.artifact_bytes,
                "artifact_sha256": contract.artifact_sha256,
                "mutation_span_bytes": contract.mutation_span_bytes,
                "partition_table_binding": INSTALL._table_binding(
                    recovery, table, contract, runtime
                ),
                "preimage_binding": INSTALL._preimage_binding(
                    recovery, wrong, contract
                ),
                "restore_attempt_count": 0, "restore_write_attempt_count": 0,
            }
            with self.assertRaisesRegex(INSTALL.InstallError, "exact installed E5"):
                INSTALL._restore_e5_same_handle(
                    device, runtime, recovery / INSTALL.LEDGER_NAME, state
                )
            self.assertNotIn("loader-entry-reset", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_live_predecessor_requires_exact_whole_padded_e5_span(self) -> None:
        temporary, artifact, recovery, device, runtime, contract = self.case()
        try:
            device.flash[0x10000 + SPAN - 1] ^= 1
            with self.assertRaisesRegex(INSTALL.InstallError, "padded span"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, contract=contract,
                    recovery_directory=recovery, runtime=runtime,
                    capture_module=Capture, capture_seconds=10.0,
                    readback_chunk_bytes=0x4000,
                )
            self.assertFalse(any(
                isinstance(event, tuple) and event[0] == "flash-begin"
                for event in device.events
            ))
        finally:
            device.close()
            temporary.cleanup()

    def test_sealed_table_and_preimage_are_reread_before_write_mark(self) -> None:
        for binding_name in ("partition_table_binding", "preimage_binding"):
            with self.subTest(binding=binding_name):
                temporary, artifact, recovery, device, runtime, contract = self.case()
                original_transition = INSTALL._transition

                def tampering_transition(path, state, phase, **updates):
                    original_transition(path, state, phase, **updates)
                    if phase == "preimage-sealed":
                        bound = pathlib.Path(state[binding_name]["path"])
                        payload = bytearray(bound.read_bytes())
                        payload[-1] ^= 1
                        bound.write_bytes(payload)

                INSTALL._transition = tampering_transition
                try:
                    with self.assertRaisesRegex(Exception, "sealed binding"):
                        INSTALL.install_same_handle(
                            device=device, artifact_path=artifact, contract=contract,
                            recovery_directory=recovery, runtime=runtime,
                            capture_module=Capture, capture_seconds=10.0,
                            readback_chunk_bytes=0x4000,
                        )
                    self.assertFalse(any(
                        isinstance(event, tuple) and event[0] == "flash-begin"
                        for event in device.events
                    ))
                    ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
                    self.assertEqual(ledger["phase"], "preimage-sealed")
                    self.assertFalse(ledger["restore_required"])
                finally:
                    INSTALL._transition = original_transition
                    device.close()
                    temporary.cleanup()

    def test_recovery_rejects_redirected_ledger_before_reset(self) -> None:
        temporary, _artifact, recovery, device, runtime, contract = self.case()
        try:
            table = bytes(device.flash[0x8000:0x8C00])
            state = {
                "schema": 1, "phase": "install-write-attempted",
                "transition_count": 3, "restore_required": True,
                "device_identity_sha256": DEVICE_SHA,
                "offset": 0x20000, "artifact_bytes": contract.artifact_bytes,
                "artifact_sha256": contract.artifact_sha256,
                "mutation_span_bytes": contract.mutation_span_bytes,
                "partition_table_binding": INSTALL._table_binding(
                    recovery, table, contract, runtime
                ),
                "preimage_binding": INSTALL._preimage_binding(
                    recovery, ORIGINAL, contract
                ),
                "restore_attempt_count": 0, "restore_write_attempt_count": 0,
            }
            with self.assertRaisesRegex(INSTALL.InstallError, "canonical E6 app span"):
                INSTALL._restore_e5_same_handle(
                    device, runtime, recovery / INSTALL.LEDGER_NAME, state
                )
            self.assertNotIn("loader-entry-reset", device.events)
        finally:
            device.close()
            temporary.cleanup()

    def test_private_preimage_and_ledger_modes(self) -> None:
        temporary, artifact, recovery, device, runtime, contract = self.case("capture-fail")
        try:
            with self.assertRaises(INSTALL.InstallError):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, contract=contract,
                    recovery_directory=recovery, runtime=runtime,
                    capture_module=Capture, capture_seconds=10.0,
                    readback_chunk_bytes=0x4000,
                )
            for name in (INSTALL.LEDGER_NAME, INSTALL.PREIMAGE_NAME, INSTALL.PARTITION_TABLE_NAME):
                self.assertEqual(stat_mode(recovery / name), 0o600)
            self.assertEqual((recovery / INSTALL.PREIMAGE_NAME).read_bytes(), ORIGINAL)
        finally:
            device.close()
            temporary.cleanup()

    def test_write_mark_is_adjacent_before_flash_begin(self) -> None:
        original_transition = INSTALL._transition
        seen: list[str] = []

        def recording(path, state, phase, **updates):
            seen.append(phase)
            original_transition(path, state, phase, **updates)

        INSTALL._transition = recording
        try:
            result, _ledger, events, _installed = self.run_case()
            self.assertEqual(result["application_launch_count"], 1)
            self.assertIn("install-write-attempted", seen)
            # Static adjacency guards against a future operation sneaking into
            # the durable-mark-to-first-flash boundary.
            source = MODULE_PATH.read_text()
            fragment = source[
                source.index('"install-write-attempted",\n            restore_required=True'):
                source.index("_write_span(\n            stub", source.index('"install-write-attempted",\n            restore_required=True'))
            ]
            self.assertNotIn("_read_", fragment)
            self.assertTrue(any(event[0] == "flash-begin" for event in events if isinstance(event, tuple)))
        finally:
            INSTALL._transition = original_transition

    def test_authorization_digest_is_an_acyclic_outer_trust_anchor(self) -> None:
        self.assertFalse(hasattr(INSTALL, "EXPECTED_AUTH_SHA256"))
        source = MODULE_PATH.read_text()
        self.assertLess(
            source.index("_contract_from_authorization(\n            authorization"),
            source.index("device = E5.open_serial_once(port)"),
        )
        self.assertNotIn('add_parser("install")', source)
        self.assertIn('choices=("recover",)', source)
        self.assertIn("route supplies the final authorization digest after", source)

    def test_authorization_artifact_must_equal_bound_build_evidence(self) -> None:
        authorized = {
            "offset": "0x10000",
            "app_binary_bytes": len(ARTIFACT),
            "app_binary_sha256": hashlib.sha256(ARTIFACT).hexdigest(),
            "mutation_span_bytes": SPAN,
            "mutation_tail_byte": "ff",
        }
        INSTALL._validate_evidence_artifact_contract(
            authorized, {"exact_artifact": dict(authorized)}
        )
        for field, replacement in (
            ("offset", "0x20000"),
            ("app_binary_bytes", len(ARTIFACT) - 1),
            ("app_binary_sha256", "0" * 64),
            ("mutation_span_bytes", SPAN + INSTALL.WRITE_BLOCK_BYTES),
            ("mutation_tail_byte", "00"),
        ):
            with self.subTest(field=field):
                changed = dict(authorized)
                changed[field] = replacement
                with self.assertRaisesRegex(INSTALL.InstallError, "bound build evidence"):
                    INSTALL._validate_evidence_artifact_contract(
                        authorized, {"exact_artifact": changed}
                    )

    def test_json_records_are_bounded_no_follow_and_same_inode(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(temporary.name)
        record = root / "record.json"
        replacement = root / "replacement.json"
        record.write_text('{"schema":1}\n')
        replacement.write_text('{"schema":2}\n')
        record.chmod(0o600)
        replacement.chmod(0o600)
        link = root / "record-link.json"
        link.symlink_to(record)
        try:
            self.assertEqual(
                INSTALL._read_json_record(record, "test record", required_mode=0o600),
                {"schema": 1},
            )
            with self.assertRaisesRegex(INSTALL.InstallError, "metadata"):
                INSTALL._read_json_record(link, "test link", required_mode=0o600)
            old_maximum = INSTALL.MAX_JSON_RECORD_BYTES
            INSTALL.MAX_JSON_RECORD_BYTES = 4
            try:
                with self.assertRaisesRegex(INSTALL.InstallError, "bounded size"):
                    INSTALL._read_json_record(
                        record, "oversize test", required_mode=0o600
                    )
            finally:
                INSTALL.MAX_JSON_RECORD_BYTES = old_maximum

            real_open = os.open
            swapped = False

            def swap_before_open(path, flags, *args, **kwargs):
                nonlocal swapped
                if pathlib.Path(path) == record and not swapped:
                    swapped = True
                    os.replace(replacement, record)
                return real_open(path, flags, *args, **kwargs)

            with mock.patch.object(INSTALL.os, "open", side_effect=swap_before_open):
                with self.assertRaisesRegex(INSTALL.InstallError, "changed while opening"):
                    INSTALL._read_json_record(
                        record, "swapped record", required_mode=0o600
                    )
        finally:
            temporary.cleanup()

    def test_stub_machine_code_comes_from_same_validated_fd_bytes(self) -> None:
        temporary, _artifact, _recovery, device, runtime, _contract = self.case()
        stub = pathlib.Path(runtime.binding["legacy_rev1_stub"]["path"])
        replacement = stub.with_name("replacement.json")
        changed = bytearray(STUB_JSON)
        changed[0] ^= 1
        replacement.write_bytes(changed)
        replacement.chmod(0o644)
        real_open = os.open
        swapped = False

        def swap_stub(path, flags, *args, **kwargs):
            nonlocal swapped
            if pathlib.Path(path) == stub and not swapped:
                swapped = True
                os.replace(replacement, stub)
            return real_open(path, flags, *args, **kwargs)

        try:
            with mock.patch.object(INSTALL.os, "open", side_effect=swap_stub):
                with self.assertRaisesRegex(INSTALL.InstallError, "changed while opening"):
                    INSTALL._load_stub(
                        device, INSTALL.E5._handle_binding(device), runtime
                    )
            self.assertNotIn("run-stub", device.events)
            self.assertFalse(any(
                isinstance(event, tuple) and event[0] == "flash-begin"
                for event in device.events
            ))
        finally:
            device.close()
            temporary.cleanup()

    def test_exact_module_executes_validated_payload_not_current_path(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        path = pathlib.Path(temporary.name) / "helper.py"
        path.write_text("VALUE = 'malicious-path-bytes'\n")
        try:
            module = INSTALL._exec_exact_module(
                b"VALUE = 'validated-fd-bytes'\n", path,
                "doom_e6_exact_payload_test",
            )
            self.assertEqual(module.VALUE, "validated-fd-bytes")
        finally:
            temporary.cleanup()

    def test_capture_raw_and_primary_error_survive_summary_write_failure(self) -> None:
        temporary, artifact, recovery, device, runtime, contract = self.case()
        original = INSTALL._persist_capture_summary

        def fail_summary(_directory, _summary):
            raise OSError("injected summary durability failure")

        INSTALL._persist_capture_summary = fail_summary
        try:
            with self.assertRaisesRegex(INSTALL.InstallError, "exact E5 was restored"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, contract=contract,
                    recovery_directory=recovery, runtime=runtime,
                    capture_module=Capture, capture_seconds=10.0,
                    readback_chunk_bytes=0x4000,
                )
            ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
            self.assertEqual(ledger["phase"], "restored-e5-launched")
            self.assertEqual(
                pathlib.Path(ledger["startup_capture"]["raw"]["path"]).read_bytes(),
                b"exact-startup\n",
            )
            self.assertNotIn("summary", ledger["startup_capture"])
            self.assertIn("summary durability failure", ledger["primary_failure"])
            self.assertEqual(bytes(device.flash[0x10000:0x10000 + SPAN]), ORIGINAL)
        finally:
            INSTALL._persist_capture_summary = original
            device.close()
            temporary.cleanup()

    def test_capture_exception_partial_bytes_are_bound_before_restore(self) -> None:
        class CaptureRaises:
            MAX_TRANSCRIPT_BYTES = INSTALL.CAPTURE.MAX_TRANSCRIPT_BYTES

            @staticmethod
            def capture_open_handle(_device, _seconds, min_stats=2):
                raise INSTALL.CAPTURE.CaptureFailure(
                    "OSError: injected retained UART read failure",
                    b"partial-retained-uart\n", min_stats,
                )

        temporary, artifact, recovery, device, runtime, contract = self.case()
        try:
            with self.assertRaisesRegex(INSTALL.InstallError, "exact E5 was restored"):
                INSTALL.install_same_handle(
                    device=device, artifact_path=artifact, contract=contract,
                    recovery_directory=recovery, runtime=runtime,
                    capture_module=CaptureRaises, capture_seconds=10.0,
                    readback_chunk_bytes=0x4000,
                )
            ledger = INSTALL._load_ledger(recovery / INSTALL.LEDGER_NAME)
            capture_binding = ledger["startup_capture"]
            self.assertEqual(
                pathlib.Path(capture_binding["raw"]["path"]).read_bytes(),
                b"partial-retained-uart\n",
            )
            summary = json.loads(
                pathlib.Path(capture_binding["summary"]["path"]).read_text()
            )
            self.assertTrue(summary["capture_incomplete"])
            self.assertIn("UART read failure", ledger["primary_failure"])
            self.assertEqual(ledger["phase"], "restored-e5-launched")
        finally:
            device.close()
            temporary.cleanup()

    def test_release_and_rollback_bindings_are_semantic_not_opaque_hashes(self) -> None:
        release = {
            "schema": 1,
            "active": True,
            "classification": "exact-unit-operator-accepted-factory-audio-release",
            "scope": "one-device-e6-complete-factory-audio-init",
            "exact_unit": {
                "identity_sha256": INSTALL.EXPECTED_DEVICE_SHA256,
                "chip": "ESP32-P4", "chip_revision": "v1.3",
                "flash_bytes": 16_777_216,
            },
            "operator_risk_acceptance": {
                "unresolved_amplifier_topology_risk_accepted": True,
                "gpio24_pdm_clock_may_feed_codec_mclk": True,
                "population_or_continuity_proof_available": False,
                "connected_unit_only": True, "reusable_authorization": False,
                "cross_unit_authorization": False,
                "acoustic_output_not_yet_proven": True,
            },
            "operator_attestation": {
                "exact_connected_unit_prior_run_no_damage_observed": True,
                "factory_image_speaker_was_audible_to_operator": True,
                "direction": "replay-complete-pinned-factory-hardware-initializer",
                "accepts_unresolved_topology_risk_for_this_unit": True,
            },
            "pinned_factory_source": {
                "commit": "c5a437311b951aaa9d17115bf420877a8f1f7b83",
                "archive_sha256": "73b32c4d4dc89cc0b091388d6a7862d827d6548412717bcb1f36f7adb8da2e28",
                "board_source_sha256": "f0aa354307710744f37d57b8ea23942b13d6ae38c26a98ad118c606ae5b11b69",
                "codec_control_source_sha256": "f80ee68cd9e079725e9ea68d218b04c06438a86a91988e98750dfbc4b319ee18",
            },
            "complete_factory_initializer": {
                "pdm_rx_controller": 0, "pdm_clock_gpio": 24,
                "pdm_clock_hz": 1_024_000, "pdm_data_input_gpio": 26,
                "pdm_samples_consumed": False, "speaker_tx_controller": 1,
                "sample_rate_hz": 16_000, "format": "signed-pcm16-stereo",
                "lrclk_gpio": 21, "bclk_gpio": 22, "dout_gpio": 23,
                "tx_mclk": "unused", "amplifier_shutdown_gpio": 30,
                "amplifier_enable_level": 0,
                "external_codec_i2c_transactions": 0, "usb_runtime": False,
            },
            "prior_nondamaging_run": {
                "record_result": "fail-no-microphone-detected-tone",
                "serial_result": "pass",
                "microphone_result": "fail",
                "connected_unit_speaker_path_accepted": False,
                "path": "hardware/test-runs/2026-08-13-audio-direct-d23-attempt2.json",
                "sha256": "74568d7b388dd437a9dd2a0a672bb635b343329b60baa0778c7740f87350925b",
            },
            "rollback_identity": {
                "offset": "0x10000", "e5_artifact_bytes": INSTALL.INSTALLED_E5_BYTES,
                "e5_artifact_sha256": INSTALL.INSTALLED_E5_SHA256,
                "e5_padded_span_bytes": INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES,
                "e5_padded_span_sha256": INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256,
            },
        }
        INSTALL._validate_exact_unit_audio_release(release)
        changed = json.loads(json.dumps(release))
        changed["operator_risk_acceptance"]["cross_unit_authorization"] = True
        with self.assertRaisesRegex(INSTALL.InstallError, "release semantics"):
            INSTALL._validate_exact_unit_audio_release(changed)
        changed = json.loads(json.dumps(release))
        changed["complete_factory_initializer"]["pdm_clock_gpio"] = 25
        with self.assertRaisesRegex(INSTALL.InstallError, "release semantics"):
            INSTALL._validate_exact_unit_audio_release(changed)

        saved = (
            INSTALL.INSTALLED_E5_BYTES, INSTALL.INSTALLED_E5_SHA256,
            INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES,
            INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256,
        )
        INSTALL.INSTALLED_E5_BYTES = self.old_e5_bytes
        INSTALL.INSTALLED_E5_SHA256 = self.old_e5_hash
        INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES = self.old_e5_padded_bytes
        INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256 = self.old_e5_padded_hash
        try:
            manifest_path = ROOT / "test-runs/doom-e6-rollback-2026-08-13/rollback-bundle.json"
            manifest = json.loads(manifest_path.read_text())
            INSTALL._validate_rollback_bundle(manifest)
            changed = json.loads(json.dumps(manifest))
            changed["installed_identity"]["padded_span"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(INSTALL.InstallError, "file bindings"):
                INSTALL._validate_rollback_bundle(changed)
        finally:
            (
                INSTALL.INSTALLED_E5_BYTES, INSTALL.INSTALLED_E5_SHA256,
                INSTALL.INSTALLED_E5_PADDED_SPAN_BYTES,
                INSTALL.INSTALLED_E5_PADDED_SPAN_SHA256,
            ) = saved

    def test_source_bans_reopen_tx_flush_and_ambiguous_launch(self) -> None:
        source = MODULE_PATH.read_text()
        transaction = source[source.index("def install_same_handle("):source.index("def recover_same_handle(")]
        self.assertNotIn(".open(", transaction)
        self.assertNotIn(".write(", transaction)
        self.assertNotIn(".flush(", transaction)
        self.assertNotIn("soft_reset(", transaction)
        self.assertNotIn(".run(", transaction)
        self.assertIn("capture_module.capture_open_handle", transaction)
        self.assertIn("runtime.launch_reset_class(device, uses_usb=False).reset()", transaction)


def stat_mode(path: pathlib.Path) -> int:
    return path.stat().st_mode & 0o777


if __name__ == "__main__":
    unittest.main()
