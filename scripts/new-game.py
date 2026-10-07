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
CAPABILITY_CONSTANTS = {
    "audio-tone": "P4_GAME_CAP_AUDIO_TONE",
    "audio-stream": "P4_GAME_CAP_AUDIO_STREAM",
    "storage": "P4_GAME_CAP_STORAGE",
    "signal-scan": "P4_GAME_CAP_SIGNAL_SCAN",
    "save": "P4_GAME_CAP_SAVE",
    "text-input": "P4_GAME_CAP_TEXT_INPUT",
    "realm": "P4_GAME_CAP_REALM",
    "multiplayer-session": "P4_GAME_CAP_MULTIPLAYER_SESSION",
    "module-handoff": "P4_GAME_CAP_MODULE_HANDOFF",
    "vector-scenes": "P4_GAME_CAP_VECTOR_SCENES",
    "video-highres": "P4_GAME_CAP_VIDEO_HIGH_RES",
}
CAPABILITIES = set(CAPABILITY_CONSTANTS)


def die(message: str) -> "NoReturn":
    raise SystemExit(f"new-game: {message}")


def derived_slug(title: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", title.lower()).strip("_")
    if slug and slug[0].isdigit():
        slug = "game_" + slug
    return slug


def occupied_launcher_ids(games_root: pathlib.Path) -> set[int]:
    occupied: set[int] = set()
    retired = games_root / "retired.json"
    if retired.is_file():
        for game in json.loads(retired.read_text())["games"]:
            occupied.add(game["launcher_id"])
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
                optional_capabilities: list[str], high_res: bool) -> str:
    symbol = f"p4_{slug}_game"
    c_title = json.dumps(title)
    required_expression = "P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS"
    if high_res:
        required_expression += " |\n        P4_GAME_CAP_VIDEO_HIGH_RES"
    optional_expression = " |\n        ".join(
        CAPABILITY_CONSTANTS[name] for name in optional_capabilities
    ) or "UINT32_C(0)"
    return f"""// SPDX-License-Identifier: MIT
// Generated starter for P4 Game API v1. Replace this with your game.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include \"p4/draw.h\"
#include \"p4/game.h\"
#include \"p4/input.h\"
#include \"p4/presentation.h\"

typedef struct {{
    int32_t x_q8;
    int32_t y_q8;
    uint32_t held_buttons;
}} {slug}_state_t;

static int surface_x(const p4_game_surface_t *surface, int x)
{{
    return x * (int)surface->width / P4_GAME_SURFACE_WIDTH;
}}

static int surface_y(const p4_game_surface_t *surface, int y)
{{
    return y * (int)surface->height / P4_GAME_SURFACE_HEIGHT;
}}

/* Preserve subpixel position until the final native-resolution raster step. */
static int surface_x_q8(const p4_game_surface_t *surface, int32_t x_q8)
{{
    return (int)(x_q8 * (int32_t)surface->width /
                 (P4_GAME_SURFACE_WIDTH * 256));
}}

static int surface_y_q8(const p4_game_surface_t *surface, int32_t y_q8)
{{
    return (int)(y_q8 * (int32_t)surface->height /
                 (P4_GAME_SURFACE_HEIGHT * 256));
}}

static bool game_start(p4_game_context_t *context)
{{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof({slug}_state_t)) {{
        return false;
    }}
    {slug}_state_t *const state = context->state;
    *state = ({slug}_state_t){{.x_q8 = 160 * 256, .y_q8 = 80 * 256}};
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
    /* 62.5 logical pixels/second. A long stall cannot cause an unbounded
     * catch-up loop or teleport; collision-heavy games can use bounded fixed
     * simulation steps and interpolate their rendered poses instead. */
    const uint32_t dt_ms = elapsed_ms > 50U ? 50U : elapsed_ms;
    const int32_t distance_q8 = (int32_t)dt_ms * 16;
    const int dx = ((input->held & P4_BUTTON_RIGHT) != 0U ? 1 : 0) -
                   ((input->held & P4_BUTTON_LEFT) != 0U ? 1 : 0);
    const int dy = ((input->held & P4_BUTTON_DOWN) != 0U ? 1 : 0) -
                   ((input->held & P4_BUTTON_UP) != 0U ? 1 : 0);
    state->x_q8 += dx * distance_q8;
    state->y_q8 += dy * distance_q8;
    if (state->x_q8 < 8 * 256) state->x_q8 = 8 * 256;
    if (state->x_q8 > 311 * 256) state->x_q8 = 311 * 256;
    if (state->y_q8 < 22 * 256) state->y_q8 = 22 * 256;
    if (state->y_q8 > 139 * 256) state->y_q8 = 139 * 256;
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
    const unsigned text_height =
        surface->width == P4_GAME_SURFACE_HIGH_RES_WIDTH ? 24U : 10U;
    p4_draw_clear(surface, UINT16_C(0x0000));
    p4_ui_text(surface, surface_x(surface, 58), surface_y(surface, 5),
                 {c_title}, UINT16_C(0xffff), text_height, 15U);
    p4_ui_text(surface, surface_x(surface, 8), surface_y(surface, 28),
                 \"MOVE + PRESS A\", UINT16_C(0x9cf3), text_height, 14U);
    p4_draw_fill_circle(surface, surface_x_q8(surface, state->x_q8),
                        surface_y_q8(surface, state->y_q8),
                        surface_x(surface, 7),
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
    .subtitle = \"WORK IN PROGRESS\",
    .accent_rgb565 = UINT16_C({accent}),
    .required_capabilities = {required_expression},
    .optional_capabilities = {optional_expression},
    .state_bytes = sizeof({slug}_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
}};
"""


def readme_text(title: str, folder: str,
                multiplayer_style: str | None,
                high_res: bool, slug: str) -> str:
    multiplayer = ""
    if multiplayer_style is not None:
        multiplayer = f"""

## Multiplayer

The `{multiplayer_style}` profile and optional `multiplayer-session`
capability are declared in `game.json`. This scaffold adds metadata only:
the generated game does not yet synchronize players or state.

Use the [multiplayer skill](../../.agents/skills/esp32-multiplayer/SKILL.md)
to implement the game's bounded protocol. Checkers provides a small turn-based
example; Air Hockey provides real-time host-authority and two-instance tests.
Console OS owns registration, Host/Join, room discovery, exact-game matching,
transport and the start barrier. There is no game-side registration call or
central table to edit.

Read the already-connected session through `p4_game_multiplayer_read_profile()`
and `p4_game_multiplayer_read_status()`; exchange only bounded game messages
with `p4_game_multiplayer_send()` and `p4_game_multiplayer_receive()`.
Keep a complete offline mode and increment `multiplayer.protocol` when message
meaning changes. Set `message_bytes` to the largest actual packet, at most 64.
Tab5 Local Wi-Fi supports up to four consoles where the game profile does;
Bluetooth and USB serial remain limited to two. This scaffold does not prove
physical multiplayer acceptance.
"""
    resolution = """

This is an explicit legacy 320x200 scaffold for requested legacy maintenance.
Maintained Tab5 games must use the default native 768x480 RGB565 scaffold.
Legacy compatibility does not qualify a maintained Tab5 release.
"""
    if high_res:
        resolution = """

This starter follows the [presentation standard](../../docs/GAME_ART.md)
and requires `video-highres` in both the manifest and C descriptor. Maintained
Tab5 games render directly into a native 768x480 RGB565 surface; startup fails
cleanly if the required capability is unavailable. Never lower the framebuffer
resolution or upscale a completed low-resolution frame to fix performance or
readability. Canonical 320x200 touch coordinates are input units, not render
resolution; scale drawing using `surface->width` and `surface->height`.

Before device acceptance, verify the actual runtime surface is 768x480 and
inspect active play, opening, pause and results on the exact package/OS/unit.
A capability declaration or a large screenshot does not prove native rendering.
"""
    return f"""# {title}

[Game index](../README.md) · [Console OS](../../apps/console_os/README.md)

This is a minimal native P4 Game API v1 scaffold, ready for your own rules,
art and gameplay. The [starter guide](../../docs/GAME_STARTERS.md) offers
optional examples; you do not need to keep this moving-circle demo or use a
particular template. It starts unpublished (`enabled: false`) so the demo does
not enter the product catalog. Edit `src/{slug}.c`, and list additional C files
in `game.json`'s `sources` when splitting the implementation. Keep identities
in `games/retired.json` reserved.

Native C is the supported authoring route. Custom software 3D, raycasting,
physics and other engines may draw directly into the negotiated RGB565 surface;
the supplied 2D drawing helpers are optional. Extend shared platform APIs when
an engine needs a missing service. Lua source cartridges are retired.

Use only public `p4/` APIs. Console OS owns display, audio, USB/BLE controllers,
storage, timing and launcher lifecycle. The starter consumes normalized
D-pad movement and A; map the completed game's menus/actions to A/B/Start/Back
and touch as appropriate. Back returns to the launcher. No per-game HID driver,
USB flag or pairing screen is needed. Hardware support still depends on the
selected board's OS.

## Build and play locally

From the repository root:

```sh
cmake -S tools/p4-game-host -B build-host/play-{slug} -G Ninja -DP4_GAME={slug} -DP4_ALLOW_DRAFT_GAME=ON
cmake --build build-host/play-{slug}
ctest --test-dir build-host/play-{slug} --output-on-failure
make play-game GAME={slug}
```

Add focused rule/protocol tests as the game grows. Run `make game-registry-check`
after manifest changes. Follow the
[local testing skill](../../.agents/skills/esp32-test-game/SKILL.md) for
sanitizer smoke and interactive controls/lifecycle checks.

## ESP32-P4 performance

Follow the [game performance contract](../../docs/GAME_PERFORMANCE.md) from
the first playable build. Target 60 FPS with an actual-device 30 FPS release
floor. This starter retains fractional positions, scales them only at raster
time and caps delayed updates at 50 ms. Keep rendering in bounded primitives,
row spans or a budgeted custom renderer; retain fractional
motion and animation time, and budget state, art and per-tick work. Review costly
pixel loops with the pinned RV32 compiler before adding more work.

Benchmark active motion/dragging at the selected render resolution, then record
exact package, OS and device cadence for release qualification. Host CPU timing
and successful installation do not prove tablet FPS; leave device acceptance
pending until measured.

## Package for the selected console

After the finished game passes its focused tests and local play checks, set
`enabled` to `true` in `game.json` and validate it again. The build discovers
the enabled manifest and puts the game in `{folder}`. Use the
[package skill](../../.agents/skills/esp32-add-game/SKILL.md) and the matching
target: `make console-os-tab5-idf`, `make console-os-waveshare-idf`,
`make console-os-olimex-idf` or `make console-os-elecrow-idf` for Elecrow.
Install the resulting `.P4G` and any required resource sidecar through that
board's documented content path. A compatible game-only update does not need
an OS reflash. Use [ESP32 - Set Up](../../.agents/skills/esp32-setup/SKILL.md) only when
setting up the OS itself.
{resolution}{multiplayer}
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
        help=("add two-player networking metadata (not synchronization) and the "
              "multiplayer-session capability"),
    )
    resolution = parser.add_mutually_exclusive_group()
    resolution.add_argument(
        "--high-res", dest="high_res", action="store_true", default=True,
        help="require native 768x480 RGB565 (default for maintained Tab5)",
    )
    resolution.add_argument(
        "--low-res", dest="high_res", action="store_false",
        help="explicit legacy exception: request only the 320x200 surface",
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
    required_capabilities = ["video", "controls"]
    if args.high_res:
        required_capabilities.append("video-highres")
        optional_capabilities = [capability for capability in optional_capabilities
                                 if capability != "video-highres"]
    game_id = "org.p4console." + slug.replace("_", "-")
    retired_path = games_root / "retired.json"
    if retired_path.exists():
        retired = json.loads(retired_path.read_text(encoding="utf-8"))
        for entry in retired["games"]:
            if entry["id"] == game_id or entry["package_file"] == f"{slug.upper()}.P4G":
                die("game identity or package filename is reserved by a retired game")
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
        "title": args.title,
        "subtitle": "WORK IN PROGRESS",
        "folder": args.folder,
        "accent_rgb565": args.accent.lower(),
        "required_capabilities": required_capabilities,
        "optional_capabilities": optional_capabilities,
        "license": "MIT",
        "assets": "original-code-rendered-shapes-and-pinned-Arimo-OFL-1.1-font",
        "enabled": False,
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
                     args.accent.lower(), optional_capabilities, args.high_res)),
        (pathlib.Path("README.md"),
         readme_text(args.title, args.folder, args.multiplayer,
                     args.high_res, slug)),
    )
    result = {
        "result": "p4-game-starter-planned" if args.dry_run
                  else "p4-game-starter-created",
        "path": str(destination),
        "component": slug,
        "game_id": game_id,
        "launcher_id": launcher_id,
        "folder": args.folder,
        "required_capabilities": required_capabilities,
        "optional_capabilities": optional_capabilities,
        "high_resolution": args.high_res,
        "multiplayer": manifest.get("multiplayer"),
        "files": [str(relative) for relative, _ in files],
    }
    if not args.dry_run:
        write_files(destination, files)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
