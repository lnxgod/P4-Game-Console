#!/usr/bin/env python3

"""Create a small, buildable P4 Game API v1 project in the monorepo."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]
SLUG_RE = re.compile(r"^[a-z][a-z0-9_]{2,31}$")
TITLE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._-]*$")
ACCENT_RE = re.compile(r"^0x[0-9a-fA-F]{4}$")
FOLDER_RE = re.compile(
    r"^[A-Z0-9][A-Z0-9 -]{0,14}(?:/[A-Z0-9][A-Z0-9 -]{0,14})?$")
CAPABILITIES = {
    "audio-tone",
    "audio-stream",
    "storage",
    "signal-scan",
    "save",
    "text-input",
    "realm",
    "multiplayer-session",
    "module-handoff",
    "vector-scenes",
}


def die(message: str) -> "NoReturn":
    raise SystemExit(f"new-game: {message}")


def derived_slug(title: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", title.lower()).strip("_")
    if slug and slug[0].isdigit():
        slug = "game_" + slug
    return slug


def occupied_launcher_ids(games_root: pathlib.Path) -> set[int]:
    occupied: set[int] = set()
    for path in games_root.glob("*/game.json"):
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            die(f"cannot inspect existing manifest {path}: {error}")
        value = manifest.get("launcher_id")
        if isinstance(value, int) and not isinstance(value, bool):
            occupied.add(value)
    return occupied


def choose_launcher_id(occupied: set[int], requested: int | None) -> int:
    if requested is not None:
        if requested < 100 or requested > 0xFFFFFFFF:
            die("--launcher-id must be in 100..4294967295")
        if requested in occupied:
            die(f"launcher ID {requested} is already in use")
        return requested
    for candidate in range(100, 0x100000000):
        if candidate not in occupied:
            return candidate
    die("no launcher IDs remain")


def cmake_text(slug: str) -> str:
    target = slug.upper()
    return f"""# SPDX-License-Identifier: MIT

set(P4_{target}_SOURCES \"src/{slug}.c\")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${{P4_{target}_SOURCES}}
        REQUIRES p4_game_api
    )
    target_compile_features(${{COMPONENT_LIB}} PUBLIC c_std_11)
    target_compile_options(${{COMPONENT_LIB}} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_{slug} VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        \"${{CMAKE_CURRENT_LIST_DIR}}/../../components/p4_game_api\"
        \"${{CMAKE_CURRENT_BINARY_DIR}}/p4_game_api\"
    )
    add_library({slug} STATIC ${{P4_{target}_SOURCES}})
    target_link_libraries({slug} PUBLIC p4_game_api)
    target_compile_features({slug} PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES \"Clang|GNU\")
        target_compile_options({slug} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
endif()
"""


def source_text(slug: str, game_id: str, title: str,
                launcher_id: int, accent: str,
                multiplayer: bool) -> str:
    symbol = f"p4_{slug}_game"
    c_title = json.dumps(title.upper())
    return f"""// SPDX-License-Identifier: MIT
// Generated starter for P4 Game API v1. Replace this with your game.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include \"p4/draw.h\"
#include \"p4/game.h\"
#include \"p4/input.h\"

typedef struct {{
    int x;
    int y;
    uint32_t held_buttons;
    uint32_t move_accumulator_ms;
}} {slug}_state_t;

static bool game_start(p4_game_context_t *context)
{{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof({slug}_state_t)) {{
        return false;
    }}
    {slug}_state_t *const state = context->state;
    *state = ({slug}_state_t){{.x = 160, .y = 80}};
    (void)p4_game_play_tone(
        context, 523U, 80U, 3U, P4_WAVE_TRIANGLE);
    return true;
}}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{{
    {slug}_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {{
        return P4_GAME_EXIT_TO_LAUNCHER;
    }}
    state->held_buttons = input->held;
    state->move_accumulator_ms += elapsed_ms;
    while (state->move_accumulator_ms >= 16U) {{
        state->move_accumulator_ms -= 16U;
        if ((input->held & P4_BUTTON_LEFT) != 0U && state->x > 8) {{
            --state->x;
        }}
        if ((input->held & P4_BUTTON_RIGHT) != 0U && state->x < 311) {{
            ++state->x;
        }}
        if ((input->held & P4_BUTTON_UP) != 0U && state->y > 22) {{
            --state->y;
        }}
        if ((input->held & P4_BUTTON_DOWN) != 0U && state->y < 139) {{
            ++state->y;
        }}
    }}
    if ((input->pressed & P4_BUTTON_A) != 0U) {{
        (void)p4_game_play_tone(
            context, 784U, 100U, 4U, P4_WAVE_SQUARE);
    }}
    return P4_GAME_CONTINUE;
}}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{{
    if (!p4_surface_valid(surface)) {{
        return false;
    }}
    const {slug}_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0000));
    p4_draw_text(surface, 8, 6, {c_title}, UINT16_C(0xffff), 1U, 15U);
    p4_draw_text(surface, 8, 16, \"MOVE + PRESS A\",
                 UINT16_C(0x9cf3), 1U, 14U);
    p4_draw_fill_circle(surface, state->x, state->y, 7,
                        UINT16_C({accent}));
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x7bef), UINT16_C({accent}),
        state->held_buttons);
    return true;
}}

static void game_stop(p4_game_context_t *context)
{{
    p4_game_stop_audio(context);
}}

const p4_game_descriptor_t {symbol} = {{
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C({launcher_id}),
    .id = \"{game_id}\",
    .title = {c_title},
    .subtitle = \"P4 GAME API V1\",
    .accent_rgb565 = UINT16_C({accent}),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE{(" |" + chr(10) + "        P4_GAME_CAP_MULTIPLAYER_SESSION") if multiplayer else ""},
    .state_bytes = sizeof({slug}_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
}};
"""


def readme_text(title: str, folder: str,
                multiplayer_style: str | None) -> str:
    multiplayer = ""
    if multiplayer_style is not None:
        multiplayer = f"""

This starter declares the `{multiplayer_style}` multiplayer profile in
`game.json`. After installation, Console OS validates the P4G and automatically
registers it in the Multiplayer selector before its first launch. There is no
game-side registration call and no central OS table to edit. Game code uses
only `p4_game_multiplayer_read_profile()`,
`p4_game_multiplayer_read_status()`, `p4_game_multiplayer_send()`, and
`p4_game_multiplayer_receive()`; it never chooses BLE, UART, or USB directly.
Keep a complete offline mode and increment `multiplayer.protocol` whenever the
meaning of your game messages changes.
"""
    return f"""# {title}

This starter is a native P4 Game API v1 component. Edit the file in `src/`,
then run `make game-sdk-host` and `make console-os-idf` from the repository
root. The build discovers `game.json` automatically and creates a `.P4G`
cartridge under `apps/console_os/build/game-storage-seed/GAMES/`. Copy that
file into the `GAMES` directory on the `P4 GAMES` USB volume and eject it; the
launcher places the game under
`{folder}` without an OS reflash.

Use only the `p4/` headers for display, controls, drawing, and sound. Keep
board drivers and raw ESP-IDF peripheral ownership in platform components.
Press the on-screen Exit control to return to the launcher.
{multiplayer}
"""


def write_files(destination: pathlib.Path,
                files: Iterable[tuple[pathlib.Path, str]]) -> None:
    destination.mkdir(parents=False)
    for relative, content in files:
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create a native P4 Game API v1 starter game")
    parser.add_argument("title", help="launcher title (15 ASCII bytes maximum)")
    parser.add_argument("--slug", help="lowercase component name")
    parser.add_argument("--launcher-id", type=int)
    parser.add_argument("--accent", default="0x5fea",
                        help="RGB565 color such as 0x5fea")
    parser.add_argument("--folder", default="GAMES/ARCADE",
                        help="one or two uppercase launcher folders")
    parser.add_argument(
        "--optional-capability",
        action="append",
        choices=sorted(CAPABILITIES),
        help=("optional Game API service; repeat as needed "
              "(default: audio-tone)"),
    )
    parser.add_argument(
        "--multiplayer",
        choices=("turn-based", "realtime", "lockstep"),
        help=("add a two-player declarative networking profile and the "
              "multiplayer-session capability"),
    )
    parser.add_argument("--games-root", type=pathlib.Path,
                        default=ROOT / "games")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not TITLE_RE.fullmatch(args.title) or len(args.title.encode("ascii")) > 15:
        die("title must be 1..15 ASCII letters, numbers, spaces, dots, _ or -")
    slug = args.slug or derived_slug(args.title)
    if not SLUG_RE.fullmatch(slug):
        die("slug must match [a-z][a-z0-9_]{2,31}")
    if not ACCENT_RE.fullmatch(args.accent):
        die("--accent must be a four-digit RGB565 value such as 0x5fea")
    if not FOLDER_RE.fullmatch(args.folder):
        die("--folder must contain one or two uppercase 1..15 byte segments")

    games_root = args.games_root.resolve()
    if not games_root.is_dir():
        die(f"games root is not a directory: {games_root}")
    destination = games_root / slug
    if destination.exists():
        die(f"destination already exists: {destination}")
    launcher_id = choose_launcher_id(
        occupied_launcher_ids(games_root), args.launcher_id)
    optional_capabilities = args.optional_capability or ["audio-tone"]
    if len(optional_capabilities) != len(set(optional_capabilities)):
        die("--optional-capability contains a duplicate")
    if args.multiplayer is not None and \
            "multiplayer-session" not in optional_capabilities:
        optional_capabilities.append("multiplayer-session")
    game_id = "org.p4console." + slug.replace("_", "-")
    manifest = {
        "schema": 1,
        "format": "p4-native-elf-v1",
        "api_version": 1,
        "version": "1.0.0",
        "package_file": f"{slug.upper()}.P4G",
        "component": slug,
        "entry_symbol": f"p4_{slug}_game",
        "launcher_id": launcher_id,
        "id": game_id,
        "title": args.title.upper(),
        "subtitle": "P4 GAME API V1",
        "folder": args.folder,
        "accent_rgb565": args.accent.lower(),
        "required_capabilities": ["video", "controls"],
        "optional_capabilities": optional_capabilities,
        "license": "MIT",
        "assets": "original-code-rendered-shapes-only",
        "enabled": True,
    }
    if args.multiplayer is not None:
        manifest["multiplayer"] = {
            "schema": 1,
            "style": args.multiplayer,
            "min_players": 2,
            "max_players": 2,
            "protocol": 1,
        }
    files = (
        (pathlib.Path("CMakeLists.txt"), cmake_text(slug)),
        (pathlib.Path("game.json"),
         json.dumps(manifest, indent=2) + "\n"),
        (pathlib.Path("src") / f"{slug}.c",
         source_text(slug, game_id, args.title, launcher_id,
                     args.accent.lower(), args.multiplayer is not None)),
        (pathlib.Path("README.md"),
         readme_text(args.title, args.folder, args.multiplayer)),
    )
    result = {
        "result": "p4-game-starter-planned" if args.dry_run
                  else "p4-game-starter-created",
        "path": str(destination),
        "component": slug,
        "game_id": game_id,
        "launcher_id": launcher_id,
        "folder": args.folder,
        "optional_capabilities": optional_capabilities,
        "multiplayer": manifest.get("multiplayer"),
        "files": [str(relative) for relative, _ in files],
    }
    if not args.dry_run:
        write_files(destination, files)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
