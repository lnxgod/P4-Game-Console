#!/usr/bin/env python3

"""Check board-specific touch constants and the borrowed-bus GT911 seam."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/platform_touch/src/platform_touch.c"
HEADER = ROOT / "components/platform_touch/include/platform/touch.h"


def fail(message: str) -> None:
    raise SystemExit(f"platform-touch verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)



# Compile the public header for each explicit board selection. Text matching a
# literal in board.h would also match an inactive, wrong-board branch.
BOARD_CONTRACTS = {
    "elecrow": ("CONFIG_P4_BOARD_ELECROW_CROWPANEL_ADVANCED_10",
                (40, 42, 1024, 600, 1024, 600, 0, 45, 46)),
    "waveshare": ("CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3",
                  (23, -1, 800, 480, 480, 800, 90, 7, 8)),
    "olimex": ("CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B",
               (-1, -1, 1280, 720, 1280, 720, 0, 7, 8)),
    "tab5": ("CONFIG_P4_BOARD_M5STACK_TAB5",
             (-1, 23, 1280, 720, 720, 1280, 90, 31, 32)),
}
BOARD_CONSTANTS = (
    "PLATFORM_TOUCH_RESET_GPIO", "PLATFORM_TOUCH_INTERRUPT_GPIO",
    "PLATFORM_TOUCH_WIDTH", "PLATFORM_TOUCH_HEIGHT",
    "PLATFORM_TOUCH_NATIVE_WIDTH", "PLATFORM_TOUCH_NATIVE_HEIGHT",
    "PLATFORM_TOUCH_ROTATION_CW_DEGREES", "PLATFORM_BOARD_I2C_SDA_GPIO",
    "PLATFORM_BOARD_I2C_SCL_GPIO",
)


def verify_board_headers(root: pathlib.Path = ROOT, compiler: str = "cc") -> None:
    # Unconfigured host callers retain the old Elecrow fallback. Firmware uses
    # its explicit board selection; this does not change the default Tab5 target.
    selections = {**BOARD_CONTRACTS,
                  "legacy-host-default": (None, BOARD_CONTRACTS["elecrow"][1])}
    for board, (selected, values) in selections.items():
        lines = ['#include "platform/touch.h"']
        for name, value in zip(BOARD_CONSTANTS, values):
            lines.append(f'_Static_assert({name} == {value}, "{board}: {name}");')
        lines.append('_Static_assert(PLATFORM_BOARD_I2C_PORT == 1, "borrowed I2C1");')
        defines = [f"-D{macro}={int(macro == selected)}"
                   for macro, _ in BOARD_CONTRACTS.values()]
        command = [compiler, "-std=c11", "-fsyntax-only", "-x", "c", *defines]
        for relative in ("components/platform_touch/include",
                         "components/platform_touch/host/include",
                         "components/platform_board/include"):
            command.extend(["-I", str(root / relative)])
        result = subprocess.run(command + ["-"], input="\n".join(lines) + "\n",
                                capture_output=True, text=True)
        require(result.returncode == 0,
                f"{board} public touch contract changed:\n{result.stderr.strip()}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=pathlib.Path,
                        help="optional Elecrow/Waveshare GT911 wrapper library; not the Tab5 adapter")
    parser.add_argument("--cc", default="cc", help="host C compiler for public-header checks")
    args = parser.parse_args()

    source = SOURCE.read_text()
    header = HEADER.read_text()
    forbidden_source = (
        "i2c_new_master_bus",
        "i2c_del_master_bus",
        "i2c_master_bus_add_device",
        "gpio_config(",
        "gpio_set_level(",
        "gpio_reset_pin(",
        "gpio_install_isr_service(",
        "gpio_isr_handler_add(",
    )
    for token in forbidden_source:
        require(token not in source, f"wrapper directly owns forbidden API {token}")

    required_source = (
        ".scl_speed_hz = PLATFORM_TOUCH_I2C_CLOCK_HZ",
        ".rst_gpio_num = (gpio_num_t)PLATFORM_TOUCH_RESET_GPIO",
        ".int_gpio_num = (gpio_num_t)PLATFORM_TOUCH_INTERRUPT_GPIO",
        ".interrupt_callback = NULL",
        "PLATFORM_TOUCH_GT911_BACKUP_ADDRESS",
        "esp_lcd_panel_io_del(touch->io)",
    )
    for token in required_source:
        require(token in source, f"reviewed runtime contract missing {token}")

    required_header = {
        "PLATFORM_TOUCH_I2C_CLOCK_HZ": "400000U",
        "PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS": "0x5dU",
        "PLATFORM_TOUCH_GT911_BACKUP_ADDRESS": "0x14U",
        "PLATFORM_TOUCH_RESET_GPIO": "PLATFORM_BOARD_TOUCH_RESET_GPIO",
        "PLATFORM_TOUCH_INTERRUPT_GPIO": "PLATFORM_BOARD_TOUCH_INTERRUPT_GPIO",
        "PLATFORM_TOUCH_MAX_CONTACTS": "5U",
        "PLATFORM_TOUCH_WIDTH": "PLATFORM_BOARD_DISPLAY_WIDTH",
        "PLATFORM_TOUCH_HEIGHT": "PLATFORM_BOARD_DISPLAY_HEIGHT",
        "PLATFORM_TOUCH_NATIVE_WIDTH": "PLATFORM_BOARD_DISPLAY_NATIVE_WIDTH",
        "PLATFORM_TOUCH_NATIVE_HEIGHT": "PLATFORM_BOARD_DISPLAY_NATIVE_HEIGHT",
    }
    for name, value in required_header.items():
        require(
            re.search(rf"^#define\s+{name}\s+{re.escape(value)}$", header,
                      re.MULTILINE) is not None,
            f"public constant {name} changed",
        )

    verify_board_headers(compiler=args.cc)

    if args.library is not None:
        library = args.library.resolve()
        require(library.is_file(), f"missing library: {library}")
        symbols = subprocess.run(
            ["nm", "-u", str(library)], check=True, capture_output=True,
            text=True,
        ).stdout
        for forbidden_symbol in (
            "i2c_new_master_bus",
            "i2c_del_master_bus",
            "gpio_config",
            "gpio_set_level",
            "gpio_reset_pin",
            "gpio_isr_handler_add",
        ):
            require(forbidden_symbol not in symbols,
                    f"wrapper links direct symbol {forbidden_symbol}")
        for required_symbol in (
            "esp_lcd_new_panel_io_i2c_v2",
            "esp_lcd_touch_new_i2c_gt911",
            "esp_lcd_touch_read_data",
            "esp_lcd_touch_get_coordinates",
        ):
            require(required_symbol in symbols,
                    f"wrapper seam missing {required_symbol}")

    print("P4_TOUCH_COMPONENT VERIFY PASS gt911_wrapper_bus=borrowed "
          "gt911_wrapper_gpio=via-pinned-driver addresses=0x5d,0x14 "
          "hz=400000 no_isr=true contacts=5 "
          "public_board_contracts=elecrow,waveshare,olimex,tab5,legacy-host-default "
          "olimex_touch=absent hardware_qualification=false")


if __name__ == "__main__":
    main()
