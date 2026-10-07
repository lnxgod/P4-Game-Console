#!/usr/bin/env python3
"""Keep exact-unit and artifact guards when installing an SDK-free release."""

import builtins
import contextlib
import copy
import importlib.machinery
import importlib.util
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
BACKUP_BYTES = bytes([0xff]) * (16 * 1024 * 1024)
BACKUP_SHA = INSTALLER.sha(BACKUP_BYTES)
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
        self.backup = self.repo / "hardware/backups/factory.bin"
        self.backup.parent.mkdir(parents=True)
        self.backup.write_bytes(BACKUP_BYTES)
        self.previous = self.repo / "hardware/backups/previous-app.bin"
        self.previous.write_bytes(image(b"previous"))
        self.identity = INSTALLER.sha(b"010203040506")
        self.record = {"device": {"identity": {"sha256": self.identity}},
                       "backup": {"file": "hardware/backups/factory.bin",
                                  "bytes": len(BACKUP_BYTES), "sha256": BACKUP_SHA}}
        self.save_backup_manifest([self.record])
        self.original_artifacts = {
            "0x2000": image(b"bootloader"), "0x8000": b"partition fixture",
            "0x10000": b"OTA fixture", "0x20000": image(b"application")}
        self.auth = {
            "board": "m5stack-tab5", "install_authorized": True,
            "operation": "first-console-os-layout", "prebuilt_manifest_sha256": MANIFEST_SHA,
            **FEATURES,
            "units": {"A": {"model_confirmed": True, "identity_sha256": self.identity,
                              "backup_sha256": BACKUP_SHA,
                              "predecessor": {"file": "hardware/backups/previous-app.bin",
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
        (self.repo / "hardware/backups/manifest.json").write_text(
            json.dumps({"additional_devices": records}))

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
        identity, backup, artifacts, predecessor = self.prepare()
        self.assertEqual(identity, self.identity)
        self.assertEqual(INSTALLER.sha(backup), BACKUP_SHA)
        self.assertEqual(artifacts, self.original_artifacts)
        self.assertIsNone(predecessor)
        self.assertFalse(INSTALLER.BUILD.exists())
        self.source_commit.assert_called_once_with(self.repo)
        self.validate_release.assert_called_once_with(
            self.release, expected_source_commit=COMMIT, source_root=self.repo)

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
                                    ("identity_sha256", "raw-device-id", "invalid identity binding"),
                                    ("identity_sha256", "0" * 64, "exactly one full backup")):
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

    def test_complete_factory_backup_and_unique_unit_binding_still_required(self):
        self.save_backup_manifest([self.record, self.record])
        self.reject(self.auth, "exactly one full backup")
        self.save_backup_manifest([])
        self.reject(self.auth, "exactly one full backup")
        wrong_size = copy.deepcopy(self.record)
        wrong_size["backup"]["bytes"] = len(BACKUP_BYTES) - 1
        self.save_backup_manifest([wrong_size])
        self.reject(self.auth, "authorization and backup manifest differ")
        self.save_backup_manifest([self.record])
        auth = copy.deepcopy(self.auth)
        auth["units"]["A"]["backup_sha256"] = "e" * 64
        self.reject(auth, "authorization and backup manifest differ")
        self.backup.write_bytes(BACKUP_BYTES[:-1])
        self.reject(self.auth, "backup corrupt or incomplete")
        self.backup.write_bytes(bytes([0]) * len(BACKUP_BYTES))
        self.reject(self.auth, "backup corrupt or incomplete")
        self.validate_release.assert_not_called()

    def test_app_only_requires_hash_bound_valid_predecessor(self):
        auth = copy.deepcopy(self.auth)
        auth["operation"] = "app-only"
        result = self.prepare(auth)
        self.assertEqual(result[3], self.previous.read_bytes())
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
        for label in ("manifest", "backup", "artifact", "feature", "predecessor"):
            with self.subTest(label=label):
                auth = copy.deepcopy(self.auth)
                if label == "manifest":
                    auth.pop("prebuilt_manifest_sha256")
                elif label == "backup":
                    auth["units"]["A"]["backup_sha256"] = "0" * 64
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
