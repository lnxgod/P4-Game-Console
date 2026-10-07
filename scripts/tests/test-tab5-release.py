#!/usr/bin/env python3
"""Hermetic portable release tests; no SDK, downloads or hardware access."""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import p4_prebuilt as portable

spec = importlib.util.spec_from_file_location("tab5_export", SCRIPTS / "package-tab5-release.py")
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


def write(root, name, data):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def json_bytes(value):
    return (json.dumps(value, sort_keys=True) + "\n").encode()


def image():
    data = bytearray(96)
    data[0] = 0xe9
    struct.pack_into("<H", data, 12, 18)
    struct.pack_into("<HH", data, 15, 100, 199)
    return bytes(data)


def cartridge(game):
    payload = b"\x7fELFsynthetic-native-cartridge"
    header = bytearray(256)
    header[:8] = b"P4GAME1\0"
    struct.pack_into("<7I", header, 8, 256, 256+len(payload), 256, len(payload),
                     1, 1, game["launcher_id"])
    struct.pack_into("<I", header, 36, 1)
    header[224:227] = b"MIT"
    header[48:80] = hashlib.sha256(payload).digest()
    for pos, key in ((80, "id"), (128, "title"), (176, "folder"), (208, "version")):
        text = game[key].encode()
        header[pos:pos+len(text)] = text
    return bytes(header) + payload


def resource(game):
    payload = b"synthetic-artwork"
    header = bytearray(128)
    header[:8] = b"P4RES01\0"
    struct.pack_into("<6I", header, 8, 128, 128+len(payload), 128, len(payload), 1, 0)
    header[32:64] = hashlib.sha256(payload).digest()
    text = game["id"].encode()
    header[64:64+len(text)] = text
    return bytes(header) + payload


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.root, self.build = self.base / "source", self.base / "build"
        self.root.mkdir()
        for name in portable.SOURCE_FIXED:
            write(self.root, name, b"reviewed source\n")
        write(self.root, "third_party/tab5-bsp.json", json_bytes({"files": []}))
        write(self.root, "games/retired.json", json_bytes({"games": []}))
        write(self.root, "apps/console_os/app-metadata.json",
              json_bytes({"version": "0.59", "game_data_embedded": False}))
        self.game = {"schema": 1, "enabled": True, "id": "org.p4console.good",
                     "title": "GOOD GAME", "folder": "GAMES", "version": "1.0.0",
                     "launcher_id": 100, "package_file": "GOOD.P4G", "resource_file": "GOOD.P4R"}
        write(self.root, "games/good/game.json", json_bytes(self.game))
        write(self.root, "games/good/README.md", b"Presentation and physical acceptance pending\n")
        write(self.root, "games/good/src/art.inc", b"0x00, 0x01\n")
        write(self.root, "games/good/assets/launcher.p4i", b"synthetic packed icon")
        write(self.root, "apps/doom_embedded_touch_audio/main/runtime_gate.c", b"reviewed shared Doom source\n")
        write(self.root, "apps/console_os/main/assets/gamechangers_ai_logo.rgb565", b"packed RGB565 logo")
        write(self.root, "apps/console_os/main/assets/gamechangers_mark_flight.rgb565a8", b"packed RGB565 alpha artwork")
        dev = dict(self.game, enabled=False, id="org.p4console.dev", launcher_id=101,
                   package_file="DEV.P4G", resource_file=None, folder="GAMES/WIP")
        write(self.root, "games/dev/game.json", json_bytes(dev))
        write(self.root, "scripts/verify-console-os-tab5.py", b'''
import json
def verify(build, firmware_only=False):
    if firmware_only:
        raise ValueError("export must run full verifier")
    return json.loads((build / "verified.json").read_text())
''')
        for name in portable.FLASH_LAYOUT.values():
            data = image()
            if name.endswith("partition-table.bin"):
                data = bytearray(64)
                for pos, subtype, offset in ((0, 0x10, 0x20000), (32, 0x11, 0x810000)):
                    struct.pack_into("<HBBII", data, pos, 0x50aa, 0, subtype, offset, 0x7f0000)
                data = bytes(data)
            if name == "ota_data_initial.bin":
                data = bytes([0xff]) * 8192
            write(self.build, name, data)
        app = image()
        update = bytearray(256)
        update[:8] = b"P4OSUP1\0"
        struct.pack_into("<6I", update, 8, 256, 256+len(app), 256, len(app), 1, 0)
        update[32:64] = hashlib.sha256(app).digest()
        update[64:68] = b"0.59"
        update[160:172] = b"esp32p4-tab5"
        write(self.build, "P4UPDATE.P4U", bytes(update) + app)
        write(self.build, "sd-card/GAMES/GOOD.P4G", cartridge(self.game))
        write(self.build, "sd-card/GAMES/GOOD.P4R", resource(self.game))
        write(self.build, "sd-card/DOOM1.WAD", b"PRIVATE-WAD-MUST-NOT-BE-EXPORTED")
        write(self.build, "factory-backup.bin", b"PRIVATE-RECOVERY")
        write(self.build, "credentials.json", b"PRIVATE-CREDENTIALS")
        write(self.root, "hardware/boards/m5stack-tab5/board-profile.json", b"PRIVATE-DEVICE-IDENTITY")
        self.verified = {"result": "tab5-build-candidate-verified", "board": "m5stack-tab5", "version": "0.59",
                         "image_bytes": len(app), "image_sha256": portable.sha256(app),
                         "native_cartridges": 1, "content_bundle_verified": True,
                         "hardware_verified": False, "flash_authorized": False,
                         **{name: False for name in portable.FEATURES}}
        write(self.build, "verified.json", json_bytes(self.verified))
        write(self.build, "sdkconfig", b"synthetic sdkconfig")
        write(self.build, "compile_commands.json", b"[]")
        write(self.build, "project_description.json", json_bytes({"app_elf": "p4_console_os.elf"}))
        write(self.build, "p4_console_os.elf", b"synthetic ELF full-verifier evidence")
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Test", "-c", "user.email=test@invalid",
                        "commit", "-qm", "hermetic source"], check=True)
        self.commit = subprocess.check_output(["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        update[96:108] = self.commit[:12].encode("ascii")
        write(self.build, "P4UPDATE.P4U", bytes(update) + app)
        self.report = exporter.export_release(self.root, self.build, self.base / "output", self.commit)
        self.archive = Path(self.report["archive"])
        self.directory = self.base / "release"
        portable.extract_release(self.archive, self.directory, expected_sha256=self.report["archive_sha256"],
                                 expected_source_commit=self.commit, source_root=self.root)

    def tearDown(self):
        self.temporary.cleanup()

    def manifest(self):
        return portable.load_json((self.directory / "manifest.json").read_bytes())

    def replace_manifest(self, value):
        (self.directory / "manifest.json").write_bytes(exporter.canonical_json(value))

    def rebind(self, name, data):
        write(self.directory, name, data)
        value = self.manifest()
        value["files"][name]["bytes"] = len(data)
        value["files"][name]["sha256"] = portable.sha256(data)
        self.replace_manifest(value)

    def check_bad_archive(self, members):
        archive = self.base / "bad.tar.gz"
        with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as tar:
            for member, data in members:
                tar.addfile(member, io.BytesIO(data) if data is not None else None)
        destination = self.base / "bad-release"
        with self.assertRaises((portable.ReleaseError, OSError)):
            portable.extract_release(archive, destination, expected_sha256=portable.file_sha256(archive),
                                     expected_source_commit=self.commit)
        self.assertFalse(destination.exists())
        self.assertFalse(list(self.base.glob(".tab5-release-*")))

    def test_allowlist_deterministic_and_build_only(self):
        with tarfile.open(self.archive) as tar:
            names = tar.getnames()
            self.assertEqual(set(names), {"manifest.json", *portable.expected_files(portable.standard_games(self.root))})
            self.assertTrue(all(member.isfile() for member in tar.getmembers()))
            raw = b"".join(tar.extractfile(member).read() for member in tar.getmembers())
        for secret in (b"PRIVATE-WAD", b"PRIVATE-RECOVERY", b"PRIVATE-CREDENTIALS", b"PRIVATE-DEVICE-IDENTITY"):
            self.assertNotIn(secret, raw)
        manifest = self.manifest()
        self.assertFalse(manifest["hardware_verified"])
        self.assertFalse(manifest["flash_authorized"])
        self.assertFalse(manifest["firmware_only"])
        self.assertTrue(manifest["export_verification"]["content_bundle_verified"])
        self.assertIn("games/good/README.md", manifest["source_files"])
        self.assertIn("games/good/src/art.inc", manifest["source_files"])
        self.assertIn("games/good/assets/launcher.p4i", manifest["source_files"])
        self.assertIn("apps/doom_embedded_touch_audio/main/runtime_gate.c", manifest["source_files"])
        self.assertIn("apps/console_os/main/assets/gamechangers_ai_logo.rgb565", manifest["source_files"])
        self.assertIn("apps/console_os/main/assets/gamechangers_mark_flight.rgb565a8", manifest["source_files"])
        self.assertIn("scripts/build-os-update.py", manifest["source_files"])
        second = exporter.export_release(self.root, self.build, self.base / "second", self.commit)
        self.assertEqual(second["archive_sha256"], self.report["archive_sha256"])

    def test_full_verifier_failure_emits_nothing(self):
        output = self.base / "failure"
        with patch.object(exporter, "full_verify", side_effect=ValueError("ELF gate rejected")) as verifier:
            with self.assertRaisesRegex(ValueError, "ELF gate rejected"):
                exporter.export_release(self.root, self.build, output, self.commit)
        verifier.assert_called_once_with(self.root, self.build)
        self.assertFalse(output.exists())

    def test_publication_failure_rolls_back_and_can_retry(self):
        output = self.base / "publication-failure"
        original = exporter.os.link
        calls = 0
        def fail_second(source, destination):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("checksum publication failed")
            return original(source, destination)
        with patch.object(exporter.os, "link", fail_second):
            with self.assertRaisesRegex(OSError, "checksum publication failed"):
                exporter.export_release(self.root, self.build, output, self.commit)
        self.assertEqual(list(output.iterdir()), [])
        report = exporter.export_release(self.root, self.build, output, self.commit)
        self.assertEqual(report["archive_sha256"], self.report["archive_sha256"])

    def test_missing_resource_and_linked_inputs_rejected(self):
        sidecar = self.build / "sd-card/GAMES/GOOD.P4R"
        saved = sidecar.read_bytes()
        sidecar.unlink()
        with self.assertRaisesRegex(portable.ReleaseError, "required file missing"):
            exporter.export_release(self.root, self.build, self.base / "missing", self.commit)
        sidecar.symlink_to(write(self.base, "outside.P4R", saved))
        with self.assertRaisesRegex(portable.ReleaseError, "symlink"):
            exporter.export_release(self.root, self.build, self.base / "linked", self.commit)

    def test_dirty_source_and_wrong_commit_rejected(self):
        with self.assertRaisesRegex(portable.ReleaseError, "differs from source HEAD"):
            exporter.export_release(self.root, self.build, self.base / "wrong", "a" * 40)
        (self.root / "games/good/README.md").write_text("changed")
        with self.assertRaisesRegex(portable.ReleaseError, "tracked source differs"):
            exporter.export_release(self.root, self.build, self.base / "dirty", self.commit)

    def test_source_lite_without_git_or_parent_discovery(self):
        lite = self.root / "nested-lite"
        shutil.copytree(self.root, lite, ignore=shutil.ignore_patterns(".git", "nested-lite"))
        write(lite, ".p4-source.json", json_bytes({"schema": 1, "source_commit": self.commit}))
        result = portable.validate_release(self.directory, expected_source_commit=self.commit, source_root=lite)
        self.assertEqual(result["verification"], self.verified)
        write(lite, ".p4-source.json", json_bytes({"schema": 1, "source_commit": "b" * 40}))
        with self.assertRaisesRegex(portable.ReleaseError, "source archive commit differs"):
            portable.validate_release(self.directory, source_root=lite)

    def test_digest_pins_and_source_lock_binding(self):
        with self.assertRaisesRegex(portable.ReleaseError, "archive digest differs"):
            portable.extract_release(self.archive, self.base / "pin", expected_sha256="0" * 64)
        with self.assertRaisesRegex(portable.ReleaseError, "manifest digest differs"):
            portable.validate_release(self.directory, expected_manifest_sha256="0" * 64)
        with self.assertRaisesRegex(portable.ReleaseError, "source commit differs"):
            portable.validate_release(self.directory, expected_source_commit="0" * 40)
        for name in ("toolchain.lock.json", "scripts/build-os-update.py",
                     "apps/console_os/main/assets/gamechangers_ai_logo.rgb565",
                     "apps/console_os/main/assets/gamechangers_mark_flight.rgb565a8",
                     "apps/doom_embedded_touch_audio/main/runtime_gate.c"):
            source = self.root / name
            saved = source.read_bytes()
            source.write_bytes(b"changed reviewed input")
            with self.subTest(name=name), self.assertRaisesRegex(
                    portable.ReleaseError, "reviewed source/lock bindings differ"):
                portable.validate_release(self.directory, source_root=self.root)
            source.write_bytes(saved)

    def test_missing_extra_and_tampered_runtime_files(self):
        path = self.directory / "content/GAMES/GOOD.P4G"
        saved = path.read_bytes()
        path.write_bytes(saved[:-1] + b"x")
        with self.assertRaisesRegex(portable.ReleaseError, "file digest differs"):
            portable.validate_release(self.directory)
        path.unlink()
        with self.assertRaisesRegex(portable.ReleaseError, "missing or unexpected"):
            portable.validate_release(self.directory)
        path.write_bytes(saved)
        write(self.directory, "content/DOOM1.WAD", b"forbidden")
        with self.assertRaisesRegex(portable.ReleaseError, "missing or unexpected"):
            portable.validate_release(self.directory)

    def test_resource_identity_and_binary_geometry_even_after_rehash(self):
        resource_path = "content/GAMES/GOOD.P4R"
        data = bytearray((self.directory / resource_path).read_bytes())
        data[64] = ord("x")
        self.rebind(resource_path, bytes(data))
        with self.assertRaisesRegex(portable.ReleaseError, "resource sidecar identity"):
            portable.validate_release(self.directory)
        self.rebind(resource_path, resource(self.game))
        partition_path = "firmware/partition_table/partition-table.bin"
        data = bytearray((self.directory / partition_path).read_bytes())
        struct.pack_into("<I", data, 4, 0x30000)
        self.rebind(partition_path, bytes(data))
        with self.assertRaisesRegex(portable.ReleaseError, "binary OTA layout"):
            portable.validate_release(self.directory)

    def test_wrong_update_board_and_payload_after_rehash(self):
        name = "firmware/P4UPDATE.P4U"
        saved = (self.directory / name).read_bytes()
        data = bytearray(saved)
        data[160] = ord("x")
        self.rebind(name, bytes(data))
        with self.assertRaisesRegex(portable.ReleaseError, "update board"):
            portable.validate_release(self.directory)
        for marker in (bytes(64), b"0" * 12 + bytes(52)):
            data = bytearray(saved)
            data[96:160] = marker
            self.rebind(name, bytes(data))
            with self.subTest(marker=marker), self.assertRaisesRegex(portable.ReleaseError, "update build"):
                portable.validate_release(self.directory)
        data = bytearray(saved)
        data[-1] ^= 1
        self.rebind(name, bytes(data))
        with self.assertRaisesRegex(portable.ReleaseError, "does not bind"):
            portable.validate_release(self.directory)

    def test_cartridge_runtime_header_even_after_rehash(self):
        name = "content/GAMES/GOOD.P4G"
        saved = (self.directory / name).read_bytes()
        cases = ((36, bytes(4)), (40, struct.pack("<I", 1)),
                 (40, struct.pack("<I", 1 << 31)), (46, struct.pack("<H", 4)),
                 (224, bytes(16)), (240, b"x"))
        for offset, invalid in cases:
            data = bytearray(saved)
            data[offset:offset+len(invalid)] = invalid
            self.rebind(name, bytes(data))
            with self.subTest(offset=offset), self.assertRaises(portable.ReleaseError):
                portable.validate_release(self.directory)
        data = bytearray(saved)
        struct.pack_into("<IH", data, 40, 1 << 9, 0)
        struct.pack_into("<H", data, 46, portable.PROFILE_HEADER_FLAG)
        self.rebind(name, bytes(data))
        with self.assertRaisesRegex(portable.ReleaseError, "invalid cartridge multiplayer profile"):
            portable.validate_release(self.directory)

    def test_manifest_cannot_change_features_authority_or_library(self):
        original = self.manifest()
        cases = [("hardware_verified", True), ("flash_authorized", True), ("firmware_only", True),
                 ("wad_included", True), ("install_authorized", True)]
        for key, value in cases:
            manifest = copy.deepcopy(original)
            manifest[key] = value
            self.replace_manifest(manifest)
            with self.subTest(key=key), self.assertRaises(portable.ReleaseError):
                portable.validate_release(self.directory)
        manifest = copy.deepcopy(original)
        manifest["features"]["usb_host_enabled"] = True
        self.replace_manifest(manifest)
        with self.assertRaisesRegex(portable.ReleaseError, "verifier evidence differs"):
            portable.validate_release(self.directory)
        manifest = copy.deepcopy(original)
        manifest["games"][0]["folder"] = "GAMES/WIP"
        self.replace_manifest(manifest)
        with self.assertRaisesRegex(portable.ReleaseError, "development game"):
            portable.validate_release(self.directory)

    def test_archive_unsafe_duplicate_link_and_oversize_members(self):
        for name in ("../escape", "/absolute", "firmware/../escape", "content\\GAMES\\GOOD.P4G",
                     "content/DOOM1.WAD", "firmware/app.elf", "content/GAMES/DEV.P4CART"):
            member = tarfile.TarInfo(name)
            member.size = 1
            with self.subTest(name=name):
                self.check_bad_archive([(member, b"x")])
        member = tarfile.TarInfo("manifest.json")
        member.size = 2
        self.check_bad_archive([(member, b"{}"), (member, b"{}")])
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.DIRTYPE, tarfile.FIFOTYPE):
            member = tarfile.TarInfo("manifest.json")
            member.type = kind
            member.linkname = "../outside"
            self.check_bad_archive([(member, None)])
        member = tarfile.TarInfo("manifest.json")
        member.size = portable.MAX_MANIFEST_BYTES + 1
        self.check_bad_archive([(member, bytes(member.size))])

    def test_duplicate_json_keys_and_unknown_artifacts_rejected(self):
        (self.directory / "manifest.json").write_bytes(b'{"schema":1,"schema":1}')
        with self.assertRaisesRegex(portable.ReleaseError, "duplicate JSON key"):
            portable.validate_release(self.directory)
        value = portable.load_json((self.base / "output/release-manifest.json").read_bytes())
        value["files"]["content/GAMES/DEV.P4G"] = dict(value["files"]["content/GAMES/GOOD.P4G"])
        self.replace_manifest(value)
        with self.assertRaisesRegex(portable.ReleaseError, "runtime allowlist"):
            portable.validate_release(self.directory)


if __name__ == "__main__":
    unittest.main()
