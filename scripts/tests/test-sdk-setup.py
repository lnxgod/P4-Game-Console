#!/usr/bin/env python3
"""Exercise SDK setup with tiny local Git repositories and inert IDF scripts."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
REAL_GIT = shutil.which("git")
TAG = "v5.5.3"


class SDKSetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if REAL_GIT is None:
            raise unittest.SkipTest("Git is required")
        cls.fixture = tempfile.TemporaryDirectory(prefix="p4-sdk-fixture-")
        cls.upstream = Path(cls.fixture.name) / "sdk upstream"
        cls.component = Path(cls.fixture.name) / "component upstream"
        cls.git_env = {
            "PATH": os.defpath,
            "HOME": cls.fixture.name,
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": os.devnull,
            "GIT_AUTHOR_NAME": "SDK fixture",
            "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
            "GIT_COMMITTER_NAME": "SDK fixture",
            "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
        }
        cls.component.mkdir()
        cls.git(cls.component, "init", "-q")
        (cls.component / "source.c").write_text("/* pinned component */\n")
        cls.git(cls.component, "add", ".")
        cls.git(cls.component, "commit", "-qm", "pinned component")
        cls.component_commit = cls.git(cls.component, "rev-parse", "HEAD").stdout.strip()
        (cls.component / "source.c").write_text("/* later component */\n")
        cls.git(cls.component, "commit", "-qam", "later component")

        cls.upstream.mkdir()
        cls.git(cls.upstream, "init", "-q")
        install = cls.upstream / "install.sh"
        install.write_text(
            f"#!{sys.executable}\n"
            "import json, os, pathlib, sys\n"
            "pathlib.Path(os.environ['TEST_INSTALL_LOG']).write_text(json.dumps({\n"
            "    'args': sys.argv[1:], 'sdk': str(pathlib.Path(__file__).parent),\n"
            "    'tools': os.environ.get('IDF_TOOLS_PATH', '<unset>')}))\n"
        )
        install.chmod(0o755)
        (cls.upstream / "export.sh").write_text(
            "printf '%s\\n' \"${IDF_TOOLS_PATH-<unset>}\" > \"$TEST_EXPORT_LOG\"\n"
            "export PATH=\"$TEST_BIN:$PATH\"\n"
        )
        cls.git(cls.upstream, "submodule", "add", "-q", cls.component.as_uri(), "component")
        cls.git(cls.upstream / "component", "checkout", "-q", cls.component_commit)
        cls.git(cls.upstream, "add", ".")
        cls.git(cls.upstream, "commit", "-qm", "pinned SDK")
        cls.sdk_commit = cls.git(cls.upstream, "rev-parse", "HEAD").stdout.strip()
        cls.git(cls.upstream, "tag", TAG)
        cls.git(cls.upstream, "commit", "--allow-empty", "-qm", "later SDK")

    @classmethod
    def tearDownClass(cls):
        cls.fixture.cleanup()

    @classmethod
    def git(cls, cwd, *args):
        return subprocess.run(
            [REAL_GIT, "-c", "protocol.file.allow=always", "-C", str(cwd), *args],
            env=cls.git_env, text=True, capture_output=True, check=True,
        )

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="p4-sdk-test-")
        self.addCleanup(self.temporary.cleanup)
        base = Path(self.temporary.name)
        self.project = base / "project with spaces"
        self.home = base / "home with spaces"
        self.bin = base / "bin"
        for path in (self.project / "scripts/lib", self.home, self.bin):
            path.mkdir(parents=True)
        for relative in ("scripts/install-esp-idf.sh", "scripts/lib/project-env.sh"):
            shutil.copy2(ROOT / relative, self.project / relative)
        (self.project / "toolchain.lock.json").write_text(json.dumps({"esp_idf": {
            "version": "5.5.3", "git_tag": TAG, "git_commit": self.sdk_commit,
        }}))
        self.git_log = base / "git.jsonl"
        self.install_log = base / "install.json"
        self.export_log = base / "export.txt"
        self.env = dict(self.git_env, HOME=str(self.home), PATH=f"{self.bin}:{os.defpath}",
                        TEST_REAL_GIT=REAL_GIT, TEST_UPSTREAM=self.upstream.as_uri(),
                        TEST_GIT_LOG=str(self.git_log), TEST_INSTALL_LOG=str(self.install_log),
                        TEST_EXPORT_LOG=str(self.export_log), TEST_BIN=str(self.bin))
        (self.bin / "python3").symlink_to(sys.executable)
        git_stub = self.bin / "git"
        git_stub.write_text(
            f"#!{sys.executable}\n"
            "import json, os, pathlib, sys\n"
            "args = sys.argv[1:]\n"
            "with pathlib.Path(os.environ['TEST_GIT_LOG']).open('a') as log:\n"
            "    log.write(json.dumps(args) + '\\n')\n"
            "if args[:1] == ['clone']:\n"
            "    args = [os.environ['TEST_UPSTREAM'] if arg ==\n"
            "            'https://github.com/espressif/esp-idf.git' else arg for arg in args]\n"
            "if os.environ.get('TEST_SKIP_SUBMODULE_UPDATE') and 'update' in args:\n"
            "    sys.exit(0)\n"
            "git = os.environ['TEST_REAL_GIT']\n"
            "os.execv(git, [git, '-c', 'protocol.file.allow=always', *args])\n"
        )
        git_stub.chmod(0o755)
        idf_stub = self.bin / "idf.py"
        idf_stub.write_text("#!/bin/sh\nprintf 'ESP-IDF %s\\n' \"${TEST_IDF_VERSION:-v5.5.3}\"\n")
        idf_stub.chmod(0o755)

    def existing_sdk(self, location="shared"):
        if location == "shared":
            path = self.home / "esp" / "esp-idf-v5.5.3"
        else:
            path = self.project / ".tools" / "esp-idf-v5.5.3"
        path.parent.mkdir(parents=True, exist_ok=True)
        self.git(path.parent, "clone", "-q", "--branch", TAG, "--depth", "1",
                 "--recursive", "--shallow-submodules", self.upstream.as_uri(), str(path))
        return path

    def setup_sdk(self):
        return subprocess.run(["sh", str(self.project / "scripts/install-esp-idf.sh")],
                              env=self.env, text=True, capture_output=True)

    def activate_sdk(self):
        return subprocess.run(
            ["sh", "-c", 'P4_SCRIPT_DIR=$1; . "$P4_SCRIPT_DIR/lib/project-env.sh"; p4_activate_idf',
             "test", str(self.project / "scripts")],
            env=self.env, text=True, capture_output=True,
        )

    def calls(self):
        return [json.loads(line) for line in self.git_log.read_text().splitlines()] if self.git_log.exists() else []

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_rejected_before_use(self, result, message):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(message, result.stderr)
        self.assertFalse(self.install_log.exists())
        self.assertFalse(self.export_log.exists())
        self.assertFalse(any("update" in call for call in self.calls()))

    def test_fresh_clone_is_shallow_and_keeps_older_pinned_submodule(self):
        self.assert_success(self.setup_sdk())
        sdk = self.project / ".tools/esp-idf-v5.5.3"
        self.assertEqual(self.git(sdk, "rev-parse", "HEAD").stdout.strip(), self.sdk_commit)
        self.assertEqual(self.git(sdk / "component", "rev-parse", "HEAD").stdout.strip(),
                         self.component_commit)
        for path in (sdk, sdk / "component"):
            self.assertEqual(self.git(path, "rev-parse", "--is-shallow-repository").stdout.strip(), "true")
        clone = next(call for call in self.calls() if call[0] == "clone")
        self.assertIn("--shallow-submodules", clone)
        self.assertIn(["--depth", "1"], [clone[i:i + 2] for i in range(len(clone))])
        self.assertIn(["--jobs", "4"], [clone[i:i + 2] for i in range(len(clone))])
        update = next(call for call in self.calls() if "update" in call)
        self.assertEqual(update[-4:], ["--depth", "1", "--jobs", "4"])
        self.assertEqual(json.loads(self.install_log.read_text())["args"], ["esp32p4"])

    def test_shared_sdk_is_reused_without_local_clone(self):
        sdk = self.existing_sdk()
        self.assert_success(self.setup_sdk())
        self.assertEqual(json.loads(self.install_log.read_text())["sdk"], str(sdk))
        self.assertFalse((self.project / ".tools/esp-idf-v5.5.3").exists())
        self.assertFalse(any(call[0] == "clone" for call in self.calls()))

    def test_ambient_sdk_is_reused(self):
        sdk = self.existing_sdk()
        moved = self.home / "ambient SDK"
        sdk.rename(moved)
        self.env["IDF_PATH"] = str(moved)
        self.assert_success(self.setup_sdk())
        self.assertEqual(json.loads(self.install_log.read_text())["sdk"], str(moved))
        self.assertFalse(any(call[0] == "clone" for call in self.calls()))

    def test_mismatched_discovered_sdk_is_rejected_without_fallback(self):
        self.existing_sdk()
        local = self.existing_sdk("local")
        self.git(local, "tag", "-d", TAG)
        self.assert_rejected_before_use(self.setup_sdk(), "tag mismatch")
        self.assert_rejected_before_use(self.activate_sdk(), "tag mismatch")
        self.assertFalse(any(call[0] == "clone" for call in self.calls()))

    def test_explicit_sdk_wins_over_discovered_sdk(self):
        self.existing_sdk()
        explicit = self.project / "explicit SDK"
        self.env["P4_IDF_PATH"] = str(explicit)
        self.assert_success(self.setup_sdk())
        self.assertEqual(json.loads(self.install_log.read_text())["sdk"], str(explicit))

    def test_incomplete_explicit_directory_is_not_replaced(self):
        self.existing_sdk()
        explicit = self.project / "incomplete SDK"
        explicit.mkdir()
        marker = explicit / "keep.txt"
        marker.write_text("keep")
        self.env["P4_IDF_PATH"] = str(explicit)
        self.assert_rejected_before_use(self.setup_sdk(), "Refusing to replace incomplete")
        self.assertEqual(marker.read_text(), "keep")

    def test_bad_checkout_is_rejected_before_install_and_export(self):
        mutations = ("tag", "commit", "dirty", "dirty-submodule", "gitlink", "no-git")
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                sdk = self.existing_sdk()
                self.env["P4_IDF_PATH"] = str(sdk)
                if mutation == "tag":
                    self.git(sdk, "tag", "-d", TAG)
                    message = "tag mismatch"
                elif mutation == "commit":
                    self.git(sdk, "commit", "--allow-empty", "-qm", "wrong commit")
                    self.git(sdk, "tag", "-f", TAG)
                    message = "commit mismatch"
                elif mutation in ("dirty", "dirty-submodule"):
                    path = sdk / ("export.sh" if mutation == "dirty" else "component/source.c")
                    path.write_text(path.read_text() + "# modified\n")
                    if mutation == "dirty-submodule":
                        self.git(sdk, "config", "submodule.component.ignore", "all")
                    message = "tracked changes"
                elif mutation == "gitlink":
                    self.git(sdk / "component", "fetch", "-q", "origin")
                    self.git(sdk / "component", "checkout", "-q", "origin/HEAD")
                    self.git(sdk, "config", "submodule.component.ignore", "all")
                    message = "tracked changes"
                else:
                    shutil.rmtree(sdk / ".git")
                    message = "no Git metadata"
                self.assert_rejected_before_use(self.setup_sdk(), message)
                self.assert_rejected_before_use(self.activate_sdk(), message)
                shutil.rmtree(sdk)
                self.git_log.unlink(missing_ok=True)

    def test_missing_submodule_is_initialized_but_never_accepted_unresolved(self):
        sdk = self.existing_sdk()
        self.git(sdk, "submodule", "deinit", "-q", "--all")
        self.assert_rejected_before_use(self.activate_sdk(), "submodules are missing")
        self.env["TEST_SKIP_SUBMODULE_UPDATE"] = "1"
        result = self.setup_sdk()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("submodules are missing", result.stderr)
        self.assertFalse(self.install_log.exists())
        del self.env["TEST_SKIP_SUBMODULE_UPDATE"]
        self.assert_success(self.setup_sdk())
        self.assert_success(self.activate_sdk())

    def test_tools_cache_selection_and_overrides_match_for_install_and_activation(self):
        self.existing_sdk()
        cache = self.project / ".tools/espressif"
        cases = ((True, None, str(cache)), (True, "/explicit/tools", "/explicit/tools"),
                 (True, "", ""), (False, None, "<unset>"))
        for present, override, expected in cases:
            with self.subTest(cache=present, override=override):
                if present:
                    cache.mkdir(parents=True, exist_ok=True)
                elif cache.exists():
                    cache.rmdir()
                self.env.pop("IDF_TOOLS_PATH", None)
                if override is not None:
                    self.env["IDF_TOOLS_PATH"] = override
                self.assert_success(self.setup_sdk())
                self.assertEqual(json.loads(self.install_log.read_text())["tools"], expected)
                self.assert_success(self.activate_sdk())
                self.assertEqual(self.export_log.read_text().removesuffix("\n"), expected)

    def test_git_worktree_sdk_and_version_guard(self):
        sdk = self.existing_sdk()
        worktree = self.home / "SDK worktree"
        self.git(sdk, "worktree", "add", "--detach", str(worktree), self.sdk_commit)
        self.git(worktree, "submodule", "update", "--init", "--recursive")
        self.env["P4_IDF_PATH"] = str(worktree)
        self.assert_success(self.setup_sdk())
        self.assert_success(self.activate_sdk())
        self.env["TEST_IDF_VERSION"] = "v5.4.0"
        result = self.activate_sdk()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ESP-IDF mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
