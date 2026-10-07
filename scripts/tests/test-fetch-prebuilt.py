#!/usr/bin/env python3
"""Exact-revision downloads fail closed and preserve existing local build outputs."""
import base64
import contextlib
import importlib.util
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

SCRIPTS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("fetch_prebuilt", SCRIPTS / "fetch-prebuilt.py")
fetcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetcher)
COMMIT = "a" * 40
DIGEST = "b" * 64


class FetchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        (self.root / ".p4-source.json").write_text(json.dumps({"schema": 1, "source_commit": COMMIT}))

    def test_archive_stamp_and_git_precedence(self):
        self.assertEqual(fetcher.source_commit(self.root), COMMIT)
        (self.root / ".git").write_text("gitdir: elsewhere")
        with patch.object(fetcher.subprocess, "run", return_value=Mock(stdout="c" * 40 + "\n")) as git:
            self.assertEqual(fetcher.source_commit(self.root), "c" * 40)
            self.assertEqual(git.call_args.kwargs["cwd"], self.root)

    def test_stamp_rejects_missing_or_short_revision(self):
        for stamp in ({"schema": 1, "source_commit": "aaaa"}, {"schema": 2, "source_commit": COMMIT},
                      {"schema": True, "source_commit": COMMIT}, [],
                      {"schema": 1, "source_commit": COMMIT, "extra": True}):
            (self.root / ".p4-source.json").write_text(json.dumps(stamp))
            with self.assertRaises(ValueError):
                fetcher.source_commit(self.root)
        (self.root / ".p4-source.json").unlink()
        with self.assertRaises(ValueError):
            fetcher.source_commit(self.root)

    def test_stamp_rejects_duplicate_keys_large_or_linked_file(self):
        stamp = self.root / ".p4-source.json"
        for raw in ('{"schema":1,"schema":1,"source_commit":"' + COMMIT + '"}', ' ' * 1025):
            stamp.write_text(raw)
            with self.assertRaises(ValueError):
                fetcher.source_commit(self.root)
        stamp.unlink()
        target = self.root / "stamp-target"
        target.write_text(json.dumps({"schema": 1, "source_commit": COMMIT}))
        stamp.symlink_to(target)
        with self.assertRaises(ValueError):
            fetcher.source_commit(self.root)

    def test_checksums_require_exact_unique_asset(self):
        filename = f"tab5-{COMMIT}.tar.gz"
        raw = f"{DIGEST}  {filename}\n".encode()
        self.assertEqual(fetcher.checksum_for(raw, filename), DIGEST)
        for invalid in (raw + raw, b"invalid\n", f"{DIGEST}  another.tar.gz\n".encode()):
            with self.assertRaises(ValueError):
                fetcher.checksum_for(invalid, filename)

    @contextlib.contextmanager
    def github(self, replies):
        """A real subprocess fixture exercises pipes without credentials or network."""
        binary = self.root / "bin"
        binary.mkdir(exist_ok=True)
        executable = binary / "gh"
        executable.write_text(f"#!{sys.executable}\n" + '''
import base64, json, os, sys, time
configuration = json.load(open(os.environ["P4_GH_FIXTURE"]))
with open(os.environ["P4_GH_CALLS"], "a") as log:
    log.write(json.dumps({"args": sys.argv[1:],
        "prompt_disabled": os.environ.get("GH_PROMPT_DISABLED"),
        "debug": os.environ.get("GH_DEBUG")}) + "\\n")
reply = configuration[sys.argv[-1]]
sys.stdout.buffer.write(base64.b64decode(reply.get("payload", "")))
sys.stdout.buffer.flush()
sys.stderr.write(reply.get("stderr", ""))
time.sleep(reply.get("wait", 0))
raise SystemExit(reply.get("exit", 0))
''')
        executable.chmod(0o755)
        configuration = self.root / "github-fixture.json"
        configuration.write_text(json.dumps(replies))
        calls = self.root / "github-calls.jsonl"
        calls.write_text("")
        with patch.dict(os.environ, {"PATH": str(binary) + os.pathsep + os.environ.get("PATH", ""),
                                    "P4_GH_FIXTURE": str(configuration), "P4_GH_CALLS": str(calls),
                                    "GH_HOST": "other.example", "GH_DEBUG": "api"}):
            yield calls

    def reply(self, payload, **extra):
        return {"payload": base64.b64encode(payload).decode("ascii"), **extra}

    def metadata(self, checksum=None, archive=b"fixture"):
        checksum = checksum or f"{DIGEST}  tab5-{COMMIT}.tar.gz\n".encode()
        return {"tag_name": f"tab5-{COMMIT}", "draft": False, "assets": [
            {"name": "SHA256SUMS", "id": 41, "size": len(checksum), "state": "uploaded",
             "url": "https://other.example/ignore-this", "browser_download_url": "https://other.example/ignore-this"},
            {"name": f"tab5-{COMMIT}.tar.gz", "id": 42, "size": len(archive), "state": "uploaded"},
        ]}

    def endpoints(self):
        base = f"repos/openai/P4-Game-Console/releases"
        return f"{base}/tags/tab5-{COMMIT}", f"{base}/assets/41", f"{base}/assets/42"

    def test_authenticated_download_is_bounded_and_pins_github_host(self):
        endpoint = self.endpoints()[1]
        payload = b"\x00\x1b\xffabc"
        with self.github({endpoint: self.reply(payload)}) as calls:
            target = self.root / "valid"
            fetcher.download(endpoint, target, len(payload), accept="application/octet-stream")
        self.assertEqual(target.read_bytes(), payload)
        call = json.loads(calls.read_text())
        self.assertEqual(call["args"], ["api", "--hostname", "github.com", "--method", "GET",
            "--header", "Accept: application/octet-stream", "--allow-escape-sequences", endpoint])
        self.assertEqual(call["prompt_disabled"], "1")
        self.assertIsNone(call["debug"])
        with self.github({endpoint: self.reply(b"12345")}):
            target = self.root / "too-large"
            with self.assertRaisesRegex(ValueError, "size limit"):
                fetcher.download(endpoint, target, 4)
        self.assertFalse(target.exists())
        with patch.object(fetcher.subprocess, "Popen") as process:
            with self.assertRaisesRegex(ValueError, "internal repository"):
                fetcher.download("repos/lnxgod/P4-Game-Console/releases/latest", self.root / "upstream", 4)
        process.assert_not_called()

    def test_download_timeout_kills_process_and_removes_partial_file(self):
        endpoint = self.endpoints()[1]
        target = self.root / "timeout"
        with self.github({endpoint: self.reply(b"partial", wait=10)}):
            with self.assertRaisesRegex(ValueError, "timed out"):
                fetcher.download(endpoint, target, 100, timeout=0.1)
        self.assertFalse(target.exists())

    def test_missing_github_cli_and_private_access_fail_closed_without_stderr(self):
        endpoint = self.endpoints()[0]
        with patch.object(fetcher.subprocess, "Popen", side_effect=FileNotFoundError("missing")):
            with self.assertRaisesRegex(ValueError, "GitHub CLI"):
                fetcher.download(endpoint, self.root / "no-gh", 100)
        self.assertFalse((self.root / "no-gh").exists())
        output = self.root / "cache"
        with self.github({endpoint: self.reply(b"partial", exit=4, stderr="SECRET_SENTINEL")}) as calls:
            with self.assertRaisesRegex(ValueError, "verify repository access") as error:
                fetcher.fetch(self.root, output)
        self.assertNotIn("SECRET_SENTINEL", str(error.exception))
        self.assertFalse(output.exists())
        self.assertEqual(len(calls.read_text().splitlines()), 1)

    def test_release_metadata_requires_exact_published_unique_bounded_assets(self):
        valid = self.metadata()
        variants = []
        for key, value in (("tag_name", "latest"), ("draft", True), ("assets", [])):
            variants.append({**valid, key: value})
        variants.append({**valid, "assets": valid["assets"] + [valid["assets"][0]]})
        for change in ({"id": True}, {"id": -1}, {"size": True},
                       {"size": fetcher.MAX_CHECKSUM_BYTES + 1}, {"state": "new"}):
            variants.append({**valid, "assets": [{**valid["assets"][0], **change}, valid["assets"][1]]})
        for metadata in variants:
            with self.subTest(metadata=metadata), tempfile.TemporaryDirectory(dir=self.root) as directory:
                endpoint = self.endpoints()[0]
                with self.github({endpoint: self.reply(json.dumps(metadata).encode())}):
                    with self.assertRaises(ValueError):
                        fetcher.release_assets(COMMIT, pathlib.Path(directory))

    def test_private_exact_tag_and_assets_preserve_checksum_and_source_binding(self):
        archive = b"binary\x00\xfffixture"
        checksum = f"{DIGEST}  tab5-{COMMIT}.tar.gz\n".encode()
        metadata, sums, asset = self.endpoints()
        replies = {metadata: self.reply(json.dumps(self.metadata(checksum, archive)).encode()),
                   sums: self.reply(checksum), asset: self.reply(archive)}
        def unpack(path, destination, **kwargs):
            self.assertEqual(path.read_bytes(), archive)
            self.assertEqual(kwargs, {"expected_sha256": DIGEST, "expected_source_commit": COMMIT, "source_root": self.root})
            destination.mkdir()
            (destination / "manifest.json").write_text("{}")
        with self.github(replies) as calls, patch.object(fetcher, "extract_release", side_effect=unpack):
            with contextlib.redirect_stdout(io.StringIO()):
                output = fetcher.fetch(self.root)
        self.assertTrue((output / "manifest.json").exists())
        self.assertEqual([json.loads(line)["args"][-1] for line in calls.read_text().splitlines()],
                         [metadata, sums, asset])

    def test_bad_checksum_does_not_download_archive(self):
        metadata, sums, asset = self.endpoints()
        checksum = b"invalid checksum\n"
        replies = {metadata: self.reply(json.dumps(self.metadata(checksum)).encode()), sums: self.reply(checksum)}
        with self.github(replies) as calls, patch.object(fetcher, "extract_release") as extract:
            with self.assertRaisesRegex(ValueError, "checksum inventory"):
                fetcher.fetch(self.root)
        extract.assert_not_called()
        self.assertEqual([json.loads(line)["args"][-1] for line in calls.read_text().splitlines()], [metadata, sums])

    def test_asset_truncation_does_not_activate(self):
        metadata, sums, asset = self.endpoints()
        checksum = f"{DIGEST}  tab5-{COMMIT}.tar.gz\n".encode()
        replies = {metadata: self.reply(json.dumps(self.metadata(checksum)).encode()),
                   sums: self.reply(checksum), asset: self.reply(b"short")}
        with self.github(replies), patch.object(fetcher, "extract_release") as extract:
            with self.assertRaisesRegex(ValueError, "byte count"):
                with contextlib.redirect_stdout(io.StringIO()):
                    fetcher.fetch(self.root)
        extract.assert_not_called()
        self.assertFalse((self.root / "build-host/prebuilt/tab5" / COMMIT).exists())

    def test_cached_validated_package_needs_no_network(self):
        output = self.root / "cache"
        output.mkdir()
        with patch.object(fetcher, "validate_release") as validate, patch.object(fetcher, "download") as network:
            self.assertEqual(fetcher.fetch(self.root, output), output)
        validate.assert_called_once_with(output, expected_source_commit=COMMIT, source_root=self.root)
        network.assert_not_called()

    def test_invalid_cache_is_preserved(self):
        output = self.root / "cache"
        output.mkdir()
        (output / "keep").write_text("unchanged")
        with patch.object(fetcher, "validate_release", side_effect=ValueError("hash mismatch")):
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                fetcher.fetch(self.root, output)
        self.assertEqual((output / "keep").read_text(), "unchanged")

    def test_failed_offline_verification_does_not_activate(self):
        archive = self.root / "offline.tar.gz"
        archive.write_bytes(b"fixture")
        output = self.root / "cache"
        with patch.object(fetcher, "extract_release", side_effect=ValueError("invalid source")):
            with self.assertRaisesRegex(ValueError, "invalid source"):
                fetcher.fetch(self.root, output, archive, DIGEST)
        self.assertFalse(output.exists())
        self.assertEqual(archive.read_bytes(), b"fixture")

    def test_offline_archive_binds_commit_and_external_hash(self):
        archive = self.root / "offline.tar.gz"
        archive.write_bytes(b"fixture")
        output = self.root / "cache"
        def unpack(path, destination, **kwargs):
            destination.mkdir()
            (destination / "manifest.json").write_text("{}")
            self.assertEqual(kwargs, {"expected_sha256": DIGEST, "expected_source_commit": COMMIT, "source_root": self.root})
        with patch.object(fetcher, "extract_release", side_effect=unpack), patch.object(fetcher, "download") as network:
            self.assertEqual(fetcher.fetch(self.root, output, archive, DIGEST), output)
        network.assert_not_called()
        self.assertTrue((output / "manifest.json").is_file())

    def test_no_matching_release_does_not_select_latest_or_public_upstream(self):
        endpoint = self.endpoints()[0]
        output = self.root / "cache"
        with self.github({endpoint: self.reply(b"", exit=1, stderr="HTTP 404")}) as calls:
            with self.assertRaisesRegex(ValueError, "no firmware was selected"):
                fetcher.fetch(self.root, output)
        self.assertEqual([json.loads(line)["args"][-1] for line in calls.read_text().splitlines()], [endpoint])
        self.assertFalse(output.exists())

    def test_offline_requires_hash(self):
        with self.assertRaisesRegex(ValueError, "both"):
            fetcher.fetch(self.root, archive=self.root / "archive")


if __name__ == "__main__":
    unittest.main()
