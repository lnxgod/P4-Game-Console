#!/usr/bin/env python3
"""Generate the guarded ESP32-P4 HS-root FS/LS timing overlay.

The managed Espressif USB 1.5.0 package remains byte-for-byte immutable. This
generator accepts only its exact ``hcd_dwc.c`` and emits a build-directory
translation unit that mirrors Espressif's experimental P4 forced-full-speed
fix: reapply HCFG.FSLSSupp and the 30 MHz UTMI clock before every root reset,
then select the post-enable UTMI clock and one-millisecond HFIR interval from
the observed speed. A platform-owned durable boot probe gates every new write,
so the next boot follows the previously stable path after an incomplete boot.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib


UPSTREAM_BYTES = 116_968
UPSTREAM_SHA256 = "de0471a749547c7d295af0fe2e3e5b61d1eedf46d88c5b57cf20cec202d6c749"


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one upstream match, found {count}")
    return source.replace(old, new, 1)


def generate(source: str) -> str:
    source = replace_once(
        source,
        '#include "esp_idf_version.h"\n',
        '#include "esp_idf_version.h"\n'
        '#include "soc/usb_dwc_struct.h"\n',
        "P4 DWC register include",
    )
    source = replace_once(
        source,
        "#define SUSPEND_ENTRY_MS                        "
        "CONFIG_USB_HOST_SUSPEND_ENTRY_MS\n",
        "#define SUSPEND_ENTRY_MS                        "
        "CONFIG_USB_HOST_SUSPEND_ENTRY_MS\n"
        "#if !defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) || "
        "!CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3\n"
        "#error P4 HS FS/LS overlay is restricted to the Waveshare 4.3 target\n"
        "#endif\n"
        "#if !defined(CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE) || "
        "!CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE\n"
        "#error P4 HS FS/LS overlay requires controller-first H2 host mode\n"
        "#endif\n"
        "#if !defined(CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST) || "
        "!CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST\n"
        "#error P4 HS FS/LS overlay requires the forced-full-speed policy\n"
        "#endif\n",
        "overlay build gates",
    )
    helpers = r'''extern bool p4_usb_hs_fsls_reapply_allowed(void) __attribute__((weak));

static bool p4_hs_fsls_reapply_enabled(const usb_dwc_hal_context_t *hal)
{
    return hal != NULL &&
           hal->dev == &USB_DWC_HS &&
           hal->constant_config.hsphy_type != 0 &&
           p4_usb_hs_fsls_reapply_allowed != NULL &&
           p4_usb_hs_fsls_reapply_allowed();
}

static void p4_hs_fsls_prepare_reset(usb_dwc_hal_context_t *hal)
{
    if (!p4_hs_fsls_reapply_enabled(hal)) {
        return;
    }
    // Espressif 6306c4f: the controller can clear FSLSSupp, so restore both
    // fields before every reset. UTMI 16-bit mode uses a 30 MHz PHY clock.
    hal->dev->hcfg_reg.fslssupp = 1U;
    hal->dev->hcfg_reg.fslspclksel = 0U;
}

static void p4_hs_fsls_finish_enable(usb_dwc_hal_context_t *hal)
{
    if (!p4_hs_fsls_reapply_enabled(hal)) {
        return;
    }
    const usb_dwc_speed_t speed =
        (usb_dwc_speed_t)hal->dev->hprt_reg.prtspd;
    hal->dev->hcfg_reg.fslspclksel =
        speed == USB_DWC_SPEED_LOW ? 2U : 0U;

    usb_dwc_hfir_reg_t hfir;
    hfir.val = hal->dev->hfir_reg.val;
    hfir.hfirrldctrl = 0U;
    hfir.frint = speed == USB_DWC_SPEED_LOW ? 5999U : 29999U;
    hal->dev->hfir_reg.val = hfir.val;
}

static usb_dwc_speed_t p4_hs_fsls_effective_speed(
    const usb_dwc_hal_context_t *hal, usb_dwc_speed_t speed)
{
    if (p4_hs_fsls_reapply_enabled(hal) &&
        speed == USB_DWC_SPEED_HIGH) {
        return USB_DWC_SPEED_FULL;
    }
    return speed;
}

static void p4_hs_fsls_log_root_ready(
    const usb_dwc_hal_context_t *hal)
{
    if (!p4_hs_fsls_reapply_enabled(hal)) {
        return;
    }
    const usb_dwc_speed_t raw_speed =
        (usb_dwc_speed_t)hal->dev->hprt_reg.prtspd;
    const usb_dwc_speed_t effective_speed =
        p4_hs_fsls_effective_speed(hal, raw_speed);
    ESP_LOGI(HCD_DWC_TAG,
             "P4_HS_FSLS_ROOT_READY reset=reapplied fslssupp=%u "
             "pclk_sel=%u frame_interval=%u raw_speed=%u effective_speed=%u",
             (unsigned)hal->dev->hcfg_reg.fslssupp,
             (unsigned)hal->dev->hcfg_reg.fslspclksel,
             (unsigned)hal->dev->hfir_reg.frint,
             (unsigned)raw_speed,
             (unsigned)effective_speed);
}

'''
    source = replace_once(
        source,
        "static usb_speed_t get_usb_port_speed(usb_dwc_speed_t priv)\n",
        helpers + "static usb_speed_t get_usb_port_speed(usb_dwc_speed_t priv)\n",
        "guarded FS/LS helpers",
    )
    source = replace_once(
        source,
        "        usb_dwc_hal_port_enable(port->hal);  // Initialize remaining host port registers\n"
        "        port->speed = get_usb_port_speed(usb_dwc_hal_port_get_conn_speed(port->hal));\n",
        "        usb_dwc_hal_port_enable(port->hal);  // Initialize remaining host port registers\n"
        "        p4_hs_fsls_finish_enable(port->hal);\n"
        "        usb_dwc_speed_t conn_speed =\n"
        "            usb_dwc_hal_port_get_conn_speed(port->hal);\n"
        "        conn_speed = p4_hs_fsls_effective_speed(port->hal, conn_speed);\n"
        "        port->speed = get_usb_port_speed(conn_speed);\n",
        "post-enable timing and speed",
    )
    source = replace_once(
        source,
        "    // Place the bus into the reset state. If the port was previously enabled, a disabled event will occur after this\n"
        "    usb_dwc_hal_port_toggle_reset(port->hal, true);\n",
        "    // Place the bus into the reset state. If the port was previously enabled, a disabled event will occur after this\n"
        "    p4_hs_fsls_prepare_reset(port->hal);\n"
        "    usb_dwc_hal_port_toggle_reset(port->hal, true);\n",
        "pre-reset reapplication",
    )
    source = replace_once(
        source,
        "    HCD_EXIT_CRITICAL();\n"
        "    xSemaphoreGive(port->port_mux);\n"
        "    return ret;\n"
        "}\n\n"
        "hcd_port_state_t hcd_port_get_state",
        "    HCD_EXIT_CRITICAL();\n"
        "    if (command == HCD_PORT_CMD_RESET && ret == ESP_OK) {\n"
        "        p4_hs_fsls_log_root_ready(port->hal);\n"
        "    }\n"
        "    xSemaphoreGive(port->port_mux);\n"
        "    return ret;\n"
        "}\n\n"
        "hcd_port_state_t hcd_port_get_state",
        "post-critical reset evidence",
    )
    source = replace_once(
        source,
        "    *speed = get_usb_port_speed(usb_dwc_hal_port_get_conn_speed(port->hal));\n",
        "    usb_dwc_speed_t conn_speed =\n"
        "        usb_dwc_hal_port_get_conn_speed(port->hal);\n"
        "    conn_speed = p4_hs_fsls_effective_speed(port->hal, conn_speed);\n"
        "    *speed = get_usb_port_speed(conn_speed);\n",
        "public effective speed",
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
            "refusing USB HCD overlay: hcd_dwc.c is not the pinned 1.5.0 source"
        )
    generated = generate(payload.decode("utf-8")).encode("utf-8")

    if args.check_output:
        if not args.output.is_file() or args.output.read_bytes() != generated:
            raise SystemExit("generated USB HCD overlay differs or is missing")
        return

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.is_file() or args.output.read_bytes() != generated:
        args.output.write_bytes(generated)


if __name__ == "__main__":
    main()
