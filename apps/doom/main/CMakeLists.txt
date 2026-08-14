# SPDX-License-Identifier: GPL-2.0-or-later

idf_component_register(
    SRCS "doom_espidf_build_probe.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        doom_engine
        esp_timer
        freertos
        log
)
