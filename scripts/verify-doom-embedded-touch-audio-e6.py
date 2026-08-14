#!/usr/bin/env python3
"""Exact build/preflash verifier for the bound-unit E6 sound successor."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import re
import struct
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/doom_embedded_touch_audio"
EVIDENCE = ROOT / "test-runs/2026-08-13-doom-embedded-touch-audio-e6-sound-build.json"
AUTH = ROOT / "hardware/evidence/doom-embedded-touch-audio-e6-factory-audio-authorization.json"
RELEASE = ROOT / "hardware/evidence/doom-embedded-touch-audio-e6-exact-unit-audio-release.json"
ROLLBACK = ROOT / "test-runs/doom-e6-rollback-2026-08-13/rollback-bundle.json"
INSTALLER = ROOT / "scripts/doom-e6-install.py"
ROUTE = ROOT / "scripts/doom-e6-authorized-route.py"

EXPECTED_DEVICE_SHA256 = "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
EXPECTED_WAD_BYTES = 4_196_020
EXPECTED_WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
EXPECTED_INSTALLER_SHA256 = "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471"
EXPECTED_ROLLBACK_SHA256 = "00a670a6c1034768a8ccbcb53ef0ea9c18bbd604905b5fa8846994ee5a15949b"
EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 24 * 1024, 0),
    ("phy_init", 1, 1, 0xF000, 4 * 1024, 0),
    ("factory", 0, 0, 0x10000, 11 * 1024 * 1024, 0),
    ("storage", 1, 0x82, 0xB10000, 4 * 1024 * 1024, 0),
]
REQUIRED_COMPONENTS = {
    "doom_audio", "doom_engine_audio", "doom_touch_input", "doom_video",
    "platform_audio", "platform_audio_factory", "platform_display",
    "platform_i2c_shared", "platform_readonly_blob", "platform_touch",
}
FORBIDDEN_COMPONENTS = {
    "doom_gamepad_input", "platform_audio_es8311", "platform_gamepad_usb",
    "platform_usb_host", "usb_host_hid", "espressif__usb_host_hid",
    "espressif__usb", "esp_codec_dev", "espressif__esp_codec_dev",
}
REQUIRED_SYMBOLS = {
    "app_main", "doomgeneric_Create", "doomgeneric_Tick", "DG_GetKey",
    "doom_video_submit_xrgb8888", "platform_display_init",
    "platform_i2c_shared_create", "platform_i2c_shared_destroy",
    "platform_touch_create", "platform_touch_poll", "platform_touch_destroy",
    "doom_touch_input_update", "doom_touch_overlay_render_xrgb8888",
    "platform_audio_factory_create", "platform_audio_factory_start",
    "platform_audio_factory_write_frames", "platform_audio_factory_get_telemetry",
    "platform_audio_force_safe_shutdown", "platform_audio_invocation_count",
    "doom_music_song_create", "doom_music_player_mix",
    "doom_audio_runtime_music_play", "doom_audio_runtime_music_set_volume",
    "s_composite_authorized", "s_touch_authorized", "s_audio_authorized",
    "_binary_doom_shareware_wad_start", "_binary_doom_shareware_wad_end",
}
FORBIDDEN_SYMBOLS = {
    "usb_host_install", "hid_host_install", "platform_usb_host_start",
    "platform_gamepad_usb_start", "esp_vfs_fat_sdmmc_mount",
    "esp_codec_dev_new", "esp_codec_dev_open", "es8311_codec_new",
}
FACTORY_ENTRYPOINTS = {
    "platform_audio_factory_force_safe_shutdown",
    "platform_audio_factory_recover",
    "platform_audio_factory_create",
    "platform_audio_factory_start",
    "platform_audio_factory_write_frames",
    "platform_audio_factory_stop",
    "platform_audio_factory_get_state",
    "platform_audio_factory_get_telemetry",
    "platform_audio_factory_destroy",
}
SOURCE_PATHS = {
    ".agents/skills/develop-esp32-p4-platform/references/elecrow-10-in-variant.md",
    ".agents/skills/use-elecrow-p4-audio/SKILL.md",
    ".agents/skills/use-elecrow-p4-audio/references/factory-audio-contract.md",
    "apps/doom_embedded_touch_audio/CMakeLists.txt",
    "apps/doom_embedded_touch_audio/README.md",
    "apps/doom_embedded_touch_audio/app-metadata.json",
    "apps/doom_embedded_touch_audio/architecture-inventory.json",
    "apps/doom_embedded_touch_audio/components/platform_audio/CMakeLists.txt",
    "apps/doom_embedded_touch_audio/components/platform_audio/include/platform/audio.h",
    "apps/doom_embedded_touch_audio/components/platform_audio/src/platform_audio_adapter.c",
    "apps/doom_embedded_touch_audio/dependencies.lock",
    "apps/doom_embedded_touch_audio/main/CMakeLists.txt",
    "apps/doom_embedded_touch_audio/main/audio_lifecycle.c",
    "apps/doom_embedded_touch_audio/main/audio_lifecycle.h",
    "apps/doom_embedded_touch_audio/main/degraded_policy.c",
    "apps/doom_embedded_touch_audio/main/degraded_policy.h",
    "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c",
    "apps/doom_embedded_touch_audio/main/runtime_gate.c",
    "apps/doom_embedded_touch_audio/main/runtime_gate.h",
    "apps/doom_embedded_touch_audio/main/touch_controls.c",
    "apps/doom_embedded_touch_audio/main/touch_controls.h",
    "apps/doom_embedded_touch_audio/partitions.csv",
    "apps/doom_embedded_touch_audio/sdkconfig.defaults",
    "apps/doom_embedded_touch_audio/tests/test_degraded_policy.c",
    "apps/doom_embedded_touch_audio/tests/test_source_contract.py",
    "apps/doom/components/doom_audio/CMakeLists.txt",
    "apps/doom/components/doom_audio/Kconfig",
    "apps/doom/components/doom_audio/include/doom/audio_mixer.h",
    "apps/doom/components/doom_audio/include/doom/audio_ring.h",
    "apps/doom/components/doom_audio/include/doom/audio_runtime.h",
    "apps/doom/components/doom_audio/include/doom/music_synth.h",
    "apps/doom/components/doom_audio/src/doom_audio_mixer.c",
    "apps/doom/components/doom_audio/src/doom_audio_ring.c",
    "apps/doom/components/doom_audio/src/doom_audio_runtime.c",
    "apps/doom/components/doom_audio/src/doom_audio_sound_module.c",
    "apps/doom/components/doom_audio/src/doom_music_synth.c",
    "apps/doom/components/doom_audio/tests/test_doom_audio.c",
    "apps/doom/components/doom_audio/tests/test_doom_audio_runtime.c",
    "apps/doom/components/doom_audio/tests/test_doom_audio_wad.c",
    "apps/doom_audio_probe/components/doom_engine_audio/CMakeLists.txt",
    "apps/doom_audio_probe/components/doom_engine_audio/doom_i_sound_feature.c",
    "components/doom_touch_input/src/input.c",
    "components/doom_touch_input/src/overlay.c",
    "components/doom_video/src/doom_video_convert.c",
    "components/doom_video/src/doom_video_espidf.c",
    "components/platform_audio_factory/include/platform_audio_factory/audio.h",
    "components/platform_audio_factory/src/platform_audio_factory.c",
    "components/platform_audio_factory/src/platform_audio_factory_policy.c",
    "components/platform_display/src/platform_display.c",
    "components/platform_i2c_shared/src/bus.c",
    "components/platform_readonly_blob/src/platform_readonly_blob_vfs.c",
    "components/platform_readonly_blob/src/readonly_blob_core.c",
    "components/platform_touch/src/platform_touch.c",
    "docs/DOOM.md",
    "docs/DOOM_MUSIC_TEST.md",
    "docs/HARDWARE.md",
    "hardware/board-profile.json",
    "hardware/evidence/elecrow-10.1-factory-audio-semantics.json",
    "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json",
    "scripts/capture-doom-e6-runtime.py",
    "scripts/doom-e5-install.py",
    "scripts/doom-e6-install.py",
    "scripts/gamepad-diag-restore.py",
    "scripts/tests/test-doom-e6-install.py",
    "scripts/tests/test-doom-e6-authorized-route.py",
    "scripts/tests/test-doom-e6-runtime-capture.py",
    "scripts/verify-doom-embedded-touch-audio.py",
    "scripts/verify-doom-embedded-touch-audio-e6.py",
    "third_party/doomgeneric.git-tree",
    "third_party/source-lock.json",
    "toolchain.lock.json",
}


def fail(message: str) -> None:
    raise SystemExit(f"doom_embedded_touch_audio E6 verification failed: {message}")


def require(value: bool, message: str) -> None:
    if not value:
        fail(message)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def checked_child(build: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    path = (build / relative).resolve()
    require(path.is_relative_to(build) and path.is_file(), f"invalid {label}: {path}")
    return path


def exact_file(path: pathlib.Path, record: dict, prefix: str) -> None:
    require(path.stat().st_size == record.get(f"{prefix}_bytes"), f"wrong {prefix} bytes")
    require(sha256(path) == record.get(f"{prefix}_sha256"), f"wrong {prefix} hash")


def partition_entries(path: pathlib.Path) -> list[tuple[str, int, int, int, int, int]]:
    entries = []
    data = path.read_bytes()
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            break
        magic, kind, subtype, part_offset, size, name, flags = struct.unpack(
            "<HBBII16sI", entry
        )
        if magic == 0xEBEB:
            break
        require(magic == 0x50AA, f"invalid partition entry at {offset}")
        entries.append((
            name.split(b"\0", 1)[0].decode("ascii"), kind, subtype,
            part_offset, size, flags,
        ))
    return entries


def cmake_tool(build: pathlib.Path, variable: str) -> pathlib.Path:
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(rf"^{re.escape(variable)}:FILEPATH=(.+)$", cache, re.MULTILINE)
    require(match is not None, f"{variable} missing from CMake cache")
    tool = pathlib.Path(match.group(1))
    require(tool.is_file(), f"configured tool missing: {tool}")
    return tool


def symbols(build: pathlib.Path, elf: pathlib.Path) -> dict[str, int]:
    result = subprocess.run(
        [str(cmake_tool(build, "CMAKE_NM")), "-a", str(elf)],
        check=True, capture_output=True, text=True,
    )
    found: dict[str, int] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            found[fields[-1]] = int(fields[0], 16)
    return found


def rodata_byte(build: pathlib.Path, elf: pathlib.Path, address: int) -> int:
    result = subprocess.run(
        [str(cmake_tool(build, "CMAKE_OBJDUMP")), "-s",
         f"--start-address={address}", f"--stop-address={address + 1}", str(elf)],
        check=True, capture_output=True, text=True,
    )
    for line in result.stdout.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]{2,})", line)
        if match and int(match.group(1), 16) == address:
            return int(match.group(2)[:2], 16)
    fail(f"cannot extract gate byte at {address:#x}")


def verify_factory_callers(build: pathlib.Path, description: dict) -> None:
    elf_relative = description.get("app_elf")
    require(isinstance(elf_relative, str) and elf_relative.endswith(".elf"), "bad ELF name")
    lines = checked_child(build, elf_relative[:-4] + ".map", "linker map").read_text(
        encoding="utf-8"
    ).splitlines()
    try:
        start = lines.index("Cross Reference Table") + 3
    except ValueError:
        fail("linker cross-reference table missing")
    references: dict[str, list[str]] = {}
    current: str | None = None
    for line in lines[start:]:
        if not line.strip():
            continue
        if line[0].isspace():
            if current is not None:
                references[current].append(line.strip())
            continue
        fields = line.split(None, 1)
        if len(fields) != 2:
            continue
        current = fields[0]
        references[current] = [fields[1].strip()]
    definition = (
        "esp-idf/platform_audio_factory/libplatform_audio_factory.a("
        "platform_audio_factory.c.obj)"
    )
    caller = (
        "esp-idf/platform_audio/libplatform_audio.a("
        "platform_audio_adapter.c.obj)"
    )
    for symbol in FACTORY_ENTRYPOINTS:
        require(references.get(symbol) == [definition, caller],
                f"factory entry point has an uncounted caller: {symbol}")


def verify_policy() -> None:
    metadata = load_json(APP / "app-metadata.json")
    require(metadata.get("app") == "doom_embedded_touch_audio", "wrong metadata app")
    require(metadata.get("stage") == "E6-sound-successor-build-only-electrical-release-pending", "wrong E6 stage")
    require(metadata.get("runtime_supported") is False, "generic runtime unexpectedly enabled")
    require(metadata.get("touch_runtime_authorized") is True, "touch runtime disabled")
    require(metadata.get("audio_runtime_authorized") is False, "global audio authorization changed")
    require(metadata.get("flash_app_authorized") is False, "generic app flash route enabled")
    require(metadata.get("flash_project_authorized") is False, "project flash route enabled")
    require(metadata.get("game_data_redistribution_authorized") is False, "WAD redistribution enabled")
    require(metadata.get("build_evidence") == str(EVIDENCE.relative_to(ROOT)), "wrong build evidence path")
    require(metadata.get("successor_mode", {}).get("required_exact_gates") == {
        "composite": 1, "touch": 1, "audio": 1,
    }, "wrong E6 gate contract")
    rollback = metadata.get("installed_predecessor", {}).get("private_rollback_bundle", {})
    require(rollback.get("manifest", {}).get("sha256") == EXPECTED_ROLLBACK_SHA256,
            "metadata rollback manifest binding differs")
    require(metadata.get("hardware_interfaces_explicitly_absent", []).count("usb_host") == 1,
            "USB Host absence contract differs")
    audio_format = metadata.get("successor_mode", {}).get("audio_format", {})
    require(audio_format.get("backend_volume_step") == "6/10",
            "quieter backend volume differs")
    require(audio_format.get("gain") == "attenuated-60-percent-linear-pcm",
            "final attenuation contract differs")
    require(audio_format.get("music_enabled") is True,
            "WAD music is not enabled")
    require(audio_format.get("music_timing_hz") == 140,
            "MUS timing differs")
    require(audio_format.get("external_midi_hardware_required") is False,
            "external MIDI hardware dependency introduced")
    require(audio_format.get("soundfont_required") is False,
            "SoundFont dependency introduced")
    require(audio_format.get("bit_exact_opl_claimed") is False,
            "unsupported OPL equivalence claim introduced")
    require(metadata.get("successor_mode", {}).get("doom_arguments") == [],
            "sound-enabled launch still disables music")


def verify_authorization(evidence: dict) -> None:
    require(AUTH.is_file() and RELEASE.is_file() and ROLLBACK.is_file(),
            "E6 release records are incomplete")
    auth = load_json(AUTH)
    require(auth.get("schema") == 1 and auth.get("active") is True, "authorization inactive")
    require(auth.get("scope") == "exact-unit-e6-factory-audio", "authorization scope differs")
    require(auth.get("gates") == {"composite": 1, "touch": 1, "audio": 1}, "authorization gates differ")
    require(auth.get("device_binding", {}).get("identity_sha256") == EXPECTED_DEVICE_SHA256,
            "authorization device differs")
    require(auth.get("exact_artifact") == evidence.get("exact_artifact"),
            "authorization artifact differs")
    require(auth.get("build_evidence") == {
        "path": str(EVIDENCE.relative_to(ROOT)), "sha256": sha256(EVIDENCE),
    }, "authorization build evidence differs")
    require(auth.get("exact_unit_audio_release") == {
        "path": str(RELEASE.relative_to(ROOT)), "sha256": sha256(RELEASE),
    }, "authorization release binding differs")
    require(auth.get("installed_predecessor_rollback") == {
        "path": str(ROLLBACK.relative_to(ROOT)), "sha256": EXPECTED_ROLLBACK_SHA256,
    }, "authorization rollback binding differs")
    require(sha256(INSTALLER) == EXPECTED_INSTALLER_SHA256, "frozen installer changed")
    route = ROUTE.read_text(encoding="utf-8")
    issued = re.search(r'^ISSUED_AUTH_SHA256 = "([0-9a-f]{64})"$', route, re.MULTILINE)
    require(issued is not None and issued.group(1) == sha256(AUTH),
            "outer issuance digest does not select this authorization")
    spec = importlib.util.spec_from_file_location("doom_e6_preflash_contract", INSTALLER)
    require(spec is not None and spec.loader is not None, "cannot load frozen installer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    contract, _ = module._contract_from_authorization(AUTH, sha256(AUTH))
    require(contract.artifact_sha256 == evidence["exact_artifact"]["app_binary_sha256"],
            "installer contract artifact differs")


def main(argv: list[str] | None = None) -> None:
    args = sys.argv if argv is None else argv
    if len(args) != 3:
        fail("usage: verify-doom-embedded-touch-audio.py BUILD_DIR build-only|app-flash")
    build = pathlib.Path(args[1]).resolve()
    mode = args[2]
    require(mode in {"build-only", "app-flash"}, "unsupported E6 verifier mode")
    require(build.is_dir(), "build directory missing")
    verify_policy()

    evidence = load_json(EVIDENCE)
    require(evidence.get("schema") == 1, "evidence schema differs")
    require(evidence.get("classification") == "build-tested-exact-unit-e6-sfx-mus-release-candidate",
            "evidence classification differs")
    require(evidence.get("result") == "pass-focused-host-tests-build-and-artifact-audit",
            "evidence result differs")
    tests = evidence.get("focused_tests", {})
    require(all(tests.get(name) == "pass" for name in (
        "doom_audio_host", "platform_audio_factory_host", "doom_touch_audio_host",
        "e6_runtime_capture", "e6_installer", "e6_authorized_route",
    )), "focused test record differs")
    inventory = evidence.get("source_inventory")
    require(isinstance(inventory, dict) and set(inventory) == SOURCE_PATHS,
            "critical source inventory is not exact")
    for relative, expected in inventory.items():
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT) and path.is_file(), f"invalid source: {relative}")
        require(sha256(path) == expected, f"source changed: {relative}")

    description = load_json(build / "project_description.json")
    require(description.get("project_name") == "p4_doom_embedded_touch_audio", "wrong project")
    require(description.get("target") == "esp32p4", "wrong target")
    require(int(description.get("min_rev")) == 100 and int(description.get("max_rev")) == 199,
            "wrong silicon family")
    components = set(description.get("build_components", []))
    require(REQUIRED_COMPONENTS <= components, "required component missing")
    require(not (FORBIDDEN_COMPONENTS & components), "forbidden component built")
    info = description.get("build_component_info", {})
    require(pathlib.Path(info["platform_audio"]["dir"]).resolve() ==
            (APP / "components/platform_audio").resolve(), "wrong app audio adapter")
    require(pathlib.Path(info["platform_audio_factory"]["dir"]).resolve() ==
            (ROOT / "components/platform_audio_factory").resolve(), "wrong factory backend")

    app = checked_child(build, description.get("app_bin"), "application")
    elf = checked_child(build, description.get("app_elf"), "ELF")
    bootloader = checked_child(build, "bootloader/bootloader.bin", "bootloader")
    partition = checked_child(build, "partition_table/partition-table.bin", "partition table")
    artifact = evidence.get("exact_artifact", {})
    require(artifact.get("offset") == "0x10000", "artifact offset differs")
    exact_file(app, artifact, "app_binary")
    exact_file(elf, artifact, "elf")
    exact_file(bootloader, artifact, "bootloader")
    exact_file(partition, artifact, "partition_table")
    require(artifact.get("mutation_span_bytes") ==
            ((app.stat().st_size + 16383) // 16384) * 16384,
            "mutation span geometry differs")
    require(artifact.get("mutation_tail_byte") == "ff", "mutation tail differs")
    require(partition_entries(partition) == EXPECTED_PARTITIONS, "partition table changed")

    config = load_json(build / "config/sdkconfig.json")
    resolved = evidence.get("resolved_config")
    require(isinstance(resolved, dict) and resolved, "resolved config missing")
    for key, expected in resolved.items():
        require(config.get(key) == expected, f"config changed: {key}")

    found = symbols(build, elf)
    require(REQUIRED_SYMBOLS <= set(found), "required linked symbol missing")
    require(not (FORBIDDEN_SYMBOLS & set(found)), "forbidden linked symbol present")
    verify_factory_callers(build, description)
    require(rodata_byte(build, elf, found["s_composite_authorized"]) == 1,
            "composite gate is not 1")
    require(rodata_byte(build, elf, found["s_touch_authorized"]) == 1,
            "touch gate is not 1")
    require(rodata_byte(build, elf, found["s_audio_authorized"]) == 1,
            "audio gate is not 1")

    binary = app.read_bytes()
    wad = (ROOT / "local-data/doom/doom1.wad").read_bytes()
    require(len(wad) == EXPECTED_WAD_BYTES and hashlib.sha256(wad).hexdigest() == EXPECTED_WAD_SHA256,
            "local WAD changed")
    require(binary.count(wad) == 1, "application must embed one exact WAD")
    for marker in (
        b"P4_DOOM_E6 START input=gt911-multitouch sound=factory-complete-i2s0-pdm-rx-i2s1-speaker-tx-sfx-mus",
        b"P4_DOOM_E6 SOUND_BOUND backend=factory-complete-audio-init",
        b"P4_DOOM_E6 SOUND_READY state=running",
        b"P4_DOOM_E6 STATS frames=%",
        b"music=wad-mus-procedural-16voice",
        b"music_pipeline=wad-mus-procedural-16voice",
    ):
        require(binary.count(marker) == 1, f"missing exact runtime marker: {marker!r}")
    require(binary.count(b"codec_i2c_transactions=0") >= 2, "codec-I2C absence proof missing")
    require(binary.count(b"usb=absent") >= 2, "USB-runtime absence proof missing")

    if mode == "app-flash":
        verify_authorization(evidence)
    print(
        f"P4_DOOM_E6 VERIFY PASS mode={mode} bytes={app.stat().st_size} "
        f"sha256={sha256(app)} gates=1/1/1 usb=absent audio=sfx-mus-factory-complete"
    )


if __name__ == "__main__":
    main()
