#!/usr/bin/env python3
"""Exercise installation setup with a local fake pip; no package downloads."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]

FAKE_PIP = r"""
import os
from pathlib import Path
import shlex
import sys

with Path(os.environ["P4_TEST_PIP_LOG"]).open("a") as stream:
    stream.write(" ".join(sys.argv[1:]) + "\n")
if os.environ.get("P4_TEST_PIP_FAIL") == "1":
    raise SystemExit("simulated pip failure")
assert sys.argv[1:6] == ["install", "--disable-pip-version-check", "--no-input",
                          "--require-virtualenv", "--requirement"], sys.argv
requirements = Path(sys.argv[6]).read_text()
assert "esptool==4.12.0\n" in requirements and "pyserial==3.5\n" in requirements
site = Path(__file__).parent
for module, package, version in (("esptool", "esptool", "4.12.0"),
                                 ("serial", "pyserial", "3.5")):
    folder = site / module
    folder.mkdir()
    (folder / "__init__.py").write_text("__version__ = " + repr(version) + "\n")
    metadata = site / (package + "-" + version + ".dist-info")
    metadata.mkdir()
    (metadata / "METADATA").write_text("Metadata-Version: 2.1\nName: " + package
                                       + "\nVersion: " + version + "\n")
console = Path(sys.prefix) / "bin/p4-fake-tool"
console.write_text("#!/bin/sh\n'''exec' " + shlex.quote(sys.executable)
                  + " \"$0\" \"$@\"\n' '''\nimport esptool\nprint(esptool.__version__)\n")
console.chmod(0o755)
"""


class InstallToolsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="p4 install tools ")
        self.addCleanup(self.temporary.cleanup)
        self.fixture = Path(self.temporary.name)
        self.repo = self.fixture / "repository with spaces"
        self.repo.mkdir()
        (self.repo / "scripts").mkdir()
        self.script = self.repo / "scripts/setup-install-tools.sh"
        shutil.copyfile(ROOT / "scripts/setup-install-tools.sh", self.script)
        shutil.copyfile(ROOT / "requirements-install.txt", self.repo / "requirements-install.txt")
        self.env_dir = self.repo / ".tools/install-python"
        self.log = self.fixture / "pip-calls.txt"
        self.executables = self.fixture / "executables"
        self.executables.mkdir()
        self.bootstrap = self.executables / "python3"
        self.bootstrap.write_text(
            "#!" + sys.executable + "\n" + textwrap.dedent("""\
                import os
                from pathlib import Path
                import subprocess
                import sys
                REAL = {real!r}
                if sys.argv[1:3] == ["-m", "venv"]:
                    subprocess.run([REAL, "-m", "venv", "--without-pip", *sys.argv[3:]], check=True)
                    site = next(Path(sys.argv[-1]).glob("lib/python*/site-packages"))
                    (site / "pip.py").write_text({pip!r})
                else:
                    os.execv(REAL, [REAL, *sys.argv[1:]])
                """).format(real=sys.executable, pip=FAKE_PIP)
        )
        self.bootstrap.chmod(0o755)
        self.environment = dict(os.environ, PATH=str(self.executables) + os.pathsep + os.environ["PATH"],
                                P4_TEST_PIP_LOG=str(self.log))

    def setup(self, *args, environment=None):
        return subprocess.run(["sh", str(self.script), *args], cwd=self.repo,
                              env=self.environment if environment is None else environment,
                              text=True, capture_output=True)

    def test_create_and_reuse_without_bootstrap_or_pip(self):
        sdk_marker = self.repo / ".tools/esp-idf-v5.5.3/preserved.txt"
        sdk_marker.parent.mkdir(parents=True)
        sdk_marker.write_text("preserve SDK\n")
        first = self.setup()
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn(str(self.env_dir / "bin/python"), first.stdout)
        self.assertIn(str(self.env_dir / "bin/activate"), first.stdout)
        self.assertEqual(len(self.log.read_text().splitlines()), 1)
        self.assertEqual(sdk_marker.read_text(), "preserve SDK\n")
        self.assertFalse((self.repo / ".tools/.install-python.setup.lock").exists())
        self.assertEqual(list((self.repo / ".tools").glob(".install-python.tmp.*")), [])
        for path in (self.env_dir / "bin/activate", self.env_dir / "bin/p4-fake-tool",
                     self.env_dir / "pyvenv.cfg"):
            self.assertNotIn(".install-python.tmp.", path.read_text())
        console = subprocess.run([str(self.env_dir / "bin/p4-fake-tool")], text=True,
                                 capture_output=True)
        self.assertEqual(console.returncode, 0, console.stderr)
        self.assertEqual(console.stdout.strip(), "4.12.0")
        activated = subprocess.run(["sh", "-c", '. "$1"; printf "%s" "$VIRTUAL_ENV"',
                                    "sh", str(self.env_dir / "bin/activate")], text=True,
                                   capture_output=True)
        self.assertEqual(activated.returncode, 0, activated.stderr)
        self.assertEqual(activated.stdout, str(self.env_dir))
        self.bootstrap.write_text("#!/bin/sh\nexit 99\n")
        second = self.setup()
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_refuse_existing_incomplete_directory(self):
        self.env_dir.mkdir(parents=True)
        marker = self.env_dir / "do-not-remove"
        marker.write_text("existing data\n")
        result = self.setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to replace", result.stderr)
        self.assertEqual(marker.read_text(), "existing data\n")
        self.assertFalse(self.log.exists())

    def test_refuse_existing_wrong_version_without_repair(self):
        self.assertEqual(self.setup().returncode, 0)
        metadata = next(self.env_dir.glob("lib/python*/site-packages/esptool-*.dist-info/METADATA"))
        metadata.write_text(metadata.read_text().replace("4.12.0", "4.11.0"))
        result = self.setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("version mismatch", result.stderr)
        self.assertIn("Refusing to replace", result.stderr)
        self.assertIn("4.11.0", metadata.read_text())
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_failure_cleans_only_owned_stage(self):
        (self.repo / ".tools").mkdir()
        unrelated = self.repo / ".tools/owner-data"
        unrelated.write_text("retained\n")
        environment = dict(self.environment, P4_TEST_PIP_FAIL="1")
        result = self.setup(environment=environment)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("simulated pip failure", result.stderr)
        self.assertFalse(self.env_dir.exists())
        self.assertEqual(unrelated.read_text(), "retained\n")
        self.assertFalse((self.repo / ".tools/.install-python.setup.lock").exists())
        self.assertEqual(list((self.repo / ".tools").glob(".install-python.tmp.*")), [])

    def test_refuse_symlink_and_existing_setup_lock(self):
        (self.repo / ".tools").mkdir()
        external = self.fixture / "external"
        external.mkdir()
        self.env_dir.symlink_to(external, target_is_directory=True)
        result = self.setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to replace", result.stderr)
        self.assertTrue(self.env_dir.is_symlink())
        self.env_dir.unlink()
        lock = self.repo / ".tools/.install-python.setup.lock"
        lock.mkdir()
        result = self.setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refusing to replace setup lock", result.stderr)
        self.assertTrue(lock.exists())
        self.assertFalse(self.log.exists())

    def test_help_and_invalid_argument_do_not_install(self):
        result = self.setup("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("without ESP-IDF", result.stdout)
        result = self.setup("--repair")
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.repo / ".tools").exists())


if __name__ == "__main__":
    unittest.main()
