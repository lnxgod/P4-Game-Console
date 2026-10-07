#!/usr/bin/env python3
"""Run the production Arena startup config through the real Console OS VFS."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    startup = (ROOT / "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c").read_text()
    start = startup.index("    const uint8_t *const wad_start = &s_console_storage_wad_marker;")
    declarations = startup[start:startup.index("#else", start)]
    start = startup.index("    const platform_readonly_blob_config_t blob_config = {", start)
    config = startup[start:startup.index("    result = platform_readonly_blob_register", start)]
    metadata = json.loads((ROOT / "third_party/game-data.json").read_text())["game_changers_ai_bundle"]
    base = metadata["files"][0]
    fixture = (ROOT / "components/platform_game_storage/tests/test_arena_vfs.c").read_text()
    helper = r'''
#define EMBEDDED_WAD_PATH "/doom/doom1.wad"
static const uint8_t s_console_storage_wad_marker;
static const char *startup_identity, *startup_sha256;
static platform_readonly_blob_config_t startup_config(platform_game_storage_doom_title_t title)
{
''' + declarations + config + r'''
    assert(!strcmp(wad_path, "/doom/freedoom2.wad"));
    startup_identity = wad_identity;
    startup_sha256 = wad_sha256;
    return blob_config;
}
'''
    fixture = fixture.replace("int main(void)", helper + "\nint main(void)", 1)
    original = 'platform_readonly_blob_config_t config={"/doom","freedoom2.wad",doom,(size_t)PLATFORM_GAME_STORAGE_ARENA_BASE_WAD_BYTES};'
    assert original in fixture
    fixture = fixture.replace(original, "platform_readonly_blob_config_t config=startup_config(PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI);", 1)
    registered = "assert(platform_readonly_blob_register(&config)==ESP_OK && last_file==2);"
    assert registered in fixture
    fixture = fixture.replace(registered, registered + f'''
    assert(config.size_bytes == {base["size_bytes"]});
    assert(!strcmp(startup_identity, {json.dumps(metadata["base_game_data_id"])}));
    assert(!strcmp(startup_sha256, {json.dumps(base["sha256"])}));
''', 1)
    with tempfile.TemporaryDirectory(prefix="p4-startup-wad-vfs-") as directory:
        source = Path(directory) / "startup_wad_vfs.c"
        binary = Path(directory) / "startup_wad_vfs"
        source.write_text(fixture)
        includes = ["components/platform_game_storage/tests/mocks",
                    "components/platform_game_storage/include",
                    "components/platform_readonly_blob/include",
                    "apps/console_os/components/platform_readonly_blob/include"]
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Wconversion", "-Werror", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-DCONFIG_VFS_SUPPORT_DIR=1",
                        "-DP4_TEST_VFS_DIAGNOSTICS=1",
                        *["-I" + str(ROOT / path) for path in includes], str(source),
                        str(ROOT / "apps/console_os/components/platform_readonly_blob/platform_game_storage_blob.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("startup WAD VFS: production Arena metadata, registration, stream reads and ordinary Doom passed")


if __name__ == "__main__":
    main()
