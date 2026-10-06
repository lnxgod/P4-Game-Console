#!/usr/bin/env python3
"""Generate the Waveshare BLE ESP-Hosted task-stack overlay.

ESP-Hosted 1.4.7 creates six 5 KiB worker stacks before its SDIO receive task
allocates the mandatory 512-byte DMA alignment buffer.  Console OS starts the
radio late, when that ordering can exhaust internal DMA RAM.  Keep the pinned
managed component immutable and move only those non-ISR worker stacks to PSRAM
through ESP-IDF's supported WithCaps task API.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib


UPSTREAM_BYTES = 21_250
UPSTREAM_SHA256 = "0fcb795dbb1a09ab99f38d2bd266fe8f0f99d48f7a9628480c45465fa07f78ea"


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one upstream match, found {count}")
    return source.replace(old, new, 1)


def generate(source: str) -> str:
    source = replace_once(
        source,
        '#include "freertos/portmacro.h"\n',
        '#include "freertos/portmacro.h"\n'
        '#include "freertos/idf_additions.h"\n'
        '#include "esp_heap_caps.h"\n'
        '#if !CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && !CONFIG_P4_BOARD_M5STACK_TAB5\n'
        '#error ESP-Hosted PSRAM task overlay is restricted to Waveshare 4.3 or Tab5\n'
        '#endif\n'
        '#if !CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY\n'
        '#error ESP-Hosted PSRAM task overlay requires external stack support\n'
        '#endif\n',
        "ESP-IDF WithCaps includes and board gate",
    )
    source = replace_once(
        source,
        "\ttask_created = xTaskCreate((void (*)(void *))start_routine, tname, tstack_size, sr_arg, tprio, thread_handle);\n",
        "\ttask_created = xTaskCreateWithCaps(\n"
        "\t\t\t(void (*)(void *))start_routine, tname, tstack_size,\n"
        "\t\t\tsr_arg, tprio, thread_handle,\n"
        "\t\t\tMALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n",
        "Hosted worker task allocation",
    )
    source = replace_once(
        source,
        "\tvTaskDelete(*thread_hdl);\n",
        "\tvTaskDeleteWithCaps(*thread_hdl);\n",
        "Hosted worker task deletion",
    )
    return source


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--check-output", action="store_true")
    args = parser.parse_args()

    payload = args.input.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    if len(payload) != UPSTREAM_BYTES or digest != UPSTREAM_SHA256:
        raise SystemExit(
            "refusing ESP-Hosted overlay: os_wrapper.c is not pinned 1.4.7"
        )
    generated = generate(payload.decode("utf-8")).encode("utf-8")

    if args.check_output:
        if not args.output.is_file() or args.output.read_bytes() != generated:
            raise SystemExit("generated ESP-Hosted overlay differs or is missing")
        return

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.is_file() or args.output.read_bytes() != generated:
        args.output.write_bytes(generated)


if __name__ == "__main__":
    main()
