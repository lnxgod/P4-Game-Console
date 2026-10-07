#!/usr/bin/env python3
"""Keep exact-unit and artifact guards when installing an SDK-free release."""

import builtins
import contextlib
import copy
import importlib.machinery
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("tab5_prebuilt_installer", ROOT / "scripts/flash-console-os-tab5.py")
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)
COMMIT = "a" * 40
MANIFEST_SHA = "b" * 64
FEATURES = {"usb_host_enabled": True, "charger_control_enabled": False,
            "ble_multiplayer_enabled": True, "wifi_multiplayer_enabled": False}


def image(payload=b"fixture"):
    data = bytearray(32)
    data[0] = 0xe9
    struct.pack_into("<H", data, 12, 18)
    struct.pack_into("<HH", data, 15, 100, 199)
    return bytes(data) + payload


class FetchLoader:
    def __init__(self, source_commit):
        self.source_commit = source_commit

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        module.source_commit = self.source_commit


class PrebuiltInstallGuards(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="p4-prebuilt-install-")
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name) / "repository"
        self.release = Path(self.temporary.name) / "prebuilt-release"
        self.build = self.release / "apps/console_os/build-tab5"
        self.previous = self.repo / "apps/console_os/previous-build/p4_console_os.bin"
        self.previous.parent.mkdir(parents=True)
        self.previous.write_bytes(image(b"previous"))
        self.identity = INSTALLER.sha(b"010203040506")
        self.original_artifacts = {
            "0x2000": image(b"bootloader"), "0x8000": b"partition fixture",
            "0x10000": b"OTA fixture", "0x20000": image(b"application")}
        self.auth = {
            "board": "m5stack-tab5", "install_authorized": True,
            "operation": "first-console-os-layout", "prebuilt_manifest_sha256": MANIFEST_SHA,
            **FEATURES,
            "units": {"A": {"model_confirmed": True, "identity_sha256": self.identity,
                              "predecessor": {"file": "apps/console_os/previous-build/p4_console_os.bin",
                                              "bytes": self.previous.stat().st_size,
                                              "sha256": INSTALLER.sha(self.previous.read_bytes())}}},
            "artifacts": {},
        }
        for offset, data in self.original_artifacts.items():
            self.write_artifact(offset, data)
        self.source_commit = mock.Mock(return_value=COMMIT)
        self.validate_release = mock.Mock(return_value={
            "manifest_sha256": MANIFEST_SHA,
            "verification": {**FEATURES, "hardware_verified": False, "flash_authorized": False},
            "build_dir": str(self.build)})
        root_patch = mock.patch.object(INSTALLER, "ROOT", self.repo)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        build_patch = mock.patch.object(INSTALLER, "BUILD", self.repo / "apps/console_os/build-tab5")
        build_patch.start()
        self.addCleanup(build_patch.stop)
        module_patch = mock.patch.dict(sys.modules, {
            "p4_prebuilt": types.SimpleNamespace(validate_release=self.validate_release)})
        module_patch.start()
        self.addCleanup(module_patch.stop)
        real_spec = importlib.util.spec_from_file_location

        def resolve_spec(name, path, *args, **kwargs):
            if Path(path) == self.repo / "scripts/fetch-prebuilt.py":
                return importlib.machinery.ModuleSpec(name, FetchLoader(self.source_commit))
            if Path(path) == self.repo / "scripts/verify-console-os-tab5.py":
                raise AssertionError("SDK/ELF verifier must not load for prebuilt installation")
            return real_spec(name, path, *args, **kwargs)

        spec_patch = mock.patch.object(INSTALLER.importlib.util, "spec_from_file_location", side_effect=resolve_spec)
        spec_patch.start()
        self.addCleanup(spec_patch.stop)
        original_path = list(sys.path)
        self.addCleanup(lambda: sys.path.__setitem__(slice(None), original_path))

    def save_backup_manifest(self, records):
        path = self.repo / "hardware/backups/manifest.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps({"additional_devices": records}))

    @contextlib.contextmanager
    def no_backup_reads(self):
        read_bytes = Path.read_bytes
        read_text = Path.read_text
        backup_root = self.repo / "hardware/backups"

        def check(path):
            self.assertFalse(path == backup_root / "manifest.json" or path.parent == backup_root,
                             f"Firmware backup input must not be read: {path}")

        def bytes_without_backup(path):
            check(path)
            return read_bytes(path)

        def text_without_backup(path, *args, **kwargs):
            check(path)
            return read_text(path, *args, **kwargs)

        with mock.patch.object(Path, "read_bytes", bytes_without_backup), \
             mock.patch.object(Path, "read_text", text_without_backup):
            yield

    def write_artifact(self, offset, data):
        path = self.build / INSTALLER.LAYOUT[offset]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.auth["artifacts"][offset] = {
            "file": str(Path("apps/console_os/build-tab5") / INSTALLER.LAYOUT[offset]),
            "bytes": len(data), "sha256": INSTALLER.sha(data)}

    @contextlib.contextmanager
    def no_device_or_write(self):
        imported = []
        real_import = builtins.__import__

        def guarded_import(name, *args, **kwargs):
            if name.split(".")[0] in ("esptool", "serial"):
                imported.append(name)
                raise AssertionError("Device tools must not load during local validation")
            return real_import(name, *args, **kwargs)

        with mock.patch("builtins.__import__", side_effect=guarded_import), \
             mock.patch.object(Path, "write_bytes", side_effect=AssertionError("No writes allowed")) as write_bytes, \
             mock.patch.object(Path, "write_text", side_effect=AssertionError("No writes allowed")) as write_text, \
             mock.patch.object(INSTALLER.tempfile, "mkdtemp", side_effect=AssertionError("No install run allowed")) as stage:
            yield
            self.assertEqual(imported, [])
            write_bytes.assert_not_called()
            write_text.assert_not_called()
            stage.assert_not_called()

    def prepare(self, auth=None):
        with self.no_device_or_write():
            return INSTALLER.prepare(self.auth if auth is None else auth, "A", self.release)

    def reject(self, auth, message):
        with self.no_device_or_write(), self.assertRaisesRegex(ValueError, message):
            INSTALLER.prepare(auth, "A", self.release)

    def test_valid_release_uses_portable_checks_without_sdk_or_writes(self):
        identity, artifacts, predecessor = self.prepare()
        self.assertEqual(identity, self.identity)
        self.assertEqual(artifacts, self.original_artifacts)
        self.assertIsNone(predecessor)
        self.assertFalse((self.repo / "hardware/backups").exists())
        self.assertFalse(INSTALLER.BUILD.exists())
        self.source_commit.assert_called_once_with(self.repo)
        self.validate_release.assert_called_once_with(
            self.release, expected_source_commit=COMMIT, source_root=self.repo)

    def validate_cli(self, unit):
        authorization = self.repo / "authorization.json"
        raw = json.dumps(self.auth).encode()
        authorization.write_bytes(raw)
        output = io.StringIO()
        args = ["flash-console-os-tab5.py", "--authorization", str(authorization),
                "--authorization-sha256", INSTALLER.sha(raw), "--unit", unit,
                "--prebuilt", str(self.release)]
        with mock.patch.object(sys, "argv", args), self.no_device_or_write(), \
             self.no_backup_reads(), contextlib.redirect_stdout(output):
            INSTALLER.main()
        return output.getvalue()

    def test_cli_supports_exact_authorized_unit_c_app_update_without_backups(self):
        self.auth["units"]["C"] = self.auth["units"].pop("A")
        self.auth["operation"] = "app-only"
        self.assertIn("Tab5 C: authorized artifacts verified", self.validate_cli("C"))
        self.validate_release.assert_called_once_with(
            self.release, expected_source_commit=COMMIT, source_root=self.repo)
        self.assertFalse((self.repo / "hardware/backups").exists())

    def test_cli_unit_c_still_requires_confirmed_model(self):
        self.auth["units"]["C"] = self.auth["units"].pop("A")
        self.auth["units"]["C"]["model_confirmed"] = False
        with self.assertRaisesRegex(ValueError, "model unconfirmed"):
            self.validate_cli("C")
        self.validate_release.assert_not_called()

    def test_requires_exact_manifest_authorization(self):
        for value in (None, "c" * 64, ""):
            with self.subTest(value=value):
                auth = copy.deepcopy(self.auth)
                if value is None:
                    auth.pop("prebuilt_manifest_sha256")
                else:
                    auth["prebuilt_manifest_sha256"] = value
                self.reject(auth, "prebuilt manifest differs")

    def test_requires_tab5_scope_model_and_hashed_unit_authorization(self):
        for key, value, message in (("board", "elecrow-10in", "Tab5 installation authorization"),
                                    ("install_authorized", False, "Tab5 installation authorization"),
                                    ("operation", "all-flash", "unsupported installation scope")):
            with self.subTest(key=key):
                auth = copy.deepcopy(self.auth)
                auth[key] = value
                self.reject(auth, message)
        for key, value, message in (("model_confirmed", False, "model unconfirmed"),
                                    ("identity_sha256", "raw-device-id", "invalid identity binding")):
            with self.subTest(key=key, value=value):
                auth = copy.deepcopy(self.auth)
                auth["units"]["A"][key] = value
                self.reject(auth, message)
        self.validate_release.assert_not_called()

    def test_requires_all_exact_feature_selections(self):
        for feature, enabled in FEATURES.items():
            with self.subTest(feature=feature):
                auth = copy.deepcopy(self.auth)
                auth[feature] = not enabled
                self.reject(auth, "selection differs")

    def test_requires_four_canonical_authorized_paths(self):
        for offset in INSTALLER.LAYOUT:
            with self.subTest(offset=offset):
                auth = copy.deepcopy(self.auth)
                auth["artifacts"][offset]["file"] = str(self.build / INSTALLER.LAYOUT[offset])
                self.reject(auth, "artifact path differs")
        for offset in ("0x2000", "0x30000"):
            with self.subTest(range=offset):
                auth = copy.deepcopy(self.auth)
                if offset == "0x2000":
                    auth["artifacts"].pop(offset)
                else:
                    auth["artifacts"][offset] = copy.deepcopy(auth["artifacts"]["0x20000"])
                self.reject(auth, "unexpected write ranges")

    def test_requires_matching_hash_and_size_for_every_image(self):
        for offset in INSTALLER.LAYOUT:
            for field, value in (("sha256", "d" * 64), ("bytes", 0)):
                with self.subTest(offset=offset, field=field):
                    auth = copy.deepcopy(self.auth)
                    auth["artifacts"][offset][field] = value
                    self.reject(auth, "artifact hash or size differs")

    def test_rejects_empty_or_oversized_flash_ranges(self):
        for offset, limit in INSTALLER.LIMITS.items():
            for data in (b"", bytes(limit + 1)):
                with self.subTest(offset=offset, size=len(data)):
                    self.write_artifact(offset, data)
                    self.reject(self.auth, "artifact crosses allowed flash range")
            self.write_artifact(offset, self.original_artifacts[offset])

    def test_rejects_wrong_bootloader_and_application_image_family(self):
        for offset in ("0x2000", "0x20000"):
            for label, position, value in (("magic", 0, 0), ("soc", 12, 9), ("revision", 15, 300)):
                with self.subTest(offset=offset, label=label):
                    data = bytearray(self.original_artifacts[offset])
                    if label == "magic":
                        data[position] = value
                    else:
                        struct.pack_into("<H", data, position, value)
                    self.write_artifact(offset, bytes(data))
                    self.reject(self.auth, "invalid ESP image|wrong SoC|wrong silicon")
            self.write_artifact(offset, self.original_artifacts[offset])

    def test_both_scopes_prepare_without_backup_records_or_snapshot_files(self):
        for operation in ("first-console-os-layout", "app-only"):
            with self.subTest(operation=operation):
                auth = copy.deepcopy(self.auth)
                auth["operation"] = operation
                with self.no_backup_reads():
                    identity, artifacts, predecessor = self.prepare(auth)
                self.assertEqual(identity, self.identity)
                self.assertEqual(artifacts, self.original_artifacts)
                self.assertEqual(predecessor, self.previous.read_bytes() if operation == "app-only" else None)
        self.assertFalse((self.repo / "hardware/backups").exists())

    def test_stale_or_corrupt_backup_inputs_are_ignored_and_preserved(self):
        backup_root = self.repo / "hardware/backups"
        backup = backup_root / "factory.bin"
        manifest = backup_root / "manifest.json"
        record = {"device": {"identity": {"sha256": "0" * 64}},
                  "backup": {"file": "hardware/backups/factory.bin",
                             "bytes": 16 * 1024 * 1024, "sha256": "1" * 64}}
        for state in ("missing-snapshot", "stale-binding", "corrupt-snapshot", "corrupt-manifest"):
            self.save_backup_manifest([record, record])
            backup.unlink(missing_ok=True)
            if state != "missing-snapshot":
                backup.write_bytes(b"corrupt old snapshot" if state == "corrupt-snapshot" else b"old firmware")
            if state == "corrupt-manifest":
                manifest.write_text("invalid old manifest")
            saved_manifest = manifest.read_bytes()
            saved_backup = backup.read_bytes() if backup.exists() else None
            for operation in ("first-console-os-layout", "app-only"):
                with self.subTest(state=state, operation=operation):
                    auth = copy.deepcopy(self.auth)
                    auth["operation"] = operation
                    auth["backup_manifest"] = {"file": "hardware/backups/manifest.json", "sha256": "2" * 64}
                    auth["units"]["A"]["backup_sha256"] = "3" * 64
                    with self.no_backup_reads():
                        identity, artifacts, _ = self.prepare(auth)
                    self.assertEqual(identity, self.identity)
                    self.assertEqual(artifacts, self.original_artifacts)
                    self.assertEqual(manifest.read_bytes(), saved_manifest)
                    self.assertEqual(backup.read_bytes() if backup.exists() else None, saved_backup)

    def test_exact_authorized_identity_does_not_need_a_backup_registry_entry(self):
        auth = copy.deepcopy(self.auth)
        auth["units"]["A"]["identity_sha256"] = "0" * 64
        with self.no_backup_reads():
            self.assertEqual(self.prepare(auth)[0], "0" * 64)

    @contextlib.contextmanager
    def mocked_install(self, operation, verification="device-checksum"):
        auth = copy.deepcopy(self.auth)
        auth["operation"] = operation
        authorization = self.repo / "authorization.json"
        raw = json.dumps(auth).encode()
        authorization.write_bytes(raw)
        ota = bytearray(b"\xff" * 8192)
        struct.pack_into("<I20xII", ota, 0, 1, 2,
                         INSTALLER.binascii.crc32(struct.pack("<I", 1), 0xffffffff))
        flash_contents = {int(offset, 0): data for offset, data in self.original_artifacts.items()}
        flash_contents[0x10000] = bytes(ota)
        flash_contents[0x20000] = self.previous.read_bytes()

        def read_flash(offset, count):
            self.assertIn(offset, flash_contents, "Unexpected live firmware snapshot read")
            self.assertLessEqual(count, len(flash_contents[offset]), "Unexpected flash read length")
            return flash_contents[offset][:count]

        def flash_md5sum(offset, count):
            self.assertIn(offset, flash_contents)
            self.assertLessEqual(count, len(flash_contents[offset]))
            return INSTALLER.hashlib.md5(flash_contents[offset][:count]).hexdigest()

        device = mock.Mock(CHIP_NAME="ESP32-P4", secure_download_mode=False)
        device.read_mac.return_value = (1, 2, 3, 4, 5, 6)
        device.get_major_chip_version.return_value = 1
        device.get_minor_chip_version.return_value = 3
        device.flash_id.return_value = 0x184046
        device.get_secure_boot_enabled.return_value = False
        device.get_flash_encryption_enabled.return_value = False
        device.run_stub.return_value = device
        device.read_flash.side_effect = read_flash
        device.flash_md5sum.side_effect = flash_md5sum
        written = {}

        def write_flash(command, esp):
            self.assertIs(esp, device)
            pairs = command[command.index("--flash_size") + 2:]
            for offset, path in zip(pairs[::2], pairs[1::2]):
                data = Path(path).read_bytes()
                written[offset] = data
                flash_contents[int(offset, 0)] = data

        esptool = types.SimpleNamespace(__version__="4.12.0",
                                        detect_chip=mock.Mock(return_value=device),
                                        main=mock.Mock(side_effect=write_flash))
        serial_port = mock.Mock()
        serial_port.read.return_value = (b"P4_CONSOLE_OS READY board=m5stack-tab5\n"
                                        b"OTA_BOOT_VALID result=ESP_OK\n")
        serial = types.SimpleNamespace(Serial=mock.Mock(return_value=serial_port),
                                       SerialException=type("TestSerialException", (Exception,), {}))
        args = ["installer", "--authorization", str(authorization), "--authorization-sha256",
                INSTALLER.sha(raw), "--unit", "A", "--prebuilt", str(self.release),
                "--install", "--port", "/dev/null", "--monitor-seconds", "30",
                "--verification", verification]
        with mock.patch.object(sys, "argv", args), \
             mock.patch.dict(sys.modules, {"esptool": esptool, "serial": serial}), \
             mock.patch.object(INSTALLER.time, "monotonic", side_effect=(0, 1, 2, 31, 31)), \
             contextlib.redirect_stdout(io.StringIO()), self.no_backup_reads():
            yield device, written

    def install_receipt(self):
        runs = list((self.repo / "hardware/backups").glob("tab5-a-install-*"))
        self.assertEqual(len(runs), 1)
        run = runs[0]
        receipt = json.loads((run / "receipt.json").read_text())
        self.assertFalse(receipt["firmware_backup_created"])
        staged = {path.stem: path.read_bytes() for path in run.glob("*.bin")}
        self.assertEqual(staged, self.original_artifacts)
        self.assertFalse((self.repo / "hardware/backups/manifest.json").exists())
        return receipt

    def test_first_layout_install_needs_no_backup_directory_or_snapshot_reads(self):
        self.assertFalse((self.repo / "hardware/backups").exists())
        with self.mocked_install("first-console-os-layout") as (device, written):
            INSTALLER.main()
            device.read_flash.assert_not_called()
            self.assertEqual(written, self.original_artifacts)
            self.assertEqual(device.flash_md5sum.call_args_list,
                             [mock.call(int(offset, 0), len(data))
                              for offset, data in self.original_artifacts.items()])
        receipt = self.install_receipt()
        self.assertTrue(receipt["boot_ready"])
        self.assertEqual(receipt["verification_method"], "device-checksum")
        self.assertFalse(receipt["readback_verified"])

    def test_app_only_install_reads_layout_metadata_and_checks_build_predecessor(self):
        with self.mocked_install("app-only") as (device, written):
            INSTALLER.main()
            self.assertEqual(written, {"0x20000": self.original_artifacts["0x20000"]})
            self.assertEqual(device.read_flash.call_args_list,
                             [mock.call(0x2000, len(self.original_artifacts["0x2000"])),
                              mock.call(0x8000, len(self.original_artifacts["0x8000"])),
                              mock.call(0x10000, 8192)])
            self.assertEqual(device.flash_md5sum.call_args_list,
                             [mock.call(0x20000, self.previous.stat().st_size),
                              mock.call(0x20000, len(self.original_artifacts["0x20000"]))])
        self.assertEqual(self.install_receipt()["write_offsets"], ["0x20000"])

    def test_explicit_full_readback_verifies_only_written_ranges_without_snapshot_capture(self):
        with self.mocked_install("first-console-os-layout", "full-readback") as (device, written):
            INSTALLER.main()
            self.assertEqual(written, self.original_artifacts)
            self.assertEqual(device.read_flash.call_args_list,
                             [mock.call(int(offset, 0), len(data))
                              for offset, data in self.original_artifacts.items()])
            device.flash_md5sum.assert_not_called()
        self.assertTrue(self.install_receipt()["readback_verified"])

    def test_backup_absence_does_not_disable_live_exact_unit_checks(self):
        for operation in ("first-console-os-layout", "app-only"):
            with self.subTest(operation=operation), self.mocked_install(operation) as (device, written):
                device.read_mac.return_value = (9, 2, 3, 4, 5, 6)
                with self.assertRaisesRegex(ValueError, "live identity differs"):
                    INSTALLER.main()
                self.assertEqual(written, {})
                device.read_flash.assert_not_called()

    def test_app_only_still_rejects_a_live_app_that_differs_from_build_predecessor(self):
        with self.mocked_install("app-only") as (device, written):
            device.flash_md5sum.side_effect = None
            device.flash_md5sum.return_value = "0" * 32
            with self.assertRaisesRegex(ValueError, "installed app differs from authorized predecessor"):
                INSTALLER.main()
            self.assertEqual(written, {})

    def test_app_only_requires_hash_bound_valid_predecessor(self):
        auth = copy.deepcopy(self.auth)
        auth["operation"] = "app-only"
        result = self.prepare(auth)
        self.assertEqual(result[2], self.previous.read_bytes())
        for field, value in (("bytes", 1), ("sha256", "f" * 64)):
            with self.subTest(field=field):
                changed = copy.deepcopy(auth)
                changed["units"]["A"]["predecessor"][field] = value
                self.reject(changed, "predecessor artifact changed")
        wrong_image = bytearray(self.previous.read_bytes())
        struct.pack_into("<H", wrong_image, 12, 9)
        self.previous.write_bytes(wrong_image)
        auth["units"]["A"]["predecessor"]["sha256"] = INSTALLER.sha(wrong_image)
        self.reject(auth, "wrong SoC")

    def test_prebuilt_firmware_only_rejected_for_both_scopes(self):
        for operation in ("first-console-os-layout", "app-only"):
            with self.subTest(operation=operation):
                auth = copy.deepcopy(self.auth)
                auth["operation"] = operation
                auth["firmware_only"] = True
                self.reject(auth, "firmware-only requires app-only|complete standard bundle")

    def test_portable_revision_rejection_propagates_without_sdk_fallback(self):
        self.validate_release.side_effect = ValueError("source commit differs from exact release")
        self.reject(self.auth, "source commit differs")
        self.validate_release.assert_called_once_with(
            self.release, expected_source_commit=COMMIT, source_root=self.repo)

    def test_install_flag_cannot_open_device_or_write_on_local_rejection(self):
        for label in ("manifest", "identity", "artifact", "feature", "predecessor"):
            with self.subTest(label=label):
                auth = copy.deepcopy(self.auth)
                if label == "manifest":
                    auth.pop("prebuilt_manifest_sha256")
                elif label == "identity":
                    auth["units"]["A"]["identity_sha256"] = "invalid binding"
                elif label == "artifact":
                    auth["artifacts"]["0x20000"]["sha256"] = "0" * 64
                elif label == "feature":
                    auth["usb_host_enabled"] = False
                else:
                    auth["operation"] = "app-only"
                    auth["units"]["A"]["predecessor"]["sha256"] = "0" * 64
                authorization = self.repo / "authorization.json"
                raw = json.dumps(auth).encode()
                authorization.write_bytes(raw)
                args = ["installer", "--authorization", str(authorization), "--authorization-sha256",
                        INSTALLER.sha(raw), "--unit", "A", "--prebuilt", str(self.release),
                        "--install", "--port", "/dev/null"]
                with mock.patch.object(sys, "argv", args), self.no_device_or_write(), \
                     self.assertRaises(ValueError):
                    INSTALLER.main()


if __name__ == "__main__":
    unittest.main()
