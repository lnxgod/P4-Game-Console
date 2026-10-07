#!/usr/bin/env python3
"""Exact-revision downloads fail closed and preserve existing local build outputs."""
import importlib.util
import io
import json
import pathlib
import subprocess
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

    def response(self, payload=b"abc", url="https://release-assets.githubusercontent.com/a", length="3"):
        response = Mock()
        response.geturl.return_value = url
        response.headers = {"Content-Length": length} if length is not None else {}
        response.read.side_effect = [payload, b""]
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        return response

    def test_download_is_bounded_and_github_https_only(self):
        for url, length, payload in (("http://github.com/a", "3", b"abc"),
                                     ("https://example.org/a", "3", b"abc"),
                                     ("https://github.com/a", "100", b"abc"),
                                     ("https://github.com/a", None, b"12345")):
            with self.subTest(url=url, length=length), patch.object(fetcher.urllib.request, "urlopen", return_value=self.response(payload, url, length)):
                target = self.root / "download"
                if target.exists(): target.unlink()
                with self.assertRaises(ValueError):
                    fetcher.download("https://github.com/a", target, 4)
        target = self.root / "valid"
        with patch.object(fetcher.urllib.request, "urlopen", return_value=self.response()):
            fetcher.download("https://github.com/a", target, 4)
        self.assertEqual(target.read_bytes(), b"abc")

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

    def test_no_matching_release_does_not_select_latest(self):
        error = fetcher.urllib.error.HTTPError("url", 404, "missing", {}, None)
        output = self.root / "cache"
        with patch.object(fetcher, "download", side_effect=error) as download:
            with self.assertRaisesRegex(ValueError, "No prebuilt Tab5 release"):
                fetcher.fetch(self.root, output)
        self.assertIn(f"releases/download/tab5-{COMMIT}/SHA256SUMS", download.call_args.args[0])
        self.assertFalse(output.exists())

    def test_offline_requires_hash(self):
        with self.assertRaisesRegex(ValueError, "both"):
            fetcher.fetch(self.root, archive=self.root / "archive")


if __name__ == "__main__":
    unittest.main()
