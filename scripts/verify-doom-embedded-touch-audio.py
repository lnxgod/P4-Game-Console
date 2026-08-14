#!/usr/bin/env python3
"""Exact build/preflash verifier for the touch-only E5 persistent demo."""

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
EVIDENCE = ROOT / "test-runs/2026-08-13-doom-embedded-touch-audio-e5-build.json"
AUTH = ROOT / "hardware/evidence/doom-embedded-touch-audio-e5-persistent-demo-authorization.json"
RUNTIME_BASIS = ROOT / "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json"
BOARD_PROFILE = ROOT / "hardware/board-profile.json"
TOUCH_PATH_VERIFIER = ROOT / "scripts/verify-touch-path.py"
VARIANT_REFERENCE = ROOT / ".agents/skills/develop-esp32-p4-platform/references/elecrow-10-in-variant.md"
POST_RUN = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-persistent.json"
RUNTIME_RAW = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-runtime.raw"
RUNTIME_SUMMARY = ROOT / "hardware/test-runs/2026-08-13-doom-e5-touch-only-runtime.json"
EXECUTED_EVIDENCE = ROOT / "test-runs/2026-08-13-doom-embedded-touch-audio-e5-build-executed.json"
EXECUTED_AUTH = ROOT / "hardware/evidence/doom-embedded-touch-audio-e5-persistent-demo-authorization-executed.json"
CAPTURE_TOOL = ROOT / "scripts/capture-doom-e5-runtime.py"

EXPECTED_RUNTIME_BASIS_SHA256 = "eaba0e61ccfd8c11e95b688709358bf13c47554421840cb8d8a8ced4b5c6c416"
EXPECTED_BOARD_PROFILE_SHA256 = "f5c38e1aceabc633aaf78b844f8b1f5f1f82d25adbf75291479c57d7c1fe2701"
EXPECTED_TOUCH_PATH_VERIFIER_SHA256 = "b029f176925e45fd0dabc24c1fe639e3be7334a8c442981d9e96a165378bef38"
EXPECTED_VARIANT_REFERENCE_SHA256 = "82c2d9284882029007b7e36e5bc7d94af94be85b1951203b2d0b1d4026ccdf37"
EXPECTED_EXECUTED_EVIDENCE_SHA256 = "6216eadaf11302f4e68b1dc0c1af9751bc6e000ddbddfde369ee9819e947e037"
EXPECTED_EXECUTED_AUTH_SHA256 = "14a8fe401bd3f3109a025cbf7a57eb3b94f982ca0126a2e37d30ca980fdcf160"
EXPECTED_RUNTIME_RAW_SHA256 = "6e54fea48d8ca40cb7df0c84c7fa89a15fdffbd5660a489892a866b30fdb8047"
EXPECTED_RUNTIME_SUMMARY_SHA256 = "6ccc6276689a84f06a200a197e43f8ecde10234cca0cdbfe2593ecf597cd4a3e"
EXPECTED_CAPTURE_TOOL_SHA256 = "de38e4eca1c1b947535ecd23c1e291c63c0b5c3c92e67ea7c07bf4e8ab3160b5"
EXPECTED_MUTATION_SPAN_SHA256 = "9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9"
EXPECTED_POST_RUN_SHA256 = "42cf1c97af54f0d37c6c4160f9d4b0b731819aad20c6084df406ed6fc3e6eb45"
EXPECTED_INSTALL_RUNTIME_BINDING = {
    "esptool_version": "4.12.0",
    "loader_sha256": "6c5f0c4a9d2047adb1c9164aee66208018789a0f6531922204f6b280168c80e8",
    "reset_sha256": "410334df7cb09cafc01efa8d32acb1903bbc5ba6f03df3a0b39823c433b0edef",
    "esp32p4_sha256": "0319aa7ee6e2e45c3ceb2e3424fb98a2ae9734873a2206767085abf75e74bf37",
    "esp32_sha256": "32b5e7f8b3e971705f57bb9cdf54e847eefd96e2168baa6120d3643beb224bb5",
    "legacy_rev1_stub_sha256": "3c0f27938055192977123cd4b503bf27d1676c59a7fd7e3f93de8e95b0cf63bf",
    "runtime_helper_sha256": "5be16e889116cc8a9e9009d730c2d0bcd3438ac07ead2d726ac1a346a5f0e244",
    "loader_entry_reset_class": "UnixTightReset",
    "application_launch_reset_class": "HardReset",
    "custom_reset_sequence_allowed": False,
    "python_executable_sha256": "5c3ea934d18a7979253ca08ff16151db78a896be29dbc37fd9bf7e7c549593b8",
    "pyserial_init_sha256": "5dec897fdef45a0eaf63eacda1e01d62e1e75490302584ae0238f75376592344",
    "pyserial_serialposix_sha256": "5d56f98513391e1766a1847a04e2e1202ced4172a152803ba1c823a149a9b6f9",
    "pyserial_serialutil_sha256": "3c84f8c7c319f161a85d6f8db5bedc4f65255714589bdcbeee6f2daf65a17c0a",
    "python_executable_path": "/opt/homebrew/Cellar/python@3.14/3.14.4/Frameworks/Python.framework/Versions/3.14/bin/python3.14",
    "pyserial_init_path": "/Users/billh/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/serial/__init__.py",
    "pyserial_serialposix_path": "/Users/billh/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/serial/serialposix.py",
    "pyserial_serialutil_path": "/Users/billh/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/serial/serialutil.py",
    "termios_path": "/opt/homebrew/Cellar/python@3.14/3.14.4/Frameworks/Python.framework/Versions/3.14/lib/python3.14/lib-dynload/termios.cpython-314-darwin.so",
    "termios_sha256": "18c723dc5d61cc411f12c75bf1b3de2be917490eb9733a226bc8d0c7a18c028f",
    "fcntl_path": "/opt/homebrew/Cellar/python@3.14/3.14.4/Frameworks/Python.framework/Versions/3.14/lib/python3.14/lib-dynload/fcntl.cpython-314-darwin.so",
    "fcntl_sha256": "0a54b8fc819837d545ee95da563421303e0cfaae73c5a4881ef56d32da3d667d",
    "flash_jedec_low24": "0x1840c8",
    "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
    "canonical_flash_id": "0x1840c8",
}
EXPECTED_PREWRITE_ATTEMPT = {
    "occurred_after_authorization_issuance": True,
    "recorded_at": "2026-08-13",
    "result": "failed-closed-before-mutation",
    "stage": "post-stub-prewrite-density-recheck",
    "initial_raw_flash_id": "0x001840c8",
    "later_raw_flash_id": "0xff1840c8",
    "canonical_flash_id": "0x1840c8",
    "flash_begin_called": False,
    "application_bytes_written": 0,
    "partition_modified": False,
    "application_launched": False,
    "device_end_state": "ram-stub-probable",
}
EXPECTED_OFFSET = "0x10000"
EXPECTED_WAD_BYTES = 4_196_020
EXPECTED_WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 24 * 1024, 0),
    ("phy_init", 1, 1, 0xF000, 4 * 1024, 0),
    ("factory", 0, 0, 0x10000, 11 * 1024 * 1024, 0),
    ("storage", 1, 0x82, 0xB10000, 4 * 1024 * 1024, 0),
]
EXPECTED_COMPONENTS = {
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
    "platform_audio_force_safe_shutdown", "platform_audio_invocation_count",
    "s_composite_authorized",
    "s_touch_authorized", "s_audio_authorized",
    "_binary_doom_shareware_wad_start", "_binary_doom_shareware_wad_end",
}
FORBIDDEN_SYMBOLS = {
    "usb_host_install", "hid_host_install", "platform_usb_host_start",
    "platform_gamepad_usb_start", "esp_vfs_fat_sdmmc_mount",
}
FACTORY_ENTRYPOINTS = {
    "platform_audio_factory_force_safe_shutdown",
    "platform_audio_factory_recover",
    "platform_audio_factory_create",
    "platform_audio_factory_start",
    "platform_audio_factory_write_frames",
    "platform_audio_factory_stop",
    "platform_audio_factory_get_state",
    "platform_audio_factory_destroy",
}
INVENTORY_TREES = (
    ROOT / "apps/doom/components/doom_audio",
    ROOT / "apps/doom_audio_probe/components/doom_engine_audio",
    APP,
    ROOT / "components/doom_touch_input",
    ROOT / "components/doom_video",
    ROOT / "components/platform_audio_factory",
    ROOT / "components/platform_display",
    ROOT / "components/platform_i2c_shared",
    ROOT / "components/platform_readonly_blob",
    ROOT / "components/platform_touch",
)
INVENTORY_FIXED = {
    ".agents/skills/develop-esp32-p4-platform/SKILL.md",
    ".agents/skills/develop-esp32-p4-platform/references/elecrow-10-in-variant.md",
    ".agents/skills/develop-esp32-p4-platform/references/hardware-safety.md",
    ".agents/skills/develop-esp32-p4-platform/references/workflow.md",
    ".agents/skills/use-elecrow-p4-display/SKILL.md",
    ".agents/skills/use-elecrow-p4-display/references/display-contract.md",
    "Makefile",
    "hardware/board-profile.json",
    "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json",
    "scripts/build.sh",
    "scripts/capture-doom-e5-runtime.py",
    "scripts/check.sh",
    "scripts/doom/verify-doomgeneric.sh",
    "scripts/doom/verify-metadata.py",
    "scripts/doom-e5-install.py",
    "scripts/flash.sh",
    "scripts/gamepad-diag-restore.py",
    "scripts/lib/app-readback.sh",
    "scripts/lib/project-env.sh",
    "scripts/tests/test-doom-e5-gate.py",
    "scripts/tests/test-doom-e5-install.py",
    "scripts/tests/test-doom-e5-runtime-capture.py",
    "scripts/tests/test-project-env.sh",
    "scripts/verify-doom-embedded-touch-audio.py",
    "scripts/verify-env.sh",
    "scripts/verify-touch-path.py",
    "third_party/doomgeneric.git-tree",
    "third_party/doomgeneric/LICENSE",
    "third_party/doomgeneric/README.TXT",
    "third_party/doomgeneric/README.md",
    "third_party/source-lock.json",
    "toolchain.lock.json",
}


def fail(message: str) -> None:
    raise SystemExit(f"doom_embedded_touch_audio verification failed: {message}")


def require(value: bool, message: str) -> None:
    if not value:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def load_capture_analyzer():
    spec = importlib.util.spec_from_file_location("doom_e5_capture_verify", CAPTURE_TOOL)
    require(spec is not None and spec.loader is not None, "cannot load exact runtime analyzer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_post_run() -> None:
    require(sha256(POST_RUN) == EXPECTED_POST_RUN_SHA256, "post-run manifest changed")
    require(sha256(EXECUTED_EVIDENCE) == EXPECTED_EXECUTED_EVIDENCE_SHA256, "executed build evidence changed")
    require(sha256(EXECUTED_AUTH) == EXPECTED_EXECUTED_AUTH_SHA256, "executed authorization changed")
    require(sha256(RUNTIME_RAW) == EXPECTED_RUNTIME_RAW_SHA256 and RUNTIME_RAW.stat().st_size == 598, "runtime raw changed")
    require(sha256(RUNTIME_SUMMARY) == EXPECTED_RUNTIME_SUMMARY_SHA256 and RUNTIME_SUMMARY.stat().st_size == 3116, "runtime summary changed")
    require(sha256(CAPTURE_TOOL) == EXPECTED_CAPTURE_TOOL_SHA256, "runtime analyzer changed")
    for path, label in (
        (POST_RUN, "post-run manifest"),
        (RUNTIME_RAW, "runtime raw"),
        (RUNTIME_SUMMARY, "runtime summary"),
        (EXECUTED_EVIDENCE, "executed evidence"),
        (EXECUTED_AUTH, "executed authorization"),
    ):
        require(path.stat().st_mode & 0o777 == 0o444, f"{label} mode is not 0444")

    executed_evidence = load_json(EXECUTED_EVIDENCE)
    executed_auth = load_json(EXECUTED_AUTH)
    require(executed_auth.get("build_evidence") == {
        "path": "test-runs/2026-08-13-doom-embedded-touch-audio-e5-build.json",
        "sha256": EXPECTED_EXECUTED_EVIDENCE_SHA256,
    }, "executed auth/evidence binding differs")
    require(executed_auth.get("active") is True and executed_auth.get("gates") == {"composite": 1, "touch": 1, "audio": 0}, "executed authorization contract differs")
    require(executed_evidence.get("exact_artifact", {}).get("app_binary_sha256") == "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8", "executed artifact binding differs")

    raw = RUNTIME_RAW.read_bytes()
    stored_summary = load_json(RUNTIME_SUMMARY)
    analysis = load_capture_analyzer().analyze(raw, min_stats=2, startup="optional")
    for key, value in analysis.items():
        require(stored_summary.get(key) == value, f"stored runtime analysis differs: {key}")
    require(stored_summary.get("raw_bytes") == len(raw) and stored_summary.get("raw_sha256") == EXPECTED_RUNTIME_RAW_SHA256, "runtime raw/summary binding differs")
    require(analysis.get("result") == "pass" and analysis.get("classification") == "runtime-pass-late-attach", "runtime did not pass late-attach policy")
    require(analysis.get("startup_status") == "not-sampled-late-attach" and analysis.get("startup_complete") is False, "runtime invents startup proof")
    require(analysis.get("contacts_observed") is None, "runtime invents contact evidence")

    post = load_json(POST_RUN)
    require(post.get("schema") == 1 and post.get("result") == "pass-install-readback-and-late-attach-steady-state-touch-response-pending", "post-run result differs")
    require(post.get("executed_preflash_records", {}).get("build_evidence", {}).get("sha256") == EXPECTED_EXECUTED_EVIDENCE_SHA256, "post-run executed evidence binding differs")
    require(post.get("executed_preflash_records", {}).get("authorization", {}).get("sha256") == EXPECTED_EXECUTED_AUTH_SHA256, "post-run authorization binding differs")
    firmware = post.get("firmware", {})
    require(firmware.get("artifact_bytes") == 4_898_400 and firmware.get("artifact_sha256") == "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8", "post-run artifact differs")
    require(firmware.get("mutation_span_bytes") == 4_898_816 and firmware.get("mutation_span_sha256") == EXPECTED_MUTATION_SPAN_SHA256, "post-run mutation span differs")
    install = post.get("install", {})
    require(install.get("result") == "pass" and install.get("readback_bytes") == 4_898_816 and install.get("readback_chunks") == 10 and install.get("readback_max_chunk_bytes") == 524_288 and install.get("readback_sha256") == EXPECTED_MUTATION_SPAN_SHA256, "post-run readback differs")
    require(install.get("same_uart_handle") is True and install.get("application_launch_count") == 1 and install.get("soft_reset_count") == 0 and install.get("esptool_run_count") == 0, "post-run launch contract differs")
    runtime = post.get("runtime_capture", {})
    require(runtime.get("raw", {}).get("sha256") == EXPECTED_RUNTIME_RAW_SHA256 and runtime.get("summary", {}).get("sha256") == EXPECTED_RUNTIME_SUMMARY_SHA256, "post-run runtime binding differs")
    require(runtime.get("startup_complete") is False and runtime.get("result") == "pass", "post-run runtime scope differs")
    require(post.get("accepted_claims") == [
        "the exact E5 application plus its 0xff-padded write span was written and read back through one exclusive UART transaction before one application launch",
        "late-attach serial proves sustained video submission/completion through frame 600 with zero video timeouts or failures",
        "late-attach serial proves increasing successful GT911 poll counts from 288 to 588 with zero poll failures",
        "every captured periodic record proves retained runtime gates composite=1 touch=1 audio=0 and counted audio_calls=0; its gpio30=untouched field is firmware-reported and derived from that sole counted audio API path, not an electrical measurement",
        "the captured interval contains no USB-runtime, reset-loop, panic, watchdog, brownout, halt, or other reject marker",
    ], "post-run accepted claims differ")
    require(post.get("claims_not_made") == [
        "late attachment did not sample START, MODE, initialization, WAD, VFS, VIDEO_READY, or ENGINE_START markers",
        "successful polling does not prove a contact was present, mapped correctly, or changed Doom state",
        "touch coordinates, multi-touch behavior, and visible control response remain pending person observation",
        "audio was disabled; no GPIO30-low, audio frame, acoustic, or sound-backend hardware claim is made",
        "USB runtime was disabled and absent from the firmware component graph; physical USB state was not tested",
        "the uninstrumented power supply has not been margin-tested",
        "the unresolved physical PCB revision is not inferred from this scoped run",
        "the WAD-bearing firmware is not authorized for redistribution",
    ], "post-run claim limits differ")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def exact_file(path: pathlib.Path, record: dict, prefix: str) -> None:
    require(path.is_file(), f"missing {prefix}: {path}")
    require(path.stat().st_size == record.get(f"{prefix}_bytes"), f"wrong {prefix} bytes")
    require(sha256(path) == record.get(f"{prefix}_sha256"), f"wrong {prefix} hash")


def checked_child(build: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    path = (build / relative).resolve()
    require(path.is_relative_to(build), f"{label} escapes build directory")
    require(path.is_file(), f"missing {label}: {path}")
    return path


def partition_entries(path: pathlib.Path) -> list[tuple[str, int, int, int, int, int]]:
    data = path.read_bytes()
    entries = []
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            break
        magic, kind, subtype, part_offset, size, name, flags = struct.unpack("<HBBII16sI", entry)
        if magic == 0xEBEB:
            break
        require(magic == 0x50AA, f"invalid partition entry at {offset}")
        entries.append((name.split(b"\0", 1)[0].decode("ascii"), kind, subtype, part_offset, size, flags))
    return entries


def cmake_tool(build: pathlib.Path, variable: str) -> pathlib.Path:
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(rf"^{re.escape(variable)}:FILEPATH=(.+)$", cache, re.MULTILINE)
    require(match is not None, f"{variable} missing from CMake cache")
    tool = pathlib.Path(match.group(1))
    require(tool.is_file(), f"configured tool missing: {tool}")
    return tool


def symbols(build: pathlib.Path, elf: pathlib.Path) -> dict[str, int]:
    nm = cmake_tool(build, "CMAKE_NM")
    result = subprocess.run([str(nm), "-a", str(elf)], check=True, capture_output=True, text=True)
    found: dict[str, int] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            found[fields[-1]] = int(fields[0], 16)
    return found


def rodata_byte(build: pathlib.Path, elf: pathlib.Path, address: int) -> int:
    objdump = cmake_tool(build, "CMAKE_OBJDUMP")
    result = subprocess.run(
        [str(objdump), "-s", f"--start-address={address}", f"--stop-address={address + 1}", str(elf)],
        check=True, capture_output=True, text=True,
    )
    for line in result.stdout.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]{2,})", line)
        if match and int(match.group(1), 16) == address:
            return int(match.group(2)[:2], 16)
    fail(f"cannot extract gate byte at {address:#x}")


def verify_factory_callers(build: pathlib.Path, description: dict) -> None:
    elf_relative = description.get("app_elf")
    require(isinstance(elf_relative, str) and elf_relative.endswith(".elf"), "ELF name is invalid")
    map_path = checked_child(build, elf_relative[:-4] + ".map", "linker map")
    lines = map_path.read_text(encoding="utf-8").splitlines()
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
            current = None
            continue
        current = fields[0]
        references[current] = [fields[1].strip()]
    expected_definition = (
        "esp-idf/platform_audio_factory/libplatform_audio_factory.a("
        "platform_audio_factory.c.obj)"
    )
    expected_caller = (
        "esp-idf/platform_audio/libplatform_audio.a("
        "platform_audio_adapter.c.obj)"
    )
    for symbol in FACTORY_ENTRYPOINTS:
        require(
            references.get(symbol) == [expected_definition, expected_caller],
            f"factory entry point has an uncounted caller: {symbol}",
        )


def expected_inventory_paths() -> set[str]:
    expected = set(INVENTORY_FIXED)
    for tree in INVENTORY_TREES:
        require(tree.is_dir(), f"inventory tree missing: {tree}")
        for path in tree.rglob("*"):
            if not path.is_file():
                continue
            relative = path.relative_to(ROOT)
            parts = relative.parts
            if "build" in parts or "managed_components" in parts or \
               path.name == "sdkconfig" or "__pycache__" in parts:
                continue
            expected.add(str(relative))
    managed = APP / "managed_components"
    for component in managed.iterdir():
        if component.is_dir():
            expected.add(str((component / ".component_hash").relative_to(ROOT)))
            expected.add(str((component / "CHECKSUMS.json").relative_to(ROOT)))
    return expected


def verify_policy(mode: str, evidence: dict) -> None:
    metadata = load_json(APP / "app-metadata.json")
    require(metadata.get("app") == "doom_embedded_touch_audio", "wrong metadata app")
    require(metadata.get("stage") == "E5-touch-only-persistent-demo-installed-steady-state-pass-contact-response-pending", "wrong stage")
    require(metadata.get("runtime_supported") is True, "touch runtime is not authorized")
    require(metadata.get("top_level_runtime_authorized") is True, "top-level runtime disabled")
    require(metadata.get("touch_runtime_authorized") is True, "touch runtime disabled")
    require(metadata.get("audio_runtime_authorized") is False, "audio runtime must remain disabled")
    require(metadata.get("flash_authorized") is False, "broad flash enabled")
    require(metadata.get("flash_project_authorized") is False, "project flash enabled")
    require(metadata.get("flash_app_authorized") is True, "app flash not authorized")
    require(metadata.get("game_data_redistribution_authorized") is False, "WAD redistribution enabled")
    require(metadata.get("runtime_mode", {}).get("required_exact_gates") == {"composite": 1, "touch": 1, "audio": 0}, "wrong gate contract")
    require(metadata.get("runtime_mode", {}).get("audio_calls") == 0, "audio calls not zero")
    require(metadata.get("runtime_mode", {}).get("gpio30_access") is False, "GPIO30 access enabled")
    require(metadata.get("build_evidence") == str(EVIDENCE.relative_to(ROOT)), "wrong evidence path")
    require(metadata.get("runtime_evidence") == str(POST_RUN.relative_to(ROOT)), "wrong runtime evidence path")
    require(metadata.get("executed_preflash_authorization") == {
        "path": str(EXECUTED_AUTH.relative_to(ROOT)),
        "sha256": EXPECTED_EXECUTED_AUTH_SHA256,
    }, "wrong executed authorization binding")
    require(metadata.get("executed_build_evidence") == {
        "path": str(EXECUTED_EVIDENCE.relative_to(ROOT)),
        "sha256": EXPECTED_EXECUTED_EVIDENCE_SHA256,
    }, "wrong executed build binding")
    require(metadata.get("hardware_acceptance") == {
        "exact_app_and_padded_span_readback": "pass",
        "single_application_launch": "pass",
        "late_attach_steady_state_video": "pass",
        "successful_gt911_poll_progress": "pass-without-contact-claim",
        "runtime_gates": "pass-1/1/0",
        "audio_calls": 0,
        "gpio30": "firmware-reported-untouched-derived-from-zero-counted-audio-api-calls-not-electrically-measured",
        "usb_runtime": "disabled-and-absent-from-firmware-component-graph-not-physically-tested",
        "contact_response": "pending-person-observation",
    }, "metadata hardware acceptance differs")
    require(metadata.get("claims_not_made") == [
        "Touch coordinates or multi-touch gameplay have been independently hardware-accepted.",
        "Audio is enabled or independently hardware-accepted by this artifact.",
        "The WAD-bearing firmware is authorized for redistribution.",
    ], "metadata claim limits differ")
    require(metadata.get("execution_contract") == {
        "write_scope": "factory-application-partition-only-at-0x10000",
        "shell_live_probe_count": 0,
        "exclusive_uart_open_count": 1,
        "exclusive_uart_reopen_count": 0,
        "exclusive_transaction_loader_entry_reset_count": 1,
        "loader_connect_no_reset_attempts": 1,
        "write_transport": "same-handle-pinned-rev1-ram-stub",
        "write_span_bytes": 4898816,
        "flash_jedec_low24": "0x1840c8",
        "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
        "canonical_flash_id": "0x1840c8",
        "stub_flash_finish_sync_count": 1,
        "stub_flash_finish_sync_launch_count": 0,
        "readback": "same-handle-ordered-full-span-512KiB-max-chunks",
        "application_launch_after_exact_readback": True,
        "application_launch_mechanism": "direct-hash-bound-HardReset-rts-only-dtr-false",
        "exclusive_transaction_application_launch_reset_count": 1,
        "application_launch_count": 1,
        "soft_reset_count": 0,
        "esptool_run_count": 0,
        "persistent_across_future_resets": True,
    }, "metadata execution contract differs")
    if mode == "app-flash":
        require(AUTH.is_file(), "persistent-demo authorization is missing")
        auth = load_json(AUTH)
        require(auth.get("schema") == 1, "authorization schema changed")
        require(auth.get("id") == "doom-embedded-touch-audio-e5-touch-only-persistent-demo-2026-08-13", "authorization ID changed")
        require(auth.get("classification") == "user-requested-touch-only-persistent-demo-active", "authorization classification changed")
        require(auth.get("active") is True and auth.get("scope") == "touch-only-persistent-demo", "authorization inactive or wrong scope")
        require(auth.get("record_scope") == "current mutable authorization copy; the exact bytes actually validated by the successful transaction are preserved separately in doom-embedded-touch-audio-e5-persistent-demo-authorization-executed.json", "authorization record scope differs")
        require(auth.get("hardware_accessed_before_issuance") is False, "authorization invents hardware access")
        require(auth.get("device_binding") == {
            "identity_kind": "sha256-of-normalized-base-identity",
            "identity_sha256": "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0",
            "flash_bytes": 16_777_216,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
        }, "authorization device binding differs")
        require(auth.get("exact_artifact") == evidence.get("exact_artifact"), "authorization artifact differs")
        require(auth.get("build_evidence") == {
            "path": str(EVIDENCE.relative_to(ROOT)),
            "sha256": sha256(EVIDENCE),
        }, "authorization build evidence differs")
        require(auth.get("runtime_basis") == {"path": str(RUNTIME_BASIS.relative_to(ROOT)), "sha256": EXPECTED_RUNTIME_BASIS_SHA256}, "authorization runtime basis differs")
        require(auth.get("board_profile") == {"path": str(BOARD_PROFILE.relative_to(ROOT)), "sha256": EXPECTED_BOARD_PROFILE_SHA256}, "authorization board profile differs")
        require(auth.get("install_runtime_binding") == EXPECTED_INSTALL_RUNTIME_BINDING, "authorization install runtime differs")
        require(auth.get("gates") == {"composite": 1, "touch": 1, "audio": 0}, "authorization gates differ")
        require(auth.get("execution_contract") == {
            "app_partition_only": True,
            "full_project_flash": False,
            "shell_live_probe_count": 0,
            "exclusive_uart_open_count": 1,
            "exclusive_uart_reopen_count": 0,
            "exclusive_transaction_loader_entry_reset_count": 1,
            "loader_connect_no_reset_attempts": 1,
            "write_transport": "same-handle-pinned-rev1-ram-stub",
            "write_span_bytes": 4898816,
            "flash_jedec_low24": "0x1840c8",
            "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
            "canonical_flash_id": "0x1840c8",
            "stub_flash_finish_sync_count": 1,
            "stub_flash_finish_sync_launch_count": 0,
            "readback_max_chunk_bytes": 524_288,
            "readback_scope": "same-handle-full-padded-mutation-span",
            "ordered_aggregate_sha256_required": True,
            "application_launch_after_exact_readback": True,
            "application_launch_mechanism": "direct-hash-bound-HardReset-rts-only-dtr-false",
            "exclusive_transaction_application_launch_reset_count": 1,
            "application_launch_count": 1,
            "soft_reset_count": 0,
            "esptool_run_count": 0,
            "persistent_across_future_resets": True,
            "audio_runtime": False,
            "gpio30_access": False,
            "usb_runtime": False,
        }, "authorization execution contract differs")
        require(auth.get("prewrite_attempt_history") == [EXPECTED_PREWRITE_ATTEMPT], "authorization prewrite history differs")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-doom-embedded-touch-audio.py BUILD_DIR build-only|app-flash|post-run")
    build = pathlib.Path(sys.argv[1]).resolve()
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash", "post-run"}, "mode must be build-only, app-flash, or post-run")
    require(build.is_dir(), "build directory missing")
    require(sha256(RUNTIME_BASIS) == EXPECTED_RUNTIME_BASIS_SHA256, "runtime basis changed")
    require(sha256(BOARD_PROFILE) == EXPECTED_BOARD_PROFILE_SHA256, "board profile changed")
    require(sha256(TOUCH_PATH_VERIFIER) == EXPECTED_TOUCH_PATH_VERIFIER_SHA256, "touch verifier changed")
    require(sha256(VARIANT_REFERENCE) == EXPECTED_VARIANT_REFERENCE_SHA256, "variant reference changed")
    basis = load_json(RUNTIME_BASIS)
    profile = load_json(BOARD_PROFILE)
    require(basis.get("result") == "touch-runtime-authorized-audio-implementation-basis-only", "basis classification changed")
    require(basis.get("touch_authorization_decision", {}).get("authorized") is True, "touch basis inactive")
    require(basis.get("audio_authorization_decision", {}).get("authorized") is False, "audio basis became active")
    require(basis.get("audio_authorization_decision", {}).get("required_runtime_gate") == 0, "audio gate requirement changed")
    require(basis.get("audio_authorization_decision", {}).get("required_gpio30_access") is False, "GPIO30 basis changed")
    require(profile.get("pin_map_authorized") is False, "global pin map enabled")
    require(profile.get("peripheral_authorizations", {}).get("touch", {}).get("authorized") is True, "profile touch disabled")
    require(profile.get("peripheral_authorizations", {}).get("audio", {}).get("authorized") is False, "profile audio enabled")

    if mode == "post-run":
        verify_post_run()

    evidence = load_json(EVIDENCE)
    require(evidence.get("schema") == 1, "evidence schema changed")
    require(evidence.get("classification") == "build-tested-touch-only-runtime-authorized-prewrite-hardware-access-no-mutation", "evidence classification changed")
    require(evidence.get("result") == "pass-build-host-tests-reproducibility-prewrite-failed-closed-before-mutation", "evidence result changed")
    require(evidence.get("record_scope") == "historical pre-success build and first prewrite-attempt record; execution=false fields describe only the record state through that failed-closed attempt, while the later successful install/runtime is recorded separately in hardware/test-runs/2026-08-13-doom-e5-touch-only-persistent.json", "evidence record scope differs")
    require(evidence.get("execution") == {
        "hardware_accessed": True,
        "firmware_flashed": False,
        "firmware_executed": False,
        "audio_path_energized": False,
        "independent_touch_acceptance": False,
        "prewrite_attempt": EXPECTED_PREWRITE_ATTEMPT,
    }, "evidence execution truth differs")
    require(evidence.get("install_contract") == {
        "scope": "factory-application-partition-only",
        "shell_live_probe_count": 0,
        "exclusive_uart_open_count": 1,
        "exclusive_uart_reopen_count": 0,
        "exclusive_transaction_loader_entry_reset_count": 1,
        "loader_connect_no_reset_attempts": 1,
        "write_transport": "same-handle-pinned-rev1-ram-stub",
        "write_span_bytes": 4898816,
        "flash_jedec_low24": "0x1840c8",
        "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
        "canonical_flash_id": "0x1840c8",
        "stub_flash_finish_sync_count": 1,
        "stub_flash_finish_sync_launch_count": 0,
        "readback_max_chunk_bytes": 524_288,
        "readback_scope": "same-handle-full-padded-mutation-span",
        "ordered_aggregate_sha256_required": True,
        "application_launch_mechanism": "direct-hash-bound-HardReset-rts-only-dtr-false",
        "exclusive_transaction_application_launch_reset_count": 1,
        "application_launch_count": 1,
        "soft_reset_count": 0,
        "esptool_run_count": 0,
    }, "evidence install contract differs")
    require(evidence.get("install_runtime_binding") == EXPECTED_INSTALL_RUNTIME_BINDING, "evidence install runtime differs")
    variant = evidence.get("electrical_and_policy_bindings", {}).get("variant_reference")
    require(variant == {
        "path": str(VARIANT_REFERENCE.relative_to(ROOT)),
        "sha256": EXPECTED_VARIANT_REFERENCE_SHA256,
    }, "evidence variant binding differs")
    verify_policy(mode, evidence)
    source_inventory = evidence.get("source_inventory")
    require(isinstance(source_inventory, dict) and source_inventory, "source inventory missing")
    required_inventory_paths = {
        "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c",
        "apps/doom_embedded_touch_audio/main/runtime_gate.c",
        "apps/doom_embedded_touch_audio/main/touch_controls.c",
        "apps/doom_embedded_touch_audio/main/degraded_policy.c",
        "apps/doom_embedded_touch_audio/components/platform_audio/include/platform/audio.h",
        "apps/doom_embedded_touch_audio/components/platform_audio/src/platform_audio_adapter.c",
        "components/platform_touch/src/platform_touch.c",
        "components/platform_i2c_shared/src/bus.c",
        "components/doom_touch_input/src/input.c",
        "components/doom_touch_input/src/overlay.c",
        "components/platform_audio_factory/src/platform_audio_factory.c",
        "components/platform_audio_factory/src/platform_audio_factory_policy.c",
        "apps/doom_embedded_touch_audio/dependencies.lock",
        "apps/doom_embedded_touch_audio/sdkconfig.defaults",
        "apps/doom_embedded_touch_audio/app-metadata.json",
        "scripts/capture-doom-e5-runtime.py",
        "scripts/doom-e5-install.py",
        "scripts/gamepad-diag-restore.py",
        "scripts/tests/test-doom-e5-runtime-capture.py",
        "scripts/tests/test-doom-e5-install.py",
        "scripts/verify-doom-embedded-touch-audio.py",
        "scripts/flash.sh",
        "scripts/tests/test-doom-e5-gate.py",
        "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json",
        "hardware/board-profile.json",
        ".agents/skills/develop-esp32-p4-platform/SKILL.md",
        ".agents/skills/develop-esp32-p4-platform/references/elecrow-10-in-variant.md",
        ".agents/skills/use-elecrow-p4-display/SKILL.md",
        "toolchain.lock.json",
    }
    require(required_inventory_paths <= set(source_inventory), "critical source inventory is incomplete")
    require(set(source_inventory) == expected_inventory_paths(), "source inventory path set is not exact")
    for relative, expected in source_inventory.items():
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT) and path.is_file(), f"inventory path invalid: {relative}")
        require(sha256(path) == expected, f"source changed: {relative}")

    description = load_json(build / "project_description.json")
    require(description.get("project_name") == "p4_doom_embedded_touch_audio", "wrong project")
    require(description.get("target") == "esp32p4" and int(description.get("min_rev")) == 100 and int(description.get("max_rev")) == 199, "wrong target/revision")
    components = set(description.get("build_components", []))
    require(EXPECTED_COMPONENTS <= components, "required component missing")
    require(not (FORBIDDEN_COMPONENTS & components), "forbidden component built")
    info = description.get("build_component_info", {})
    require(pathlib.Path(info["platform_audio"]["dir"]).resolve() == (APP / "components/platform_audio").resolve(), "legacy platform_audio selected")
    require(pathlib.Path(info["platform_audio_factory"]["dir"]).resolve() == (ROOT / "components/platform_audio_factory").resolve(), "wrong factory audio component")

    app = checked_child(build, description.get("app_bin"), "application")
    elf = checked_child(build, description.get("app_elf"), "ELF")
    partition = build / "partition_table/partition-table.bin"
    bootloader = build / "bootloader/bootloader.bin"
    artifact = evidence.get("exact_artifact", {})
    require(artifact.get("offset") == EXPECTED_OFFSET, "wrong artifact offset")
    exact_file(app, artifact, "app_binary")
    exact_file(elf, artifact, "elf")
    exact_file(bootloader, artifact, "bootloader")
    exact_file(partition, artifact, "partition_table")
    require(partition_entries(partition) == EXPECTED_PARTITIONS, "partition table changed")

    config = load_json(build / "config/sdkconfig.json")
    required_config = evidence.get("resolved_config")
    require(isinstance(required_config, dict), "resolved config missing")
    for key, expected in required_config.items():
        require(config.get(key) == expected, f"config changed: {key}")

    found = symbols(build, elf)
    require(REQUIRED_SYMBOLS <= set(found), "required linked symbol missing")
    require(not (FORBIDDEN_SYMBOLS & set(found)), "forbidden linked symbol present")
    verify_factory_callers(build, description)
    require(rodata_byte(build, elf, found["s_composite_authorized"]) == 1, "composite gate byte is not 1")
    require(rodata_byte(build, elf, found["s_touch_authorized"]) == 1, "touch gate byte is not 1")
    require(rodata_byte(build, elf, found["s_audio_authorized"]) == 0, "audio gate byte is not 0")
    binary = app.read_bytes()
    wad = (ROOT / "local-data/doom/doom1.wad").read_bytes()
    require(len(wad) == EXPECTED_WAD_BYTES and hashlib.sha256(wad).hexdigest() == EXPECTED_WAD_SHA256, "local WAD changed")
    require(binary.count(wad) == 1, "application must embed one exact WAD")
    require(binary.count(b"P4_DOOM_E5 MODE composite_gate=%u touch_gate=%u audio_gate=%u mode=%s") == 1, "dynamic mode marker missing")
    require(binary.count(b"P4_DOOM_E5 START input=gt911-multitouch sound=disabled-backend-linked-build-only usb=absent runtime=guarded") == 1, "truthful touch-only START marker missing")
    require(binary.count(b"SOUND_DISABLED audio_gate=%u audio_calls=%") == 1, "dynamic audio-off marker missing")
    require(binary.count(b"composite_gate=%u touch_gate=%u audio_gate=%u") >= 2, "dynamic gate proof missing")
    require(binary.count(b"gpio30=%s") >= 2, "derived GPIO30 proof missing")
    print(f"P4_DOOM_E5 VERIFY PASS mode={mode} bytes={app.stat().st_size} sha256={sha256(app)} gates=1/1/0 usb=absent audio_runtime=off")


if __name__ == "__main__":
    main()
