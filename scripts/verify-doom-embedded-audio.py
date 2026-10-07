#!/usr/bin/env python3

"""Fail-closed exact-image gate for the build-only E2 Doom/audio composite."""

from __future__ import annotations

import csv
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/doom_embedded_audio"
LOCAL_WAD = ROOT / "local-data/doom/doom1.wad"
BUILD_EVIDENCE_REL = "test-runs/2026-08-13-doom-embedded-audio-e2-build.json"
BUILD_EVIDENCE = ROOT / BUILD_EVIDENCE_REL
BUILD_EVIDENCE_SHA256 = "66eeafc9403b65bb1d6707dd68a60ce985eb4441ad14bbd64c5e1ec6b2fe1f68"

# These two future records do not exist yet. app-flash must fail closed until
# D2.3 has passed and a separate exact E2 authorization is reviewed/activated.
D23_RUNTIME_REL = "hardware/test-runs/2026-08-13-audio-direct-diag-d23.json"
E2_AUTHORIZATION_REL = (
    "hardware/evidence/doom-embedded-audio-e2-one-shot-authorization.json"
)
# Fill these only after the final records exist. Leaving any value unset keeps
# app-flash fail-closed even if someone flips metadata flags prematurely.
D23_RUNTIME_SHA256: str | None = None
E2_AUTHORIZATION_SHA256: str | None = None
D23_DIAG_BYTES = 195920
D23_DIAG_SHA256 = "f6d7b9995d3b15eac612644c30a73f54071f0178b60fa92b5e8096c5b6ad6c47"
D23_ANALYZER_REL = "scripts/analyze-audio-tone.py"
D23_ANALYZER_SHA256: str | None = None

APP_OFFSET = 0x10000
APP_CAPACITY = 11 * 1024 * 1024
APP_BYTES = 4858032
APP_SHA256 = "22a085c0e2b3a035e7e1fe4f54fcbac147e1c4aa5a799119bc1a741e55c859f4"
ELF_BYTES = 13337340
ELF_SHA256 = "d32b27ecb5117182bc9a2015d25d5f654ef1503db305d393176d989811f854f1"
BOOTLOADER_BYTES = 22912
BOOTLOADER_SHA256 = (
    "fcb629f826b8cd79fb21533b8691650a775804f337db37451b03127e36a32ca9"
)
PARTITION_BYTES = 3072
PARTITION_SHA256 = (
    "5b5bfa656e96706d5144b352bf9294ab455a7e1e49136ea5521cf15902a9e433"
)
WAD_BYTES = 4196020
WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
WAD_BINARY_OFFSET = 143656
WAD_ELF_START = 0x40083128
WAD_ELF_END = 0x404837DC
DOOM_COMMIT = "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"
DOOM_TREE = "413539bdaa1521af167d9b34e9db0cd193367624"
DOOM_MANIFEST_SHA256 = (
    "499fd1de6e8809099ba1ae5c75fb5eebde7a66f68950acfb9c72683c5279f2fe"
)

EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 24 * 1024, 0),
    ("phy_init", 1, 1, 0xF000, 4 * 1024, 0),
    ("factory", 0, 0, APP_OFFSET, APP_CAPACITY, 0),
    ("storage", 1, 0x82, 0xB10000, 4 * 1024 * 1024, 0),
]

EXPECTED_INTERFACES = {
    "mipi_dsi_bus_0",
    "internal_ldo_3_2500mv",
    "internal_ldo_4_3300mv",
    "gpio31_backlight_pwm",
    "i2s1_direct_speaker_candidate",
    "gpio30_amplifier_shutdown_candidate",
}
EXPECTED_ABSENT = {
    "gpio29",
    "gpio41",
    "sd_card",
    "touch",
    "i2c",
    "audio_codec",
    "mclk",
    "camera",
    "wireless",
    "usb_host",
}

EXPECTED_SOURCE_INVENTORY = {
    "apps/doom_embedded_audio/CMakeLists.txt": "1d705180349f810659cb3e30787a8f80d566f54d3b9b1e435be8644fdd0fbbba",
    "apps/doom_embedded_audio/README.md": "7c11fd10e33db0562ed2bb1f5ac68344b894272698ff8b775cd49587526248ec",
    "apps/doom_embedded_audio/app-metadata.json": "864c4c8686b0ffcfda05544963253e0885cb390fc712249dfc3503aa65fa4025",
    "apps/doom_embedded_audio/dependencies.lock": "705d29841eb277c84a8330b3eb0b1978f508fc96e5a7f1687c68a0641447e54a",
    "apps/doom_embedded_audio/main/CMakeLists.txt": "1ba0ad37c2fc85a6958648f568771cd0fa3dfe8b5a905fd81cd667a0228d2545",
    "apps/doom_embedded_audio/main/doom_embedded_audio_main.c": "d42ce8e2b9b7ea06284cdd9c6f1c452d4022cfa3520bc1a25a4564621f3204e8",
    "apps/doom_embedded_audio/main/idf_component.yml": "c9e2f6fe22ce6e037c6618ddc9737546e7cc3548dc421202592a0c79d7754528",
    "apps/doom_embedded_audio/partitions.csv": "019fd608a82c197af7ec4ae246c6457e535c96bf9e9b147e3c9c30ff0ad65401",
    "apps/doom_embedded_audio/sdkconfig.defaults": "2547af44040d90ce0b6b7715bfa06e95a664e9bd7147ab45883c864543375ccf",
    "apps/doom_audio_probe/components/doom_engine_audio/CMakeLists.txt": "eedb1e1f78b2c83e0f4f37f3c2ab46b676a0eafb7a7f7279b74b049ab901d1db",
    "apps/doom_audio_probe/components/doom_engine_audio/doom_i_sound_feature.c": "5b1c332b0f470084f639f80444b33bb3efd64801a46b1304f011ce30b95dffc4",
    "apps/doom/components/doom_audio/CMakeLists.txt": "94a5eae111d4010af1dce76e91e8d129efa66af5b2fb882c654239039e17a1d4",
    "apps/doom/components/doom_audio/Kconfig": "e82beabcc211e252941c6ab9f1af86cce1b78f2fd0b54a0ebefa1e89c30f2234",
    "apps/doom/components/doom_audio/include/doom/audio_mixer.h": "60f881175abeee0692730732b9caf0cb6db7ffc0792b2231228c71a73e2db9a1",
    "apps/doom/components/doom_audio/include/doom/audio_ring.h": "30eb3cc0159aaaef6bba14d3f85b4f859126486ddc3b5caaea116220a67995e3",
    "apps/doom/components/doom_audio/include/doom/audio_runtime.h": "ace936f83c98917e93ec62aa7c310820a38128aa5508797a5209fd5e5c9693e7",
    "apps/doom/components/doom_audio/src/doom_audio_mixer.c": "f4a77e5b414e964cf3999db218d658218f8fae76bebfae93d689eabfde9c8ced",
    "apps/doom/components/doom_audio/src/doom_audio_ring.c": "78f9c43800c36486ab5856c050ba198b7a3cf46f3f9641e108dd0f9163f1fbf3",
    "apps/doom/components/doom_audio/src/doom_audio_runtime.c": "f254662dbbcac63f00800a4d7e9847d3651fb3e5ec1cae9f91f947be9d4d5196",
    "apps/doom/components/doom_audio/src/doom_audio_sound_module.c": "3bb3a282461920b8254eeae0ab8b5af828a85963e899fb99d5fe27dfab1aecc1",
    "apps/doom/components/doom_audio/tests/CMakeLists.txt": "42a582c8df422f671524bfa07f3e924f3ed1c591eaae8e27f435262e64a8283c",
    "apps/doom/components/doom_audio/tests/mocks/esp_err.h": "bdb3fd6aa05f2fd2ad1b03828af5d91878a63ad0b75b8ded410ad130b7b8b1d5",
    "apps/doom/components/doom_audio/tests/mocks/freertos/FreeRTOS.h": "acdcdc12574446cd3734592ff00c23b54a7d25059c33985bcf3675a6106736fb",
    "apps/doom/components/doom_audio/tests/mocks/freertos/semphr.h": "16575c682aa97365b8f136079f6d2644afda13fbf5ee38b9c61242d3b7a5058d",
    "apps/doom/components/doom_audio/tests/mocks/freertos/task.h": "26e1204e2270174f477578cc72c5d3bef2b9c3bbe3151ae0ffab2f48276ffef8",
    "apps/doom/components/doom_audio/tests/mocks/platform/audio.h": "135374d37c8e9c8ea1dab64b42c2a1391094b750b7e375c9fc571b8768bcb115",
    "apps/doom/components/doom_audio/tests/test_doom_audio.c": "49f70148c15f6992ad07306537d8a408c9ef4ab07fc1072358d2c7016905e826",
    "apps/doom/components/doom_audio/tests/test_doom_audio_runtime.c": "32b122eada23aae624525463240f6c1bf8b3638f3e465dc4d476934d5b6185e5",
    "apps/doom/components/doom_audio/tests/test_doom_audio_wad.c": "f8f4b3728b7c05db9769eacaa54faf168f00b63fb8137b12334f72156c12d71e",
    "components/doom_video/CMakeLists.txt": "66a5b3acd466bbccd46cc7e7b4b6f90d4134e4120ac7282c43b83b9d2c772892",
    "components/doom_video/include/doom/video.h": "21afc0ea01e6bd0e629f93052e477bdc19d494489a4ec20dbb0bae32d53beabc",
    "components/doom_video/include/doom/video_convert.h": "4f7f89ff9f04af6007f7b6ca43af5150af0bd3ce23c468d2e4bf140b782705a6",
    "components/doom_video/src/doom_video_convert.c": "cd7ee358a9831d211d63a624b0833c19710b26a97b8c5d95515e5d0d3c176a2f",
    "components/doom_video/src/doom_video_espidf.c": "0e09a26765a37fad32f44668c548220a63e256d559e35a8916374e1f0708555d",
    "components/doom_video/tests/CMakeLists.txt": "6902f8b6008031c15a79f16709b0e98dce71cd95f04a54ed72fc50d66b8492c1",
    "components/doom_video/tests/test_doom_video.c": "6f9ed77ab3607eac52eb2d58e052780fc00f7e094da40d08cfd7efae339422f3",
    "components/doom_video/tests/test_pinned_doom_rgb565.c": "acd3870ee31a13a1f6b51aa500d28b0d8f06b3a7cd7bef6a1454e14d5e8b2973",
    "components/platform_audio/CMakeLists.txt": "398e59026062c128c549fcee088db1c0380e9fb8dabf707d0091c775bc7163b9",
    "components/platform_audio/Kconfig": "d92b6114da51923b7af3f0123e87d61973395ddf752a3fcb135a0b228fa15376",
    "components/platform_audio/include/platform/audio.h": "07098da697b236f65640e540443b0d6a4696ba6439ac637d34ab6ba6d1cc82c4",
    "components/platform_audio/include/platform/audio_tone.h": "90b8715f51f049cf8f73ce3dfc64a023f44bd7cc73a2d7f9011d300731434978",
    "components/platform_audio/src/platform_audio.c": "a80a5a0ade6c28f85f6e6252d1e4d1cd5d8ad17fb123d2b9b53eb7fc916f193a",
    "components/platform_audio/src/platform_audio_policy.c": "1da6ded17aa60cc9441c7b67801d4a32e178d98d38dca0891bebb204f0f18e9b",
    "components/platform_audio/src/platform_audio_policy.h": "8052e63f74b18199f4c8267e80dba096d78ecda7e2ce10a76a5202a788192ca3",
    "components/platform_audio/src/platform_audio_tone.c": "a1e482a8ef5d8b4c26ca391f3a8d9152956e21794a584728245eeed42ee6252c",
    "components/platform_audio/tests/CMakeLists.txt": "2720bcec6e6bc680db2e2b97149ae45f5ba11c0b954a936bc67c59ae610bfeba",
    "components/platform_audio/tests/mocks/driver/gpio.h": "5d93281644a82e43a474ba8ba7567767f0545b1d9790c5dc3b0620ad03326aed",
    "components/platform_audio/tests/mocks/driver/i2s_std.h": "b6714cd06cc81de0da7fde8bd99548fbcbf7c5d5f2285936820bf749a57d9b8e",
    "components/platform_audio/tests/mocks/esp_err.h": "4be4b9e70e51e383b02a61542fbc9b2ca947144848ef5e1389900069bd77c9c6",
    "components/platform_audio/tests/mocks/freertos/FreeRTOS.h": "f8efda89f80a980a3a574f83666794b021eed21b99a356ee787996408e6219f7",
    "components/platform_audio/tests/mocks/freertos/task.h": "151d90c44d210bbcf5bb5db9862f9d6fc2e0fdb104f17660464e5e4ca4a10de7",
    "components/platform_audio/tests/mocks/mock_esp32_runtime.c": "e9d24e5d86bb005cfd40f6420e3b368017dba69d3d2c345f0883fe186b18898c",
    "components/platform_audio/tests/mocks/mock_esp32_runtime.h": "b9accc3cfc429a0cda53caf4afc83a750190080605c286f8e8b2d5570d95780c",
    "components/platform_audio/tests/mocks/sdkconfig.h": "8c48e0d8ec477fe5b2f48ad16ef6f95391a5fb8bae4c0301f0382fe74457c46d",
    "components/platform_audio/tests/test_platform_audio.c": "19b21866576c20cbcaa82c971a98cd185aca5fb2656fc02bde7c87c928647c6d",
    "components/platform_audio/tests/test_platform_audio_runtime.c": "c8ac326e28c4f5ba9eb750bdfbb3418e5b01bcf3937c360993e5fa9a30911dc9",
    "components/platform_display/CMakeLists.txt": "306e6e6bf37016f6df2ce9da9989b86e7a4a2c851b67d02e0ebf9930b1c4c5ab",
    "components/platform_display/Kconfig": "6aef47cead043a9c7ed68cd3ec0a50be9f75b32cbbd1cc98af9785409305a6b8",
    "components/platform_display/include/platform/display.h": "df4aa1457e25b9676061f32ff3aa304cbd30036edadf5f082bee497a22831bc1",
    "components/platform_display/src/platform_display.c": "fc81de0f6b2995b371751740add26ab9cf1e1dfed83d0a30fbdfe853fa6f55aa",
    "components/platform_display/src/platform_display_layout.c": "45941c68063a33e197603121b61dc1e0ca43856cd9ad28f9f699775bc843fb83",
    "components/platform_display/src/platform_display_layout.h": "c58e3482042d63e03dcd5d0bb33721455566054eda7108f363fac2f8d15e1e12",
    "components/platform_display/tests/CMakeLists.txt": "6fae3abd6a08a3e98620b87f213ef9ac51f805802cd0b63fe64496cc4a83b1c8",
    "components/platform_display/tests/test_platform_display_layout.c": "218d94589970a42030858d201728bcf21edf0b3ee343a25a0a95d56c5e9428e5",
    "components/platform_readonly_blob/CMakeLists.txt": "a687e6ae8152256a65e1cce04e8022d87fde26b066717571732d0b4eef9399b2",
    "components/platform_readonly_blob/include/platform/readonly_blob.h": "de016ed704770afed8d1ab23c34deb9345d231c8622a5b72aa35e282e17b8ded",
    "components/platform_readonly_blob/src/platform_readonly_blob_vfs.c": "ea942ec76f84236e94155cbed316ecc1a361a3bcf153d3f5e2cfdc6727bf1144",
    "components/platform_readonly_blob/src/readonly_blob_core.c": "19a4b50357bf1ba990edaa3ae7341f7fad590d13f7241f5809e302b3b5c610b5",
    "components/platform_readonly_blob/src/readonly_blob_core.h": "65649cf048f41f3f7f215998dbd1fd151a18b2867da9ae57fd2ca8a455f584e3",
    "components/platform_readonly_blob/tests/CMakeLists.txt": "c7bcbed34d9f98e11008d51e4aa194473805ab886446da133674c4cb27852cda",
    "components/platform_readonly_blob/tests/test_readonly_blob_core.c": "cffe9de14e59f451d82948b009324f78b7a5cc292e8410d6931e73dbb69b740b",
    "third_party/source-lock.json": "b4692240698e88dd3bd7ea1cb7f1d88a8cb87c520a081c97e58586ccf02893dc",
    "third_party/doomgeneric.git-tree": "499fd1de6e8809099ba1ae5c75fb5eebde7a66f68950acfb9c72683c5279f2fe",
    "third_party/game-data.json": "07cfd76665309467f8a0606c7f7fcc58928d07ce721bebb070a06fa8662ed606",
    "toolchain.lock.json": "0539c66e7aa2de5d5564afea86e6412174185602fdea8efbe068676b51867936",
}


def fail(message: str) -> None:
    raise SystemExit(f"doom_embedded_audio verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        fail(f"cannot hash {path}: {error}")
    return digest.hexdigest()


def checked_child(directory: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    candidate = (directory / relative).resolve()
    require(candidate.is_relative_to(directory), f"{label} leaves build directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def command_output(arguments: list[str], label: str) -> str:
    try:
        result = subprocess.run(
            arguments, check=True, capture_output=True, encoding="utf-8"
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"{label} failed: {error}")
    return result.stdout


def ordered(text: str, tokens: tuple[str, ...], message: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        require(position >= 0, f"{message}: missing {token!r}")
        cursor = position + len(token)


def partition_entries(path: pathlib.Path) -> list[tuple[str, int, int, int, int, int]]:
    try:
        data = path.read_bytes()
    except OSError as error:
        fail(f"cannot read partition table: {error}")
    entries: list[tuple[str, int, int, int, int, int]] = []
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            break
        magic, part_type, subtype, part_offset, size, name, flags = struct.unpack(
            "<HBBII16sI", entry
        )
        if magic == 0xEBEB:
            break
        require(magic == 0x50AA, f"invalid partition entry at byte {offset}")
        label = name.split(b"\0", 1)[0].decode("ascii", errors="strict")
        entries.append((label, part_type, subtype, part_offset, size, flags))
    return entries


def verify_metadata(mode: str) -> dict:
    metadata = load_json(APP_DIR / "app-metadata.json")
    require(
        metadata.get("schema") == 1
        and metadata.get("app") == "doom_embedded_audio"
        and metadata.get("stage") == "E2-composite-build-tested-runtime-denied"
        and metadata.get("evidence_class") == "build-tested"
        and metadata.get("build_evidence") == BUILD_EVIDENCE_REL,
        "E2 metadata identity/evidence binding changed",
    )
    for key in (
        "flash_authorized",
        "flash_project_authorized",
        "game_data_redistribution_authorized",
    ):
        require(metadata.get(key) is False, f"{key} must remain false")
    require(metadata.get("runtime_evidence") is None, "runtime evidence is not reviewed")
    require(metadata.get("game_data_embedded") is True, "embedded WAD declaration changed")
    require(metadata.get("game_data_committed") is False, "metadata claims WAD is committed")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES,
            "E2 hardware interface scope changed")
    require(
        set(metadata.get("hardware_interfaces_explicitly_absent", [])) == EXPECTED_ABSENT,
        "E2 denied interface set changed",
    )
    if mode == "build-only":
        require(metadata.get("runtime_supported") is False,
                "build-only metadata must keep runtime unsupported")
        require(metadata.get("flash_app_authorized") is False,
                "build-only metadata must keep app flash false")
        require(
            metadata.get("one_shot_authorization_evidence") is None,
            "E2 authorization placeholder must remain null before D2.3 acceptance",
        )
    else:
        verify_future_app_flash_gate(metadata)
    return metadata


def verify_evidence() -> dict:
    require(BUILD_EVIDENCE.is_file(), "E2 build evidence is missing")
    require(sha256_file(BUILD_EVIDENCE) == BUILD_EVIDENCE_SHA256,
            "E2 build evidence changed")
    evidence = load_json(BUILD_EVIDENCE)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification") == "build-tested-runtime-denied"
        and evidence.get("result") == "pass-build-only-runtime-denied"
        and evidence.get("scope")
        == "Embedded-shareware-WAD Doom composite with display and reusable direct-I2S SFX audio; neutral input",
        "E2 build evidence classification changed",
    )
    require(evidence.get("source_inventory") == EXPECTED_SOURCE_INVENTORY,
            "frozen E2 source inventory changed")
    for relative, expected_hash in EXPECTED_SOURCE_INVENTORY.items():
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT), f"source leaves repository: {relative}")
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source changed: {relative}")

    artifacts = evidence.get("artifacts", {})
    require(
        artifacts.get("app_binary_bytes") == APP_BYTES
        and artifacts.get("app_binary_sha256") == APP_SHA256
        and artifacts.get("elf_bytes") == ELF_BYTES
        and artifacts.get("elf_sha256") == ELF_SHA256
        and artifacts.get("bootloader_bytes") == BOOTLOADER_BYTES
        and artifacts.get("bootloader_sha256") == BOOTLOADER_SHA256
        and artifacts.get("partition_table_bytes") == PARTITION_BYTES
        and artifacts.get("partition_table_sha256") == PARTITION_SHA256,
        "reviewed E2 artifact identities changed",
    )
    reproducibility = evidence.get("reproducibility", {})
    identities = reproducibility.get("identities", [])
    exact_identity = {
        "app_binary_bytes": APP_BYTES,
        "app_binary_sha256": APP_SHA256,
        "elf_bytes": ELF_BYTES,
        "elf_sha256": ELF_SHA256,
        "bootloader_bytes": BOOTLOADER_BYTES,
        "bootloader_sha256": BOOTLOADER_SHA256,
        "partition_table_bytes": PARTITION_BYTES,
        "partition_table_sha256": PARTITION_SHA256,
    }
    require(
        reproducibility.get("clean_builds_compared") == 3
        and reproducibility.get("independent_build_directories_compared") == 2
        and reproducibility.get("identical_application_binary") is True
        and reproducibility.get("identical_application_elf") is True
        and reproducibility.get("identical_bootloader_binary") is True
        and reproducibility.get("identical_partition_table_binary") is True
        and isinstance(identities, list)
        and [entry.get("label") for entry in identities]
        == ["canonical", "independent-a", "independent-b"]
        and all(
            {key: entry.get(key) for key in exact_identity} == exact_identity
            for entry in identities
        ),
        "exact canonical-plus-two artifact identities changed",
    )
    build = evidence.get("build", {})
    require(
        build.get("reproducible_build") is True
        and build.get("compile_time_date_enabled") is False
        and build.get("independent_build_directories_compared") == 2
        and build.get("canonical_build_also_compared") is True
        and build.get("identical_binary") is True
        and build.get("identical_elf") is True
        and build.get("identical_bootloader") is True
        and build.get("identical_partition_table") is True
        and build.get("generated_flash_after_action") == "no_reset"
        and build.get("application_offset") == "0x10000"
        and build.get("application_partition_bytes") == APP_CAPACITY,
        "canonical-plus-two reproducibility or no-reset contract changed",
    )
    engine = evidence.get("engine", {})
    require(
        engine.get("commit") == DOOM_COMMIT
        and engine.get("tree") == DOOM_TREE
        and engine.get("vendor_manifest_sha256") == DOOM_MANIFEST_SHA256
        and engine.get("feature_sound_enabled") is True
        and engine.get("music_disabled") is True,
        "reviewed Doom engine/audio policy changed",
    )
    host = evidence.get("host_tests", {})
    require(
        host.get("doom_audio_command_ring") == "pass"
        and host.get("doom_audio_runtime") == "pass"
        and host.get("doom_audio_wad_decode") == "pass"
        and host.get("cases") == 3
        and host.get("sanitizers") == ["AddressSanitizer", "UndefinedBehaviorSanitizer"]
        and host.get("worker_stack_hwm_initial_value") == 0xFFFFFFFF
        and host.get("worker_stack_hwm_published_by_worker_only") is True,
        "reviewed Doom audio host acceptance changed",
    )
    safety = evidence.get("safety", {})
    require(
        safety.get("literal_first_app_main_statement")
        == "platform_audio_force_safe_shutdown"
        and safety.get("display_initializes_dark_and_owns_ldo3_ldo4") is True
        and safety.get("audio_config")
        == {
            "control_bus": None,
            "sample_rate_hz": 16000,
            "volume_percent": 10,
            "absolute_pcm_cap": 512,
        }
        and safety.get("audio_bound_before_doomgeneric_create") is True
        and safety.get("cleanup_stop_attempt_unconditional") is True
        and safety.get("unbind_requires_accepted_stop_or_confirmed_already_muted_state") is True
        and safety.get("destroy_requires_confirmed_no_worker") is True
        and safety.get("failed_create_hidden_owner_recovered_before_rail_release") is True
        and safety.get("display_and_rails_retained_on_worker_destroy_or_recovery_failure") is True
        and safety.get("halt_retries_retained_cleanup") is True
        and safety.get("spiram_malloc_alwaysinternal_threshold_bytes") == 1024
        and safety.get("memory_telemetry_internal_and_psram") is True
        and safety.get("worker_stack_hwm_telemetry") is True,
        "reviewed E2 lifecycle/memory contract changed",
    )
    execution = evidence.get("execution", {})
    require(
        execution
        == {
            "firmware_flashed": False,
            "firmware_executed": False,
            "hardware_accessed": False,
            "audio_path_energized": False,
            "runtime_supported": False,
            "flash_authorized": False,
            "flash_app_authorized": False,
            "flash_project_authorized": False,
        },
        "build evidence makes an unsupported execution/authorization claim",
    )
    return evidence


def verify_local_wad() -> bytes:
    require(LOCAL_WAD.is_file(), f"exact ignored local WAD is missing: {LOCAL_WAD}")
    require(LOCAL_WAD.stat().st_size == WAD_BYTES, "local WAD byte count changed")
    require(sha256_file(LOCAL_WAD) == WAD_SHA256, "local WAD SHA-256 changed")
    try:
        data = LOCAL_WAD.read_bytes()
    except OSError as error:
        fail(f"cannot read local WAD: {error}")
    require(data[:4] == b"IWAD" and len(data) >= 12, "local WAD header is invalid")
    lump_count, directory_offset = struct.unpack_from("<II", data, 4)
    require(
        lump_count > 0
        and directory_offset <= len(data)
        and lump_count * 16 <= len(data) - directory_offset,
        "local WAD directory is out of bounds",
    )
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", str(LOCAL_WAD.relative_to(ROOT))],
        cwd=ROOT,
        check=False,
    )
    require(ignored.returncode == 0, "local WAD is not ignored by Git")
    tracked = subprocess.run(
        ["git", "ls-files", "--error-unmatch", str(LOCAL_WAD.relative_to(ROOT))],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    require(tracked.returncode != 0, "local WAD is tracked by Git")
    for source_root in ("apps", "components", "docs", "hardware", "scripts", "test-runs", "third_party"):
        for candidate in (ROOT / source_root).rglob("*"):
            if candidate.is_file() and candidate.suffix.lower() == ".wad":
                fail(f"WAD file exists in source/evidence: {candidate.relative_to(ROOT)}")
    return data


def verify_source_contract() -> None:
    app = (APP_DIR / "main/doom_embedded_audio_main.c").read_text()
    app_main = app[app.index("void app_main(void)") :]
    ordered(
        app_main,
        (
            "platform_audio_force_safe_shutdown()",
            "P4_DOOM_AUDIO E2 START",
            "platform_display_init()",
            "I_AtExit(engine_exit_composite, true)",
            "verify_embedded_wad(wad_start, wad_size)",
            "platform_readonly_blob_register(&blob_config)",
            "verify_readonly_vfs()",
            "doom_video_init()",
            "doom_video_submit_black",
            "platform_audio_create(&audio_config, &s_audio)",
            "doom_audio_runtime_bind(&doom_audio_config)",
            "platform_display_set_brightness(25U)",
            "doomgeneric_Create(argc, argv)",
        ),
        "E2 safe startup/display/audio/engine order changed",
    )
    require(
        ".control_bus = NULL" in app
        and ".sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ" in app
        and ".volume_percent = 10U" in app
        and ".display_owns_ldo3_ldo4 = true" in app,
        "E2 platform_audio/rail ownership config changed",
    )
    require(
        '"-nomusic"' in app
        and '"-nosound"' not in app
        and "*pressed = 0" in app
        and "*key = 0U" in app,
        "E2 music/SFX/neutral-input policy changed",
    )
    require(
        "platform_storage" not in app
        and "sdmmc" not in app.lower()
        and "usb_host" not in app.lower()
        and "i2c_" not in app.lower()
        and "esp_codec" not in app.lower(),
        "out-of-scope hardware API entered E2 app source",
    )
    cleanup = app[app.index("static void composite_cleanup") : app.index("static void halt_dark")]
    ordered(
        cleanup,
        (
            "platform_audio_force_safe_shutdown()",
            "doom_audio_runtime_stop()",
            "doom_audio_runtime_unbind()",
            "platform_audio_destroy(&s_audio)",
            "platform_audio_recover()",
            "doom_video_deinit()",
            "platform_display_deinit()",
        ),
        "E2 composite cleanup order changed",
    )
    require(
        "backend_released = no_worker_confirmed && s_audio == NULL" in cleanup
        and "destroy_confirmed && recover_result == ESP_OK" in cleanup
        and "if (backend_released)" in cleanup
        and "if (!s_video_initialized && s_display_initialized)" in cleanup,
        "display/rail teardown is no longer gated on complete backend proof",
    )
    halt = app[app.index("static void halt_dark") : app.index("static void engine_exit_composite")]
    require(
        halt.count("composite_cleanup()") == 2
        and "for (;;)" in halt
        and "vTaskDelay(pdMS_TO_TICKS(1000))" in halt,
        "halt no longer retries retained cleanup",
    )
    require(
        "worker_stack_hwm_bytes" in app
        and 'log_memory_snapshot("pre-audio-create")' in app
        and 'log_memory_snapshot("post-audio-create")' in app
        and 'log_memory_snapshot("post-engine-audio-init")' in app,
        "E2 memory/worker telemetry changed",
    )

    defaults = (APP_DIR / "sdkconfig.defaults").read_text()
    required_defaults = (
        "CONFIG_ESPTOOLPY_AFTER_NORESET=y",
        "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024",
        "CONFIG_PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED=y",
        "CONFIG_PLATFORM_AUDIO_ELECROW_10_1_REVIEWED_BUILD_ONLY=y",
        "CONFIG_DOOM_AUDIO_ENGINE_ADAPTER=y",
    )
    require(all(token in defaults for token in required_defaults),
            "E2 no-reset/memory/component Kconfig changed")

    main_cmake = (APP_DIR / "main/CMakeLists.txt").read_text()
    require(
        '"${CMAKE_CURRENT_LIST_DIR}/../../../local-data/doom/doom1.wad"' in main_cmake
        and "P4_EXPECTED_DOOM_WAD_BYTES 4196020" in main_cmake
        and WAD_SHA256 in main_cmake
        and "target_add_binary_data(" in main_cmake
        and "RENAME_TO doom_shareware_wad" in main_cmake,
        "build-time exact-WAD gate/linker name changed",
    )
    for forbidden in ("platform_storage", "usb", "esp_codec_dev", "esp_driver_i2c"):
        require(forbidden not in main_cmake, f"forbidden main dependency entered: {forbidden}")

    audio_cmake = (ROOT / "components/platform_audio/CMakeLists.txt").read_text()
    require(
        "esp_driver_gpio" in audio_cmake
        and "esp_driver_i2s" in audio_cmake
        and "freertos" in audio_cmake
        and "esp_driver_i2c" not in audio_cmake
        and "esp_codec_dev" not in audio_cmake,
        "platform_audio direct-only dependency seam changed",
    )
    sound_module = (
        ROOT / "apps/doom/components/doom_audio/src/doom_audio_sound_module.c"
    ).read_text()
    require(
        "music_init" in sound_module
        and "return false;" in sound_module[sound_module.index("static boolean music_init") :]
        and "SNDDEVICE_NONE" in sound_module
        and "doom_audio_runtime_start()" in sound_module
        and "doom_audio_runtime_stop()" in sound_module,
        "Doom SFX-enabled/music-stub seam changed",
    )


def verify_engine_provenance() -> None:
    result = subprocess.run(
        [str(ROOT / "scripts/doom/verify-doomgeneric.sh")],
        cwd=ROOT,
        capture_output=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        fail(f"doomgeneric provenance check failed: {result.stderr.strip()}")
    require(
        f"commit={DOOM_COMMIT} tree={DOOM_TREE} files=205" in result.stdout,
        "doomgeneric provenance marker changed",
    )


def verify_reviewed_source_layout() -> None:
    # Preserve the full reviewed source layout without a captured firmware image.
    with (APP_DIR / "partitions.csv").open(newline="") as source:
        rows = [row for row in csv.reader(line for line in source
                if not line.lstrip().startswith("#")) if row]
    require(all(len(row) >= 5 for row in rows), "malformed source partition layout")
    layout = [{"name": row[0].strip(), "type": row[1].strip(),
               "subtype": row[2].strip(), "offset": row[3].strip(),
               "size": row[4].strip()} for row in rows]
    expected = [
        {"name": "nvs", "type": "data", "subtype": "nvs", "offset": "0x9000", "size": "24K"},
        {"name": "phy_init", "type": "data", "subtype": "phy", "offset": "0xf000", "size": "4K"},
        {"name": "factory", "type": "app", "subtype": "factory", "offset": "0x10000", "size": "11M"},
        {"name": "storage", "type": "data", "subtype": "spiffs", "offset": "0xb10000", "size": "4M"},
    ]
    require(layout == expected, "reviewed source partition layout changed")


def verify_build_graph(build_dir: pathlib.Path, evidence: dict, wad: bytes) -> pathlib.Path:
    lock = load_json(ROOT / "toolchain.lock.json")
    target = lock.get("target", {})
    toolchain = evidence.get("toolchain", {})
    build = evidence.get("build", {})
    require(
        toolchain.get("esp_idf_version") == lock["esp_idf"]["version"]
        and toolchain.get("esp_idf_commit") == lock["esp_idf"]["git_commit"]
        and build.get("target") == target.get("chip")
        and build.get("minimum_revision_full") == target.get("min_revision_full")
        and build.get("maximum_revision_full") == target.get("max_revision_full")
        and build.get("flash_size_bytes") == 16777216,
        "E2 evidence differs from pinned toolchain/target",
    )
    lock_text = (APP_DIR / "dependencies.lock").read_text()
    require(sha256_file(APP_DIR / "dependencies.lock")
            == "705d29841eb277c84a8330b3eb0b1978f508fc96e5a7f1687c68a0641447e54a",
            "E2 dependency lock changed")
    require(
        "version: 5.5.3" in lock_text
        and "version: 1.0.2" in lock_text
        and "esp_codec_dev" not in lock_text,
        "locked IDF/display/no-codec dependency policy changed",
    )

    sdk = load_json(build_dir / "config/sdkconfig.json")
    require(
        sdk.get("APP_REPRODUCIBLE_BUILD") is True
        and sdk.get("APP_COMPILE_TIME_DATE") is not True
        and sdk.get("IDF_TARGET") == "esp32p4"
        and sdk.get("ESP32P4_SELECTS_REV_LESS_V3") is True
        and sdk.get("ESP32P4_REV_MIN_100") is True
        and sdk.get("ESP32P4_REV_MIN_FULL") == 100
        and sdk.get("ESPTOOLPY_FLASHSIZE") == "16MB"
        and sdk.get("ESPTOOLPY_FLASHFREQ") == "80m"
        and sdk.get("ESPTOOLPY_AFTER_NORESET") is True
        and sdk.get("ESPTOOLPY_AFTER") == "no_reset",
        "generated pinned/reproducible/no-reset configuration changed",
    )
    require(
        sdk.get("SPIRAM") is True
        and sdk.get("SPIRAM_USE_MALLOC") is True
        and sdk.get("SPIRAM_MALLOC_ALWAYSINTERNAL") == 1024
        and sdk.get("ESP_MAIN_TASK_STACK_SIZE") == 24576
        and sdk.get("VFS_SUPPORT_DIR") is True
        and sdk.get("PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True
        and sdk.get("PLATFORM_AUDIO_ELECROW_10_1_REVIEWED_BUILD_ONLY") is True
        and sdk.get("DOOM_AUDIO_ENGINE_ADAPTER") is True
        and sdk.get("PLATFORM_STORAGE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is not True,
        "generated E2 memory/display/audio/no-storage config changed",
    )

    description = load_json(build_dir / "project_description.json")
    require(
        description.get("project_name") == "p4_doom_embedded_audio"
        and description.get("project_version") == "0.2.0"
        and description.get("target") == target.get("chip")
        and int(description.get("min_rev")) == target.get("min_revision_full")
        and int(description.get("max_rev")) == target.get("max_revision_full")
        and description.get("git_revision") == lock["esp_idf"]["git_tag"],
        "generated E2 project/toolchain identity changed",
    )
    components = set(description.get("build_components", []))
    require(
        {"doom_audio", "doom_engine_audio", "doom_video", "platform_audio",
         "platform_display", "platform_readonly_blob"} <= components,
        "required E2 service/engine component is absent",
    )
    require(
        "platform_storage" not in components
        and "esp_codec_dev" not in components
        and "usb" not in components
        and "usb_host_hid" not in components,
        "storage/codec/USB component entered E2",
    )
    info = description.get("build_component_info", {})
    require(
        info.get("platform_audio", {}).get("reqs")
        == ["esp_driver_gpio", "esp_driver_i2s", "freertos"]
        and info.get("doom_audio", {}).get("reqs") == ["freertos", "platform_audio"]
        and info.get("doom_engine_audio", {}).get("reqs") == ["doom_audio"],
        "platform_audio/doom_audio/engine dependency seam changed",
    )

    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    boot = flash_args.get("bootloader", {})
    part = flash_args.get("partition-table", {})
    require(
        int(str(app.get("offset")), 0) == APP_OFFSET
        and int(str(boot.get("offset")), 0) == 0x2000
        and int(str(part.get("offset")), 0) == 0x8000
        and flash_args.get("flash_settings", {}).get("flash_size") == "16MB"
        and flash_args.get("extra_esptool_args", {}).get("after") == "no_reset",
        "generated offsets/flash size/no-reset action changed",
    )
    binary = checked_child(build_dir, app.get("file"), "application binary")
    elf = checked_child(build_dir, description.get("app_elf"), "application ELF")
    boot_binary = checked_child(build_dir, boot.get("file"), "bootloader binary")
    part_binary = checked_child(build_dir, part.get("file"), "partition table binary")
    expected = (
        (binary, APP_BYTES, APP_SHA256),
        (elf, ELF_BYTES, ELF_SHA256),
        (boot_binary, BOOTLOADER_BYTES, BOOTLOADER_SHA256),
        (part_binary, PARTITION_BYTES, PARTITION_SHA256),
    )
    for path, size, digest in expected:
        require(path.stat().st_size == size, f"{path.name} byte count changed")
        require(sha256_file(path) == digest, f"{path.name} SHA-256 changed")
    require(APP_CAPACITY - APP_BYTES == 6676304, "E2 app partition free space changed")
    require(partition_entries(part_binary) == EXPECTED_PARTITIONS,
            "generated partition table differs from saved factory layout")

    try:
        binary_bytes = binary.read_bytes()
    except OSError as error:
        fail(f"cannot inspect application binary: {error}")
    require(binary_bytes.find(wad) == WAD_BINARY_OFFSET,
            "exact WAD binary offset changed")
    require(binary_bytes.find(wad, WAD_BINARY_OFFSET + 1) == -1,
            "exact WAD appears more than once")
    require(binary_bytes.count(bytes.fromhex(WAD_SHA256)) == 1,
            "exact WAD digest occurrence changed")

    generated_wad = build_dir / "doom1.wad.S"
    require(generated_wad.is_file(), "generated WAD assembly is missing")
    build_ninja = (build_dir / "build.ninja").read_text()
    require(
        str(LOCAL_WAD) in build_ninja
        and "doom1.wad.S" in build_ninja
        and str(LOCAL_WAD) in generated_wad.read_text(errors="strict")[:1000]
        and "_binary_doom_shareware_wad_start" in generated_wad.read_text(errors="strict")[:1000],
        "exact one-input WAD build seam changed",
    )

    commands = load_json_array(build_dir / "compile_commands.json")
    engine_sources = {
        pathlib.Path(item["file"]).name
        for item in commands
        if isinstance(item, dict)
        and isinstance(item.get("file"), str)
        and (
            "/third_party/doomgeneric/doomgeneric/" in item["file"]
            or item["file"].endswith("/doom_i_sound_feature.c")
        )
    }
    require(len(engine_sources) == 80 and "doom_i_sound_feature.c" in engine_sources,
            "audio-enabled engine compiled source set changed")
    archive = build_dir / "esp-idf/doom_engine_audio/libdoom_engine_audio.a"
    require(archive.is_file(), "audio engine archive is missing")
    compiler = description.get("c_compiler")
    require(isinstance(compiler, str) and compiler, "compiler path is absent")
    tool_dir = pathlib.Path(compiler).parent
    ar = tool_dir / "riscv32-esp-elf-ar"
    nm = tool_dir / "riscv32-esp-elf-nm"
    objdump = tool_dir / "riscv32-esp-elf-objdump"
    require(ar.is_file() and nm.is_file() and objdump.is_file(),
            "pinned ELF inspection tools are missing")
    members = command_output([str(ar), "t", str(archive)], "engine archive inspection").splitlines()
    require(len(members) == len(set(members)) == 80,
            "audio engine archive member set changed")
    map_path = build_dir / "p4_doom_embedded_audio.map"
    require(map_path.is_file(), "E2 linker map is missing")
    map_text = map_path.read_text(errors="replace")
    map_members = set(re.findall(
        r"esp-idf/doom_engine_audio/libdoom_engine_audio\.a\(([^)]+\.c\.obj)\)",
        map_text,
    ))
    require(len(map_members) == 80, "audio engine whole-archive map coverage changed")
    require(
        "-Wl,--whole-archive  esp-idf/doom_engine_audio/libdoom_engine_audio.a  -Wl,--no-whole-archive"
        in build_ninja,
        "audio engine is no longer whole-archive linked",
    )

    symbols_text = command_output([str(nm), "-n", "-g", str(elf)], "ELF symbols")
    symbols = set(re.findall(r"^[0-9a-fA-F]+\s+[A-Za-z]\s+(\S+)$", symbols_text, re.MULTILINE))
    required_symbols = {
        "app_main", "doomgeneric_Create", "doomgeneric_Tick", "DG_Init",
        "DG_DrawFrame", "DG_GetKey", "doom_video_init", "doom_video_submit_black",
        "doom_video_submit_xrgb8888", "platform_display_init",
        "platform_display_deinit", "platform_display_set_brightness",
        "platform_readonly_blob_register", "doom_audio_runtime_bind",
        "doom_audio_runtime_start", "doom_audio_runtime_stop",
        "doom_audio_runtime_unbind", "doom_audio_runtime_get_stats",
        "platform_audio_force_safe_shutdown", "platform_audio_recover",
        "platform_audio_create", "platform_audio_start", "platform_audio_write_frames",
        "platform_audio_stop", "platform_audio_destroy", "i2s_channel_init_std_mode",
        "i2s_channel_preload_data", "i2s_channel_enable", "i2s_channel_write",
        "i2s_channel_disable", "i2s_del_channel", "DG_sound_module", "DG_music_module",
        "_binary_doom_shareware_wad_start", "_binary_doom_shareware_wad_end",
    }
    require(required_symbols <= symbols, "required E2 engine/display/audio symbol is absent")
    forbidden_symbols = {
        "platform_storage_init", "platform_storage_get_card_info",
        "platform_storage_find_doom_shareware", "esp_vfs_fat_sdmmc_mount",
        "esp_vfs_fat_sdspi_mount", "sdmmc_host_init", "sdspi_host_init",
        "usb_host_install", "hid_host_install", "platform_usb_host_start",
        "platform_gamepad_usb_start", "i2c_new_master_bus", "esp_codec_dev_open",
        "es8311_codec_new",
    }
    require(not (forbidden_symbols & symbols),
            "SD/USB/I2C/codec runtime API reached E2 ELF")
    values = {
        name: int(value, 16)
        for value, name in re.findall(
            r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(_binary_doom_shareware_wad_(?:start|end))$",
            symbols_text,
            re.MULTILINE,
        )
    }
    require(
        values.get("_binary_doom_shareware_wad_start") == WAD_ELF_START
        and values.get("_binary_doom_shareware_wad_end") == WAD_ELF_END
        and WAD_ELF_END - WAD_ELF_START == WAD_BYTES,
        "embedded WAD ELF span changed",
    )
    disassembly = command_output(
        [str(objdump), "-d", "--disassemble=app_main", str(elf)],
        "app_main disassembly",
    )
    calls = re.findall(r"\b(?:jal|jalr)\b[^\n]*<([^>]+)>", disassembly)
    require(calls and calls[0] == "platform_audio_force_safe_shutdown",
            "platform_audio force-safe is not the first app_main call")
    return binary


def load_json_array(path: pathlib.Path) -> list:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, list), f"{path} must contain an array")
    return value


def verify_future_app_flash_gate(metadata: dict) -> None:
    # Every condition below is intentionally unmet in the build-only tree.
    # When D2.3 passes, bind the exact reviewed records without weakening any
    # assertion or allowing the legacy/full-project flags.
    require(metadata.get("flash_authorized") is False,
            "legacy broad flash must remain permanently false")
    require(metadata.get("flash_project_authorized") is False,
            "full-project flash must remain permanently false")
    require(metadata.get("flash_app_authorized") is True,
            "E2 app-only flash has not been explicitly activated")
    require(metadata.get("runtime_supported") is True,
            "E2 one-shot runtime has not been activated")
    require(metadata.get("one_shot_authorization_evidence") == E2_AUTHORIZATION_REL,
            "E2 metadata is not bound to the exact one-shot authorization")
    require(metadata.get("runtime_evidence") is None,
            "pre-run E2 metadata must not claim runtime evidence")

    d23_path = ROOT / D23_RUNTIME_REL
    require(d23_path.is_file(), "D2.3 acoustic runtime acceptance is missing")
    require(
        isinstance(D23_RUNTIME_SHA256, str)
        and re.fullmatch(r"[0-9a-f]{64}", D23_RUNTIME_SHA256) is not None
        and sha256_file(d23_path) == D23_RUNTIME_SHA256,
        "final D2.3 runtime evidence SHA-256 is not pinned or differs",
    )
    d23 = load_json(d23_path)
    firmware = d23.get("firmware", {})
    runtime = d23.get("runtime", {})
    acoustic = d23.get("acoustic_acceptance", {})
    authorization = d23.get("one_shot_authorization", {})
    require(
        d23.get("schema") == 1
        and d23.get("classification") == "hardware-tested"
        and d23.get("result") == "pass-reusable-direct-i2s-acoustic"
        and firmware.get("app_binary_bytes") == D23_DIAG_BYTES
        and firmware.get("app_binary_sha256") == D23_DIAG_SHA256
        and firmware.get("installed_app_readback_matches_binary") is True
        and firmware.get("launch_count_after_readback") == 1,
        "D2.3 exact artifact/readback/single-launch acceptance changed",
    )
    require(
        runtime.get("ordered_serial_markers")
        == ["CAPTURE_ARM", "TONE_BEGIN", "POSTROLL", "PASS", "HEARTBEAT"]
        and runtime.get("pass_marker_exact") is True
        and runtime.get("heartbeat_marker_exact") is True
        and runtime.get("reject_markers_seen") == [],
        "D2.3 ordered serial/PASS/heartbeat acceptance changed",
    )
    require(
        isinstance(D23_ANALYZER_SHA256, str)
        and re.fullmatch(r"[0-9a-f]{64}", D23_ANALYZER_SHA256) is not None
        and (ROOT / D23_ANALYZER_REL).is_file()
        and sha256_file(ROOT / D23_ANALYZER_REL) == D23_ANALYZER_SHA256
        and acoustic.get("analyzer") == D23_ANALYZER_REL
        and acoustic.get("analyzer_sha256") == D23_ANALYZER_SHA256
        and acoustic.get("result") == "pass"
        and acoustic.get("serial_correlated") is True
        and acoustic.get("sustained_tone_events") == 1
        and acoustic.get("event_duration_ms", 0) >= 300
        and acoustic.get("event_duration_ms", 0) <= 650
        and abs(acoustic.get("dominant_frequency_hz", 0) - 440) <= 5
        and acoustic.get("dominant_over_pre_post_noise_db", 0) >= 12
        and acoustic.get("adjacent_band_margin_db", 0) >= 6
        and acoustic.get("operator_audibility_confirmed") is True
        and acoustic.get("pop_observation_recorded_separately") is True,
        "D2.3 synchronized analyzer/operator acoustic acceptance changed",
    )
    require(
        authorization.get("consumed") is True
        and authorization.get("revoked") is True,
        "D2.3 one-shot authorization was not consumed and revoked",
    )

    auth_path = ROOT / E2_AUTHORIZATION_REL
    require(auth_path.is_file(), "exact E2 one-shot authorization is missing")
    require(
        isinstance(E2_AUTHORIZATION_SHA256, str)
        and re.fullmatch(r"[0-9a-f]{64}", E2_AUTHORIZATION_SHA256) is not None
        and sha256_file(auth_path) == E2_AUTHORIZATION_SHA256,
        "final E2 one-shot authorization SHA-256 is not pinned or differs",
    )
    auth = load_json(auth_path)
    require(
        auth.get("schema") == 1
        and auth.get("classification") == "user-requested-doom-e2-one-shot"
        and auth.get("result") == "authorized-exact-app-preflash-reviewed"
        and auth.get("scope") == "doom_embedded_audio_e2_single_connected_unit_run"
        and auth.get("d23_acoustic_prerequisite") == D23_RUNTIME_REL
        and auth.get("exact_artifact")
        == {"offset": "0x10000", "bytes": APP_BYTES, "sha256": APP_SHA256}
        and auth.get("exception_boundary")
        == {
            "application_flash_scope": "factory_app_partition_only_at_0x10000",
            "full_project_flash_authorized": False,
            "single_launch_after_exact_readback": True,
            "connected_unit_only": True,
            "usb_sd_i2c_codec_music_authorized": False,
            "recognizable_sfx_operator_confirmation_required": True,
        },
        "E2 one-shot authorization escaped its exact connected-unit boundary",
    )
    verify_future_central_flash_path()


def verify_future_central_flash_path() -> None:
    flash = (ROOT / "scripts/flash.sh").read_text()
    preflight = re.search(
        r"case \"\$P4_APP\" in(?P<body>.*?)esac", flash, re.DOTALL
    )
    require(
        preflight is not None and "doom_embedded_audio" in preflight.group("body"),
        "E2 preflight is not held in the ROM loader",
    )
    hook = re.search(
        r'if \[ "\$P4_APP" = doom_embedded_audio \]; then\n(?P<body>.*?)\nfi',
        flash,
        re.DOTALL,
    )
    require(
        hook is not None
        and 'python3 "$P4_SCRIPT_DIR/verify-doom-embedded-audio.py"' in hook.group("body")
        and '"$P4_BUILD_DIR" "$P4_FLASH_TARGET"' in hook.group("body"),
        "central flash path is not bound to the E2 verifier",
    )
    blocks = [
        match
        for match in re.finditer(
            r'if \[ "\$P4_APP" = doom_embedded_audio \]; then\n(?P<body>.*?)\nfi',
            flash,
            re.DOTALL,
        )
    ]
    readback = [m for m in blocks if "P4_READBACK_AFTER=no_reset" in m.group("body")]
    launch = [m for m in blocks if "P4_RUN_OUTPUT=$(esptool.py" in m.group("body")]
    require(len(readback) == 1, "E2 has no unique no-reset readback membership")
    require(len(launch) == 1, "E2 has no unique single deferred-launch membership")

    # The verified/authorized application bytes must be copied into a private,
    # read-only snapshot and written directly. Running idf.py after verification
    # would rebuild and reopen a post-verifier artifact substitution race.
    require(
        "doom_embedded_audio" in flash[flash.index("P4_VERIFIED_DIRECT_WRITE=false") :
                                      flash.index("if [ \"$P4_RESUME_READBACK\" = true ]")]
        and 'P4_VERIFIED_APP_DIR=$(mktemp -d' in flash
        and 'cp -- "$P4_BUILT_APP_PATH" "$P4_VERIFIED_APP_PATH"' in flash
        and 'chmod 400 "$P4_VERIFIED_APP_PATH"' in flash
        and 'P4_BUILT_APP_PATH="$P4_VERIFIED_APP_PATH"' in flash
        and 'P4_VERIFIED_DIRECT_WRITE=true' in flash
        and 'elif [ "$P4_VERIFIED_DIRECT_WRITE" = true ]; then' in flash
        and '"$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_PATH"' in flash
        and "Authorized application readback identity mismatch" in flash,
        "E2 is not a sealed exact-artifact direct-write/readback member",
    )
    write_pos = flash.index("P4_FLASH_OUTPUT=$(idf.py")
    verify_pos = flash.index("if ! p4_verify_chunked_application_readback")
    hash_pos = flash.index('if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then')
    confirmed_pos = flash.index("Application readback verified:")
    run_pos = flash.index("P4_RUN_OUTPUT=$(esptool.py", launch[0].start())
    require(
        write_pos < readback[0].start() < verify_pos < hash_pos
        < confirmed_pos < launch[0].start() < run_pos,
        "E2 launch is not deferred until exact readback succeeds",
    )


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-doom-embedded-audio.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"},
            "mode must be build-only or app-flash; full-project flash is prohibited")

    verify_metadata(mode)
    evidence = verify_evidence()
    wad = verify_local_wad()
    verify_source_contract()
    verify_engine_provenance()
    verify_reviewed_source_layout()
    binary = verify_build_graph(build_dir, evidence, wad)
    print(
        "doom_embedded_audio verification: PASS "
        f"mode={mode} offset=0x{APP_OFFSET:x} bytes={binary.stat().st_size} "
        f"sha256={APP_SHA256} engine_sources=80 wad_embedded=true "
        f"wad_bytes={WAD_BYTES} wad_occurrences=1 input=neutral "
        "sfx=enabled music=disabled sd=unused usb=unused i2c=unused codec=none "
        f"runtime_authorization={'connected-unit-one-shot' if mode == 'app-flash' else 'false'}"
    )


if __name__ == "__main__":
    main()
