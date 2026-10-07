"""Portable validation for verified Tab5 releases; never authorizes installation.

The exporter runs the ELF/toolchain-dependent verifier. Consumers must pin the
archive/source and bind the manifest digest in their local installation
authorization before trusting that recorded build evidence.
"""
from __future__ import annotations

import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile

from p4_game_release import development_only
from p4_multiplayer_manifest import PROFILE_HEADER_FLAG, decode_multiplayer_profile

FORMAT = "p4-tab5-release-v1"
FLASH_LAYOUT = {
    "0x2000": "bootloader/bootloader.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0x10000": "ota_data_initial.bin",
    "0x20000": "p4_console_os.bin",
}
FLASH_LIMITS = {"0x2000": 0x6000, "0x8000": 0x1000,
                "0x10000": 0x2000, "0x20000": 0x7f0000}
FEATURES = ("usb_host_enabled", "charger_control_enabled",
            "ble_multiplayer_enabled", "wifi_multiplayer_enabled")
LOCK_PATHS = ("toolchain.lock.json", "apps/console_os/dependencies-tab5.lock")
SOURCE_FIXED = (
    *LOCK_PATHS, "hardware/boards/m5stack-tab5/board-profile.json",
    "hardware/boards/m5stack-tab5/adapter-plan.json",
    "hardware/boards/m5stack-tab5/sdkconfig.defaults",
    "hardware/boards/m5stack-tab5/sdkconfig.ble.defaults",
    "hardware/boards/m5stack-tab5/sdkconfig.no-usb-host.defaults",
    "hardware/boards/m5stack-tab5/PORTING.md",
    "third_party/tab5-bsp.json", "third_party/bmi270/source.json",
    "third_party/bmi270/config.inc", "apps/console_os/app-metadata.json",
    "apps/console_os/CMakeLists.txt", "apps/console_os/partitions-tab5.csv",
    "apps/console_os/sdkconfig.defaults",
    "components/board_deps_tab5/idf_component.yml",
    "components/console_shell/include/console/brand.h", "games/retired.json",
    "scripts/verify-console-os-tab5.py", "scripts/p4_game_release.py",
    "scripts/p4_prebuilt.py", "scripts/package-tab5-release.py",
    "scripts/build.sh", "scripts/lib/project-env.sh", "scripts/build-game-package.py",
    "scripts/build-game-resource.py", "scripts/build-os-update.py", "scripts/p4_multiplayer_manifest.py",
    "scripts/generate-tab5-hosted-sdio-overlay.py", "Makefile",
)
MAX_FILES = 256
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_MANIFEST_BYTES = 512 * 1024
HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")
PACKAGE_RE = re.compile(r"[A-Z0-9][A-Z0-9_-]{0,31}\.P4G\Z")
GAME_KEYS = {"source", "id", "title", "folder", "version", "launcher_id",
             "package_file", "resource_file"}
VERIFICATION_KEYS = {"result", "board", "version", "image_bytes", "image_sha256",
                     "native_cartridges", "content_bundle_verified",
                     "hardware_verified", "flash_authorized", *FEATURES}
MANIFEST_KEYS = {"format", "schema", "board", "target", "version", "source_commit",
                 "source_files", "lock_sha256", "features", "files", "games",
                 "export_verification", "build_evidence", "firmware_only",
                 "content_scope", "wad_included", "hardware_verified", "flash_authorized"}


class ReleaseError(ValueError):
    """An export or portable release failed closed."""


def require(condition, message):
    if not condition:
        raise ReleaseError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_path(name: str) -> pathlib.PurePosixPath:
    require(isinstance(name, str) and name and "\\" not in name and "\0" not in name,
            "unsafe release path")
    relative = pathlib.PurePosixPath(name)
    require(not relative.is_absolute() and relative.as_posix() == name and
            all(part not in ("", ".", "..") for part in relative.parts),
            f"unsafe release path: {name}")
    return relative


def regular_file(root: pathlib.Path, name: str) -> pathlib.Path:
    relative = safe_path(name)
    path = root
    for part in relative.parts:
        path /= part
        require(not path.is_symlink(), f"symlink in release/source path: {name}")
    require(path.is_file(), f"required file missing: {name}")
    return path


def load_json(raw: bytes) -> dict:
    require(len(raw) <= MAX_MANIFEST_BYTES, "release manifest is too large")
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result
    try:
        value = json.loads(raw, object_pairs_hook=pairs,
                           parse_constant=lambda value: (_ for _ in ()).throw(
                               ReleaseError(f"invalid JSON constant: {value}")))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ReleaseError(f"invalid release JSON: {error}") from error
    require(isinstance(value, dict), "release JSON must be an object")
    return value


def source_paths(root: pathlib.Path) -> set[str]:
    """Hash reviewed code/contracts and presentation READMEs, never copy them."""
    require((root / ".git").exists(), "source inventory requires this root's Git checkout")
    try:
        tracked = subprocess.check_output(
            ["git", "-C", str(root), "ls-files", "-z"], text=True).split("\0")
    except subprocess.CalledProcessError as error:
        raise ReleaseError("source inventory requires a Git checkout") from error
    suffixes = {".c", ".h", ".S", ".s", ".cpp", ".cc", ".cxx", ".hpp", ".ld", ".cmake", ".json", ".yml",
                ".yaml", ".lock", ".md", ".inc", ".p4i", ".bin", ".rgb565", ".rgb565a8"}
    paths = set(SOURCE_FIXED)
    for name in tracked:
        path = pathlib.PurePosixPath(name)
        if name.startswith(("apps/", "components/", "games/", "third_party/")) and (
                path.suffix in suffixes or path.name.startswith(("CMakeLists", "Kconfig"))):
            paths.add(name)
    bsp = load_json(regular_file(root, "third_party/tab5-bsp.json").read_bytes())
    for entry in bsp["files"]:
        paths.add(safe_path(entry["path"]).as_posix())
    require(len(paths) <= 4096, "source inventory exceeds release bound")
    return paths


def source_bindings(root: pathlib.Path, *, declared_paths=None) -> dict[str, str]:
    if (root / ".git").exists():
        paths = source_paths(root)
    else:
        # A source-lite archive has a stamp and deliberately has no Git tree.
        # Do not discover a parent checkout when it is unpacked inside one.
        stamp = load_json(regular_file(root, ".p4-source.json").read_bytes())
        require(set(stamp) == {"schema", "source_commit"} and type(stamp["schema"]) is int and
                stamp["schema"] == 1 and isinstance(stamp["source_commit"], str) and
                COMMIT_RE.fullmatch(stamp["source_commit"]), "invalid source archive stamp")
        require(declared_paths is not None, "source archive requires declared source bindings")
        paths = set(declared_paths)
        required = set(SOURCE_FIXED) | {path.relative_to(root).as_posix()
                                      for path in (root / "games").glob("*/game.json")}
        require(required <= paths, "source archive lacks required source/game bindings")
        bsp = load_json(regular_file(root, "third_party/tab5-bsp.json").read_bytes())
        require({entry["path"] for entry in bsp["files"]} <= paths, "source archive lacks vendor bindings")
    return {name: file_sha256(regular_file(root, name)) for name in sorted(paths)}


def standard_games(root: pathlib.Path) -> list[dict]:
    retired = load_json(regular_file(root, "games/retired.json").read_bytes())["games"]
    retired_ids = {entry["id"] for entry in retired}
    retired_packages = {entry["package_file"] for entry in retired}
    retired_launchers = {entry["launcher_id"] for entry in retired}
    games = []
    for path in sorted((root / "games").glob("*/game.json")):
        source = path.relative_to(root).as_posix()
        manifest = load_json(regular_file(root, source).read_bytes())
        if development_only(manifest):
            continue
        entry = {key: manifest[key] for key in GAME_KEYS - {"source", "resource_file"}}
        entry.update(source=source, resource_file=manifest.get("resource_file"))
        validate_game_entry(entry)
        require(entry["id"] not in retired_ids and entry["package_file"] not in retired_packages and
                entry["launcher_id"] not in retired_launchers, "retired game identity in release")
        games.append(entry)
    require(0 < len(games) <= 100, "standard game inventory is empty or too large")
    for key in ("id", "package_file", "launcher_id", "source"):
        require(len({entry[key] for entry in games}) == len(games), f"duplicate game {key}")
    return games


def validate_game_entry(game: dict) -> None:
    require(isinstance(game, dict) and set(game) == GAME_KEYS, "invalid game entry")
    require(isinstance(game["source"], str) and
            re.fullmatch(r"games/[a-z][a-z0-9_]*/game\.json", game["source"]) is not None,
            "invalid game source path")
    require(isinstance(game["package_file"], str) and PACKAGE_RE.fullmatch(game["package_file"]),
            "invalid game package name")
    require(game["resource_file"] in (None, game["package_file"][:-1] + "R"),
            "resource sidecar must match cartridge name")
    for key, width in (("id", 48), ("title", 16), ("folder", 32), ("version", 16)):
        value = game[key]
        require(isinstance(value, str) and 0 < len(value) < width and
                value.isascii() and all(32 <= ord(c) < 127 for c in value),
                f"invalid game {key}")
    require(re.fullmatch(r"[a-z][a-z0-9.-]{2,47}", game["id"]) is not None,
            "invalid game identity")
    require(game["folder"] != "GAMES/WIP", "development game in release")
    require(type(game["launcher_id"]) is int and 100 <= game["launcher_id"] <= 0xffffffff,
            "invalid launcher identity")


def expected_files(games: list[dict]) -> dict[str, tuple[str, int]]:
    files = {"firmware/" + name: ("firmware", FLASH_LIMITS[offset])
             for offset, name in FLASH_LAYOUT.items()}
    files["firmware/P4UPDATE.P4U"] = ("update", 0x7f0000 + 256)
    for game in games:
        files["content/GAMES/" + game["package_file"]] = ("game", 512 * 1024)
        if game["resource_file"]:
            files["content/GAMES/" + game["resource_file"]] = ("resource", 8 * 1024 * 1024)
    return files


def c_string(data: bytes, label: str, *, allow_empty: bool = False) -> str:
    require(b"\0" in data, f"{label} is not terminated")
    value, padding = data.split(b"\0", 1)
    require((value or allow_empty) and not any(padding) and
            all(32 <= character < 127 for character in value), f"{label} padding/text differs")
    try:
        return value.decode("ascii")
    except UnicodeError as error:
        raise ReleaseError(f"{label} is not ASCII") from error


def validate_binaries(data: dict[str, bytes], manifest: dict) -> None:
    for offset in ("0x2000", "0x20000"):
        image = data["firmware/" + FLASH_LAYOUT[offset]]
        require(len(image) > 24 and image[0] == 0xe9 and
                struct.unpack_from("<H", image, 12)[0] == 18 and
                struct.unpack_from("<HH", image, 15) == (100, 199),
                "firmware chip/revision family differs")
    partition = data["firmware/partition_table/partition-table.bin"]
    require(len(partition) % 32 == 0, "partition table is truncated")
    slots = []
    for pos in range(0, len(partition), 32):
        magic, kind, subtype, start, size = struct.unpack_from("<HBBII", partition, pos)
        if magic == 0x50aa and kind == 0:
            slots.append((subtype, start, size))
    require(slots == [(0x10, 0x20000, 0x7f0000), (0x11, 0x810000, 0x7f0000)],
            "binary OTA layout differs")
    image = data["firmware/p4_console_os.bin"]
    update = data["firmware/P4UPDATE.P4U"]
    require(len(update) >= 256 and update[:8] == b"P4OSUP1\0" and
            struct.unpack_from("<6I", update, 8) == (256, len(update), 256, len(image), 1, 0) and
            update[256:] == image and update[32:64] == hashlib.sha256(image).digest(),
            "update does not bind the released app")
    require(update[160:176] == b"esp32p4-tab5" + bytes(4) and
            c_string(update[64:96], "update version") == manifest["version"] and
            not any(update[176:256]), "update board/version/reserved fields differ")
    require(c_string(update[96:160], "update build") == manifest["source_commit"][:12],
            "update build differs from source commit")
    for game in manifest["games"]:
        package = data["content/GAMES/" + game["package_file"]]
        require(len(package) > 256 and package[:8] == b"P4GAME1\0" and
                struct.unpack_from("<4I", package, 8) == (256, len(package), 256, len(package)-256) and
                struct.unpack_from("<3I", package, 24) == (1, 1, game["launcher_id"]) and
                package[48:80] == hashlib.sha256(package[256:]).digest(),
                f"corrupt native cartridge: {game['package_file']}")
        required_caps, optional_caps = struct.unpack_from("<II", package, 36)
        flags = struct.unpack_from("<H", package, 46)[0]
        require(required_caps & 1 and not ((required_caps | optional_caps) & ~0x7fff) and
                not (required_caps & optional_caps) and not (flags & ~3),
                "cartridge capabilities or flags differ from runtime contract")
        c_string(package[144:176], "game subtitle", allow_empty=True)
        c_string(package[224:240], "game license")
        if flags & PROFILE_HEADER_FLAG:
            require((required_caps | optional_caps) & (1 << 9),
                    "cartridge multiplayer profile lacks capability")
            try:
                decode_multiplayer_profile(package[240:256])
            except ValueError as error:
                raise ReleaseError(f"invalid cartridge multiplayer profile: {error}") from error
        else:
            require(not any(package[240:256]), "cartridge reserved profile bytes differ")
        for pos, width, key in ((80, 48, "id"), (128, 16, "title"),
                                (176, 32, "folder"), (208, 16, "version")):
            require(c_string(package[pos:pos+width], "game " + key) == game[key],
                    f"cartridge {key} differs from inventory")
        if game["resource_file"]:
            resource = data["content/GAMES/" + game["resource_file"]]
            require(len(resource) > 128 and resource[:8] == b"P4RES01\0" and
                    struct.unpack_from("<6I", resource, 8) ==
                    (128, len(resource), 128, len(resource)-128, 1, 0) and
                    resource[32:64] == hashlib.sha256(resource[128:]).digest() and
                    c_string(resource[64:112], "resource game ID") == game["id"] and
                    not any(resource[112:128]), "resource sidecar identity or payload differs")


def validate_manifest(manifest: dict) -> dict[str, tuple[str, int]]:
    require(set(manifest) == MANIFEST_KEYS, "unknown or missing release manifest field")
    require(manifest["format"] == FORMAT and type(manifest["schema"]) is int and
            manifest["schema"] == 1 and manifest["board"] == "m5stack-tab5" and
            manifest["target"] == "esp32p4-tab5", "wrong release format/board")
    require(isinstance(manifest["source_commit"], str) and COMMIT_RE.fullmatch(manifest["source_commit"]),
            "invalid source commit")
    require(isinstance(manifest["version"], str) and
            re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", manifest["version"]), "invalid OS version")
    require(manifest["hardware_verified"] is False and manifest["flash_authorized"] is False and
            manifest["firmware_only"] is False and manifest["wad_included"] is False and
            manifest["content_scope"] == "standard-native-only", "release cannot grant hardware/install authority")
    features = manifest["features"]
    require(isinstance(features, dict) and set(features) == set(FEATURES) and
            all(type(value) is bool for value in features.values()) and
            features["ble_multiplayer_enabled"] == features["wifi_multiplayer_enabled"],
            "invalid release feature selection")
    games = manifest["games"]
    require(isinstance(games, list) and 0 < len(games) <= 100, "invalid standard game inventory")
    for game in games:
        validate_game_entry(game)
    for key in ("source", "id", "package_file", "launcher_id"):
        require(len({game[key] for game in games}) == len(games), f"duplicate game {key}")
    files = expected_files(games)
    require(isinstance(manifest["files"], dict) and set(manifest["files"]) == set(files),
            "release file inventory differs from runtime allowlist")
    total = 0
    for name, (role, limit) in files.items():
        entry = manifest["files"][name]
        require(isinstance(entry, dict) and set(entry) == {"bytes", "sha256", "role"} and
                type(entry["bytes"]) is int and 0 < entry["bytes"] <= limit and
                isinstance(entry["sha256"], str) and HASH_RE.fullmatch(entry["sha256"]) and
                entry["role"] == role, f"invalid file binding: {name}")
        total += entry["bytes"]
    require(len(files) + 1 <= MAX_FILES and total <= MAX_TOTAL_BYTES, "release exceeds runtime bounds")
    source = manifest["source_files"]
    require(isinstance(source, dict) and 0 < len(source) <= 4096 and set(SOURCE_FIXED) <= set(source),
            "missing reviewed source bindings")
    for name, digest in source.items():
        safe_path(name)
        require(isinstance(digest, str) and HASH_RE.fullmatch(digest), "invalid source digest")
    require(manifest["lock_sha256"] == {name: source[name] for name in LOCK_PATHS},
            "authoritative lock bindings differ")
    evidence = manifest["build_evidence"]
    require(isinstance(evidence, dict) and set(evidence) ==
            {"sdkconfig", "project_description.json", "compile_commands.json", "app_elf"} and
            all(isinstance(value, str) and HASH_RE.fullmatch(value) for value in evidence.values()),
            "missing full-build evidence hashes")
    verified = manifest["export_verification"]
    app = manifest["files"]["firmware/p4_console_os.bin"]
    require(isinstance(verified, dict) and set(verified) == VERIFICATION_KEYS and
            verified["result"] == "tab5-build-candidate-verified" and
            verified["board"] == manifest["board"] and verified["version"] == manifest["version"] and
            verified["hardware_verified"] is False and verified["flash_authorized"] is False and
            verified["content_bundle_verified"] is True and
            type(verified["native_cartridges"]) is int and verified["native_cartridges"] == len(games) and
            type(verified["image_bytes"]) is int and verified["image_bytes"] == app["bytes"] and
            verified["image_sha256"] == app["sha256"] and
            all(type(verified[key]) is bool and verified[key] == features[key] for key in FEATURES),
            "export verifier evidence differs from portable artifacts/features")
    return files


def validate_release(directory: pathlib.Path, *, expected_source_commit: str | None = None,
                     source_root: pathlib.Path | None = None,
                     expected_manifest_sha256: str | None = None) -> dict:
    """Recheck portable evidence without SDK/ELF; no hardware or auth side effects."""
    directory = pathlib.Path(directory)
    require(directory.is_dir() and not directory.is_symlink(), "release directory is missing or linked")
    manifest_path = regular_file(directory, "manifest.json")
    require(manifest_path.stat().st_size <= MAX_MANIFEST_BYTES, "release manifest is too large")
    raw = manifest_path.read_bytes()
    if expected_manifest_sha256 is not None:
        require(isinstance(expected_manifest_sha256, str) and HASH_RE.fullmatch(expected_manifest_sha256) and
                sha256(raw) == expected_manifest_sha256,
                "release manifest digest differs")
    manifest = load_json(raw)
    files = validate_manifest(manifest)
    if expected_source_commit is not None:
        require(isinstance(expected_source_commit, str) and COMMIT_RE.fullmatch(expected_source_commit) and
                manifest["source_commit"] == expected_source_commit, "release source commit differs")
    actual = set()
    allowed_dirs = {parent.as_posix() for name in {"manifest.json", *files}
                    for parent in pathlib.PurePosixPath(name).parents if parent.as_posix() != "."}
    for path in directory.rglob("*"):
        name = path.relative_to(directory).as_posix()
        require(not path.is_symlink(), f"linked release member: {name}")
        if path.is_dir():
            require(name in allowed_dirs, f"unexpected release directory: {name}")
        else:
            require(path.is_file(), f"nonregular release member: {name}")
            actual.add(name)
    require(actual == {"manifest.json", *files}, "release contains missing or unexpected files")
    data = {}
    for name in files:
        path = regular_file(directory, name)
        entry = manifest["files"][name]
        require(path.stat().st_size == entry["bytes"], f"released file size differs: {name}")
        data[name] = path.read_bytes()
        require(sha256(data[name]) == entry["sha256"], f"released file digest differs: {name}")
    validate_binaries(data, manifest)
    if source_root is not None:
        source_root = pathlib.Path(source_root)
        if not (source_root / ".git").exists():
            stamp = load_json(regular_file(source_root, ".p4-source.json").read_bytes())
            require(stamp.get("source_commit") == manifest["source_commit"], "source archive commit differs")
        require(source_bindings(source_root, declared_paths=manifest["source_files"]) == manifest["source_files"],
                "reviewed source/lock bindings differ")
        require(standard_games(source_root) == manifest["games"], "standard game inventory differs from source")
    return {"manifest": manifest, "manifest_sha256": sha256(raw),
            "verification": manifest["export_verification"],
            "build_dir": directory / "firmware", "content_dir": directory / "content"}


def extract_release(archive: pathlib.Path, destination: pathlib.Path, *, expected_sha256: str,
                    expected_source_commit: str | None = None,
                    source_root: pathlib.Path | None = None) -> dict:
    """Check an externally pinned tar, safely stage it, validate and publish once."""
    archive, destination = pathlib.Path(archive), pathlib.Path(destination)
    require(isinstance(expected_sha256, str) and HASH_RE.fullmatch(expected_sha256),
            "expected archive digest is required")
    require(archive.is_file() and not archive.is_symlink() and
            archive.stat().st_size <= MAX_TOTAL_BYTES, "invalid or oversized release archive")
    require(file_sha256(archive) == expected_sha256, "release archive digest differs")
    require(not destination.exists() and not destination.is_symlink(), "release destination already exists")
    require(destination.parent.is_dir() and not destination.parent.is_symlink(), "unsafe release destination parent")
    temporary = pathlib.Path(tempfile.mkdtemp(prefix=".tab5-release-", dir=destination.parent))
    try:
        seen, total = set(), 0
        with tarfile.open(archive, "r|gz") as tar:
            for member in tar:
                name = safe_path(member.name).as_posix()
                require(name not in seen, "duplicate archive member")
                require(member.isfile() and not member.issparse() and not member.pax_headers,
                        "archive contains nonregular/sparse/extended member")
                require(name == "manifest.json" or name in
                        {"firmware/" + path for path in FLASH_LAYOUT.values()} or
                        name == "firmware/P4UPDATE.P4U" or
                        re.fullmatch(r"content/GAMES/[A-Z0-9][A-Z0-9_-]{0,31}\.P4[GR]", name),
                        "archive member is outside runtime allowlist")
                limit = MAX_MANIFEST_BYTES if name == "manifest.json" else (
                    8 * 1024 * 1024 if name.endswith(".P4R") else (
                    512 * 1024 if name.endswith(".P4G") else 0x7f0000 + 256))
                require(0 < member.size <= limit, "archive member exceeds size bound")
                seen.add(name)
                total += member.size
                require(len(seen) <= MAX_FILES and total <= MAX_TOTAL_BYTES, "archive exceeds release bounds")
                stream = tar.extractfile(member)
                require(stream is not None, "archive member is unreadable")
                path = temporary.joinpath(*safe_path(name).parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb") as output:
                    shutil.copyfileobj(stream, output, 1024 * 1024)
                path.chmod(0o644)
        result = validate_release(temporary, expected_source_commit=expected_source_commit,
                                  source_root=source_root)
        temporary.rename(destination)
        result["build_dir"] = destination / "firmware"
        result["content_dir"] = destination / "content"
        return result
    except (tarfile.TarError, EOFError, struct.error) as error:
        raise ReleaseError(f"invalid release archive: {error}") from error
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
