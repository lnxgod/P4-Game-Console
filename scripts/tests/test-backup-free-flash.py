#!/usr/bin/env python3
"""Mocked flash routes and live-layout gates run with no recovery files."""

from __future__ import annotations

import contextlib
import copy
import importlib.util
import io
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


METADATA = module("flash_metadata", "scripts/verify-metadata.py")
LAYOUT = module("flash_layout", "scripts/verify-live-app-layout.py")
WAD = module("flash_wad", "scripts/verify-wad-provisioner.py")
DOOM = module("flash_doom", "scripts/verify-doom-embedded.py")
DOOM_AUDIO = module("flash_doom_audio", "scripts/verify-doom-embedded-audio.py")
AUDIO_LEVEL = module("flash_audio_level", "scripts/verify-audio-level-diag.py")


def table(entries):
    records = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype,
                                   offset, size, b"test", 0)
                       for kind, subtype, offset, size in entries)
    checksum = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(records).digest()
    return (records + checksum).ljust(4096, b"\xff")


class MetadataTests(unittest.TestCase):
    def test_normal_preflight_never_opens_backups_or_manifest(self):
        original_open = Path.open
        def no_backups(path, *args, **kwargs):
            self.assertNotIn("hardware/backups", str(path))
            return original_open(path, *args, **kwargs)
        with patch.object(Path, "open", no_backups), contextlib.redirect_stdout(io.StringIO()):
            METADATA.main(audit_backups=False)

    def test_preflight_still_rejects_unreviewed_pin_scope(self):
        original = METADATA.load
        def changed(path):
            value = original(path)
            if path == "hardware/board-profile.json":
                value = copy.deepcopy(value)
                value["pin_map_authorized"] = True
            return value
        with patch.object(METADATA, "load", side_effect=changed):
            with self.assertRaisesRegex(SystemExit, "pin map"):
                METADATA.main(audit_backups=False)


class LayoutTests(unittest.TestCase):
    def test_live_table_supplies_factory_range_without_snapshot(self):
        value = table([(1, 2, 0x9000, 0x6000), (0, 0, 0x10000, 0xB00000)])
        self.assertEqual(LAYOUT.factory_app(value, 16 * 1024 * 1024), (0x10000, 0xB00000))

    def test_live_table_rejects_corruption_overlap_and_out_of_bounds(self):
        good = table([(0, 0, 0x10000, 0xB00000)])
        corrupt = bytearray(good)
        corrupt[16] ^= 1
        for value in (good[:-1], bytes(corrupt),
                      table([(0, 0, 0x10000, 0x10000), (1, 2, 0x18000, 0x1000)]),
                      table([(0, 0, 0x10000, 16 * 1024 * 1024)]),
                      table([(0, 0x10, 0x10000, 0x10000)])):
            with self.subTest(length=len(value)), self.assertRaises(ValueError):
                LAYOUT.factory_app(value, 16 * 1024 * 1024)


class SourceLayoutTests(unittest.TestCase):
    def test_embedded_doom_layout_uses_reviewed_source_without_backups(self):
        for verifier in (DOOM, DOOM_AUDIO):
            with self.subTest(app=verifier.APP_DIR.name), tempfile.TemporaryDirectory() as directory:
                app = Path(directory)
                source = (verifier.APP_DIR / "partitions.csv").read_text()
                (app / "partitions.csv").write_text(source)
                with patch.object(verifier, "APP_DIR", app):
                    verifier.verify_reviewed_source_layout()
                    for changed in (source.replace("0x10000", "0x20000"), source + "malformed\n"):
                        (app / "partitions.csv").write_text(changed)
                        with self.assertRaises(SystemExit):
                            verifier.verify_reviewed_source_layout()

    def test_wad_capacity_uses_reviewed_source_without_backups(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            path = repo / "apps/wad_provisioner/partitions.csv"
            path.parent.mkdir(parents=True)
            source = (ROOT / "apps/wad_provisioner/partitions.csv").read_text()
            path.write_text(source)
            with patch.object(WAD, "ROOT", repo):
                self.assertEqual(WAD.factory_partition_from_reviewed_source(), (0x10000, 11 * 1024 * 1024))
                path.write_text(source.replace("11M", "12M"))
                with self.assertRaisesRegex(SystemExit, "boundary"):
                    WAD.factory_partition_from_reviewed_source()


class AudioAuthorizationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        self.auth = AUDIO_LEVEL.load_json(AUDIO_LEVEL.AUTHORIZATION)
        self.auth["classification"] = "user-requested-bounded-diagnostic-one-shot-inactive"
        self.auth["result"] = "prepared-inactive-pending-independent-final-gate-audit"
        self.auth["exact_host_tooling"] = {}
        for relative in AUDIO_LEVEL.HOST_TOOL_PATHS:
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(relative.encode())
            self.auth["exact_host_tooling"][relative] = hashlib.sha256(path.read_bytes()).hexdigest()

    def validate(self, mode="build-only"):
        with patch.object(AUDIO_LEVEL, "ROOT", self.repo), \
             patch.object(AUDIO_LEVEL, "load_json", return_value=self.auth):
            AUDIO_LEVEL.verify_authorization(mode)

    def test_new_reviewed_audio_authorization_does_not_require_manifest(self):
        self.validate()
        self.assertFalse((self.repo / "hardware/backups").exists())
        self.auth["exact_host_tooling"]["hardware/backups/manifest.json"] = "obsolete historical field"
        self.validate()

    def test_audio_layout_tool_and_exact_scope_stay_required(self):
        (self.repo / "scripts/verify-live-app-layout.py").write_text("changed")
        with self.assertRaisesRegex(SystemExit, "host tool changed"):
            self.validate()
        del self.auth["exact_host_tooling"]["scripts/verify-live-app-layout.py"]
        with self.assertRaisesRegex(SystemExit, "inventory"):
            self.validate()

    def test_audio_authorization_stays_inactive_and_app_only(self):
        with self.assertRaisesRegex(SystemExit, "not independently activated"):
            self.validate("app-flash")
        self.auth["exception_boundary"]["full_project_flash_authorized"] = True
        with self.assertRaisesRegex(SystemExit, "exception boundary"):
            self.validate()


class ShellFlashTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        scripts = self.repo / "scripts"
        (scripts / "lib").mkdir(parents=True)
        shutil.copyfile(ROOT / "scripts/flash.sh", scripts / "flash.sh")
        shutil.copyfile(ROOT / "scripts/verify-live-app-layout.py", scripts / "verify-live-app-layout.py")
        self.log = self.repo / "calls"
        self.env = {**os.environ, "FAKE_CALLS": str(self.log), "FAKE_ROOT": str(self.repo),
                    "FAKE_LIVE_IDENTITY": "a" * 64}
        (self.repo / "hardware").mkdir()
        self.profile = {"device_identity": {"sha256": "a" * 64},
                        "measured": {"flash_bytes": 16777216, "chip_revision": "v1.3"}}
        (self.repo / "hardware/board-profile.json").write_text(json.dumps(self.profile))
        (self.repo / "apps/bringup/build").mkdir(parents=True)
        self.auth = self.repo / "apps/bringup/app-metadata.json"
        self.auth.write_text(json.dumps({"flash_app_authorized": True, "flash_project_authorized": True}))
        (self.repo / "apps/bringup/build/app.bin").write_bytes(b"candidate" * 64)
        (self.repo / "apps/bringup/build/flasher_args.json").write_text(json.dumps({
            "app": {"offset": "0x10000", "file": "app.bin"}}))
        (scripts / "verify-metadata.py").write_text("import sys\nassert sys.argv[1:] == ['--flash-preflight']\n")
        (scripts / "lib/project-env.sh").write_text('''P4_PROJECT_ROOT=$FAKE_ROOT
p4_require_app() { [ "$1" = bringup ]; }
p4_require_flash_authorized() {
    python3 - "$P4_PROJECT_ROOT/apps/bringup/app-metadata.json" "$2" <<'CHECK'
import json, sys
key = 'flash_app_authorized' if sys.argv[2] == 'app-flash' else 'flash_project_authorized'
if json.load(open(sys.argv[1]))[key] is not True: raise SystemExit('flash not authorized')
CHECK
}
p4_require_port() { [ "$1" = /dev/null ]; }
p4_activate_idf() { :; }
p4_read_device_identity_hash() { printf '%s\\n' "$FAKE_LIVE_IDENTITY"; }
p4_redact_device_identifiers() { cat; }
p4_sha256_file() { python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }
''')
        (scripts / "lib/app-readback.sh").write_text('''P4_READBACK_CHUNK_BYTES=524288
p4_remove_readback() { :; }
p4_verify_chunked_application_readback() {
    P4_READBACK_HASH=$(p4_sha256_file "$1")
    P4_READBACK_ACTUAL_BYTES=$3
    P4_READBACK_CHUNKS=1
}
''')
        self.bin = self.repo / "bin"
        self.bin.mkdir()
        self.env["PATH"] = str(self.bin) + os.pathsep + self.env["PATH"]
        self.executable(scripts / "build.sh", '#!/bin/sh\nprintf "build\\n" >> "$FAKE_CALLS"\n')
        self.executable(self.bin / "idf.py", '#!/bin/sh\nprintf "write %s\\n" "$*" >> "$FAKE_CALLS"\n')
        self.executable(self.bin / "esptool.py", '''#!/usr/bin/env python3
import hashlib, os, pathlib, struct, sys
args = sys.argv[1:]
with open(os.environ['FAKE_CALLS'], 'a') as log: log.write('esptool ' + ' '.join(args) + '\\n')
if 'flash_id' in args:
    print('Chip is ESP32-P4 (revision ' + os.environ.get('FAKE_REVISION', 'v1.3') + ')')
    print('Detected flash size: ' + os.environ.get('FAKE_SIZE', '16MB'))
elif 'get_security_info' in args:
    print('Secure Boot: ' + os.environ.get('FAKE_SECURE_BOOT', 'Disabled'))
    print('Flash Encryption: Disabled')
elif 'read_flash' in args:
    n = args.index('read_flash')
    assert args[n+1:n+3] == ['0x8000', '0x1000'], 'must read only live partition table'
    offset = int(os.environ.get('FAKE_APP_OFFSET', '65536'))
    entry = struct.pack('<HBBII16sI', 0x50aa, 0, 0, offset, 0xb00000, b'factory', 0)
    value = entry + b'\\xeb\\xeb' + b'\\xff'*14 + hashlib.md5(entry).digest()
    pathlib.Path(args[n+3]).write_bytes(value.ljust(4096, b'\\xff'))
else: raise SystemExit('unexpected esptool call')
''')

    def executable(self, path, text):
        path.write_text(text)
        path.chmod(0o755)

    def run_flash(self, *, full=False):
        args = ["sh", str(self.repo / "scripts/flash.sh"), "--app", "bringup", "--port", "/dev/null"]
        if not full:
            args.append("--app-only")
        result = subprocess.run(args, cwd="/", env=self.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        calls = self.log.read_text() if self.log.exists() else ""
        return result, calls

    def test_app_only_flash_has_no_backup_directory_or_manifest(self):
        result, calls = self.run_flash()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("write ", calls)
        self.assertIn("read_flash 0x8000 0x1000", calls)
        self.assertFalse((self.repo / "hardware/backups").exists())

    def test_full_scope_flash_does_not_require_or_read_existing_backup(self):
        backups = self.repo / "hardware/backups"
        backups.mkdir()
        (backups / "manifest.json").write_text("broken historical manifest")
        (backups / "factory.bin").write_bytes(b"incomplete historical image")
        result, calls = self.run_flash(full=True)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("write ", calls)
        self.assertNotIn("read_flash", calls)
        self.assertEqual((backups / "manifest.json").read_text(), "broken historical manifest")

    def test_wrong_live_identity_security_capacity_revision_or_layout_cannot_write(self):
        cases = [("FAKE_LIVE_IDENTITY", "b" * 64), ("FAKE_SECURE_BOOT", "Enabled"),
                 ("FAKE_SIZE", "32MB"), ("FAKE_REVISION", "v2.0"),
                 ("FAKE_REVISION", "v1.30"), ("FAKE_APP_OFFSET", "131072")]
        for key, value in cases:
            with self.subTest(key=key), patch.dict(self.env, {key: value}):
                self.log.unlink(missing_ok=True)
                result, calls = self.run_flash()
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertNotIn("write ", calls)

    def test_flash_authorization_remains_required(self):
        self.auth.write_text(json.dumps({"flash_app_authorized": False}))
        result, calls = self.run_flash()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, "")


if __name__ == "__main__":
    unittest.main()
