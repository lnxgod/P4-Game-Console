#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise filtered checkout and committed-source archives without hardware."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def load_packager():
    spec = importlib.util.spec_from_file_location("package_source", ROOT / "scripts/package-source.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def read_archive(path):
    with tarfile.open(path, "r:gz") as archive:
        return {member.name.split("/", 1)[1]: archive.extractfile(member).read()
                for member in archive.getmembers() if member.isfile()}


class LiteSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="p4-lite-source-")
        self.base = Path(self.temp.name)
        self.repo = self.base / "upstream"
        self.repo.mkdir()
        git(self.repo, "init", "-b", "main")
        git(self.repo, "config", "user.name", "P4 source fixture")
        git(self.repo, "config", "user.email", "fixture@example.invalid")
        git(self.repo, "config", "uploadpack.allowFilter", "true")
        self.keep = {
            "README.md": b"fixture readme\n",
            "LICENSE": b"fixture license\n",
            ".agents/skills/example/SKILL.md": b"fixture skill\n",
            "toolchain.lock.json": b"{}\n",
            "games/draft/game.json": json.dumps({"enabled": False, "sources": ["draft.c"],
                "launcher_icon": "assets/launch-v5/launcher.p4i",
                "resource_payload": "assets/generated/art.bin"}).encode(),
            "games/draft/src/draft.c": b'#include "generated/art.inc"\n',
            "games/draft/src/generated/art.inc": b"/* runtime artwork */\n",
            "games/draft/assets/launch-v5/launcher.p4i": b"packed icon\n",
            "games/draft/assets/generated/art.bin": b"resource payload\n",
            "games/draft/assets/provenance.json": b"{}\n",
            "games/draft/assets/music/score.pdf": b"music source\n",
            "games/draft/assets/music/score.mid": b"music source\n",
            "games/draft/assets/music/score.ly": b"music source\n",
            "games/draft/tools/convert.py": b"# offline converter\n",
            "games/draft/README.md": b"asset notices\n",
            "games/lord/src/generated/title.inc": b"protected runtime artwork\n",
            "games/lord/game.json": b'{"component":"lord","enabled":false}\n',
            "games/lord/src/lord.c": b"/* default manifest source */\n",
            "design/tab5-bluetooth/proposal.json": b"{}\n",
            "design/gallery/README.md": b"design provenance\n",
            "design/research/candidate.patch": b"historical patch\n",
            "design/gallery/page.html": b"gallery source\n",
            "components/console_shell/assets/cover.png": b"keep app PNG\n",
            "apps/console_os/main/assets/boot.png": b"keep app PNG\n",
            "apps/console_os/main/assets/boot.rgb565a8": b"packed boot mark\n",
            "third_party/arimo/OFL.txt": b"font license\n",
            "images/other.png": b"outside omission roots\n",
            "games/draft/other.png": b"outside assets\n",
            "games/draft/assets/keep.svg": b"not part of the PNG policy\n",
            ".gitattributes": (ROOT / ".gitattributes").read_bytes() +
                              b"\ngames/draft/src/draft.c export-ignore\n",
        }
        self.omit = {
            "games/draft/assets/source.png": os.urandom(1024 * 1024),
            "games/draft/assets/launch-v5/deep/source.PNG": os.urandom(1024 * 1024),
            "design/gallery/preview.png": os.urandom(1024 * 1024),
            "design/gallery/deep/preview.GIF": os.urandom(1024 * 1024),
            "design/root.png": b"root design preview\n",
        }
        for name, data in {**self.keep, **self.omit}.items():
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        executable = self.repo / "games/draft/tools/convert.py"
        executable.chmod(0o755)
        (self.repo / "source-link").symlink_to("README.md")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-qm", "source fixture")
        self.commit = git(self.repo, "rev-parse", "HEAD")

    def tearDown(self):
        self.temp.cleanup()

    def test_filtered_clone_preserves_runtime_and_avoids_authoring_blobs(self):
        checkout = self.base / "checkout"
        subprocess.run([str(ROOT / "scripts/clone-lite.sh"), self.repo.as_uri(),
                        str(checkout), "main"], check=True, capture_output=True, text=True)
        self.assertEqual(git(checkout, "rev-parse", "--is-shallow-repository"), "true")
        self.assertEqual(git(checkout, "config", "remote.origin.promisor"), "true")
        self.assertEqual(git(checkout, "config", "remote.origin.partialclonefilter"), "blob:none")
        self.assertEqual(git(checkout, "rev-parse", "HEAD"), self.commit)
        for name, expected in self.keep.items():
            self.assertEqual((checkout / name).read_bytes(), expected, name)
        for name in self.omit:
            self.assertFalse((checkout / name).exists(), name)
        self.assertTrue((checkout / "source-link").is_symlink())
        objects = git(checkout, "rev-parse", "--git-path", "objects")
        object_dir = Path(objects) if Path(objects).is_absolute() else checkout / objects
        stored = sum(path.stat().st_size for path in object_dir.rglob("*") if path.is_file())
        self.assertLess(stored, 512 * 1024, "excluded random media was fetched")
        subprocess.run(["git", "-C", str(checkout), "sparse-checkout", "disable"],
                       check=True, capture_output=True)
        for name, expected in self.omit.items():
            self.assertEqual((checkout / name).read_bytes(), expected, name)

    def test_archives_pair_exact_commit_and_ignore_dirty_or_export_attributes(self):
        packager = load_packager()
        (self.repo / "README.md").write_text("dirty worktree must not ship\n")
        (self.repo / "untracked-secret").write_text("must not ship\n")
        output = self.base / "packages"
        report = packager.create_packages(self.repo, self.commit, output)
        self.assertEqual(report["source_commit"], self.commit)
        lite = read_archive(output / report["archives"]["source_lite"]["file"])
        art = read_archive(output / report["archives"]["art_source"]["file"])
        stamp = {"schema": 1, "source_commit": self.commit}
        self.assertEqual(json.loads(lite.pop(".p4-source.json")), stamp)
        self.assertEqual(json.loads(art.pop(".p4-source.json")), stamp)
        self.assertEqual(lite, self.keep)
        self.assertEqual({name: data for name, data in art.items() if name in self.omit}, self.omit)
        self.assertEqual({**lite, **art}, {**self.keep, **self.omit})
        self.assertIn("LICENSE", art)
        self.assertIn("games/draft/assets/provenance.json", art)
        with tarfile.open(output / report["archives"]["source_lite"]["file"], "r:gz") as archive:
            prefix = f"p4console-{self.commit}/"
            self.assertEqual(archive.getmember(prefix + "games/draft/tools/convert.py").mode, 0o755)
            self.assertEqual(archive.getmember(prefix + "source-link").linkname, "README.md")
        for artifact in report["archives"].values():
            path = output / artifact["file"]
            self.assertEqual(path.stat().st_size, artifact["bytes"])
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), artifact["sha256"])
        repeated = packager.create_packages(self.repo, self.commit, self.base / "repeat")
        self.assertEqual(report, repeated)

    def test_required_input_cannot_be_silently_omitted(self):
        manifest = json.loads(self.keep["games/draft/game.json"])
        manifest["resource_payload"] = "assets/source.png"
        (self.repo / "games/draft/game.json").write_text(json.dumps(manifest))
        git(self.repo, "add", "games/draft/game.json")
        git(self.repo, "commit", "-qm", "fixture future PNG runtime input")
        with self.assertRaisesRegex(ValueError, "required.*assets/source.png"):
            load_packager().create_packages(self.repo, "HEAD", self.base / "invalid")

    def test_source_zip_attributes_match_selective_media_policy(self):
        for path in self.keep.keys() | self.omit.keys():
            value = git(self.repo, "check-attr", "export-ignore", "--", path).rsplit(": ", 1)[1]
            expected = path in self.omit or path == "games/draft/src/draft.c"
            self.assertEqual(value == "set", expected, path)

    def test_filesystem_path_clone_keeps_filter_and_shallow_history(self):
        checkout = self.base / "local-checkout"
        subprocess.run([str(ROOT / "scripts/clone-lite.sh"), str(self.repo), str(checkout)],
                       check=True, capture_output=True, text=True)
        self.assertEqual(git(checkout, "rev-parse", "--is-shallow-repository"), "true")
        object_dir = checkout / ".git/objects"
        stored = sum(path.stat().st_size for path in object_dir.rglob("*") if path.is_file())
        self.assertLess(stored, 512 * 1024, "local optimization fetched original media")

    def test_replacement_refs_cannot_change_stamped_source(self):
        (self.repo / "README.md").write_text("replacement tree must not ship\n")
        git(self.repo, "add", "README.md")
        git(self.repo, "commit", "-qm", "replacement fixture")
        replacement = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "replace", self.commit, replacement)
        output = self.base / "unreplaced"
        report = load_packager().create_packages(self.repo, self.commit, output)
        lite = read_archive(output / report["archives"]["source_lite"]["file"])
        self.assertEqual(report["source_commit"], self.commit)
        self.assertEqual(lite["README.md"], self.keep["README.md"])

    def test_current_repository_archives_retain_actual_build_inputs(self):
        packager = load_packager()
        report = packager.create_packages(ROOT, "HEAD", self.base / "actual")
        lite_path = self.base / "actual" / report["archives"]["source_lite"]["file"]
        with tarfile.open(lite_path, "r:gz") as archive:
            names = {member.name.split("/", 1)[1] for member in archive.getmembers()}
            prefix = f"p4console-{report['source_commit']}/"
            manifest_names = [name for name in names if name.startswith("games/")
                              and len(Path(name).parts) == 3 and name.endswith("/game.json")]
            for name in manifest_names:
                manifest = json.load(archive.extractfile(prefix + name))
                game = str(Path(name).parent)
                sources = manifest.get("sources", [f"{manifest['component']}.c"])
                required = [f"{game}/src/{source}" for source in sources]
                required += [f"{game}/{manifest[key]}" for key in ("launcher_icon", "resource_payload")
                             if key in manifest]
                for path in required:
                    self.assertIn(path, names, path)
            for path in ("games/byte_buddy/assets/generated/byte_buddy_dragon_art.bin",
                         "games/byte_buddy/assets/launch-v5/launcher.p4i",
                         "games/lord/src/generated/lord_illustrated_title.h",
                         "design/tab5-bluetooth/proposal.json",
                         "game-data/pure-hades/v0.6/PUREHADES.WAD",
                         "game-data/pure-hades/v0.6/LICENSE",
                         "apps/console_os/main/assets/gamechangers_mark_flight.rgb565a8"):
                self.assertIn(path, names)
            self.assertFalse(any(packager.is_authoring_media(name) for name in names))
        self.assertGreater(report["omitted_media_bytes"], 200 * 1024 * 1024)
        self.assertLess(report["lite_tracked_bytes"], report["tracked_bytes"] // 3)


if __name__ == "__main__":
    unittest.main()
