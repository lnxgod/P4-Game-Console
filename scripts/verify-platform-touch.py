#!/usr/bin/env python3

"""Fail closed if platform_touch takes ownership outside its reviewed seam."""

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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=pathlib.Path)
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
        "PLATFORM_TOUCH_RESET_GPIO": "40",
        "PLATFORM_TOUCH_INTERRUPT_GPIO": "42",
        "PLATFORM_TOUCH_MAX_CONTACTS": "5U",
        "PLATFORM_TOUCH_WIDTH": "1024U",
        "PLATFORM_TOUCH_HEIGHT": "600U",
    }
    for name, value in required_header.items():
        require(
            re.search(rf"^#define\s+{name}\s+{re.escape(value)}$", header,
                      re.MULTILINE) is not None,
            f"public constant {name} changed",
        )

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

    print("P4_TOUCH_COMPONENT VERIFY PASS bus=borrowed gpio=via-pinned-driver "
          "addresses=0x5d,0x14 hz=400000 no_isr=true contacts=5")


if __name__ == "__main__":
    main()
