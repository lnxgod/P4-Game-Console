#!/usr/bin/env python3
"""Generate the fail-safe P4 external-hub enumeration retry overlay.

The Espressif USB 1.5.0 package remains immutable and is still checked against
its Component Manager lock.  This generator accepts only the exact pinned
``ext_port.c`` and emits a build-directory source with one bounded recovery:
after enumeration cancellation disables a still-connected downstream port,
wait for both the failed USBH device and the parent hub's control/status chain,
then reset that port and retry.  The generated source never changes the hub
scheduler and a platform-owned boot guard can suppress the recovery entirely.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib


UPSTREAM_BYTES = 49_731
UPSTREAM_SHA256 = "0760b3c8ef14813db621b66c19d27caea5391793c592ca480b6ce18121797736"


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one upstream match, found {count}")
    return source.replace(old, new, 1)


def generate(source: str) -> str:
    source = replace_once(
        source,
        "#define EXT_PORT_POWER_ON_CUSTOM_DELAY_MS      "
        "CONFIG_USB_HOST_EXT_PORT_CUSTOM_POWER_ON_DELAY_MS\n",
        "#define EXT_PORT_POWER_ON_CUSTOM_DELAY_MS      "
        "CONFIG_USB_HOST_EXT_PORT_CUSTOM_POWER_ON_DELAY_MS\n"
        "#if !defined(CONFIG_P4_USB_HOST_EXT_PORT_ENUM_RETRY_ATTEMPTS)\n"
        "#error P4 external-port enumeration retry policy is missing\n"
        "#endif\n"
        "#if CONFIG_P4_USB_HOST_EXT_PORT_ENUM_RETRY_ATTEMPTS < 0 || "
        "CONFIG_P4_USB_HOST_EXT_PORT_ENUM_RETRY_ATTEMPTS > 2\n"
        "#error P4 external-port enumeration retries must stay in range 0..2\n"
        "#endif\n"
        "#define EXT_PORT_ENUM_RETRY_ATTEMPTS            "
        "CONFIG_P4_USB_HOST_EXT_PORT_ENUM_RETRY_ATTEMPTS\n"
        "extern bool p4_usb_ext_port_enum_retry_allowed(void) "
        "__attribute__((weak));\n"
        "static bool ext_port_enum_retry_allowed(void)\n"
        "{\n"
        "    return EXT_PORT_ENUM_RETRY_ATTEMPTS > 0 &&\n"
        "           p4_usb_ext_port_enum_retry_allowed != NULL &&\n"
        "           p4_usb_ext_port_enum_retry_allowed();\n"
        "}\n",
        "retry policy macro",
    )
    source = replace_once(
        source,
        "            uint32_t waiting_free: 1;       /**< Port is waiting to be freed */\n"
        "            uint32_t reserved25: 25;        /**< Reserved */\n",
        "            uint32_t waiting_free: 1;       /**< Port is waiting to be freed */\n"
        "            uint32_t enum_retry_pending: 1; /**< Retry after the failed USBH node is free */\n"
        "            uint32_t reserved24: 24;        /**< Reserved */\n",
        "retry pending flag",
    )
    source = replace_once(
        source,
        "    uint8_t dev_reset_attempts;             /**< Ports' device reset failure */\n",
        "    uint8_t dev_reset_attempts;             /**< Ports' device reset failure */\n"
        "    uint8_t enum_retry_attempts;            /**< Bounded retries after enumeration failure */\n",
        "retry attempt field",
    )
    source = replace_once(
        source,
        "                // New device connected, flush reset attempts\n"
        "                ext_port->dev_reset_attempts = 0;\n"
        "                ext_port->state = USB_PORT_STATE_RESETTING;\n",
        "                // A physical connection starts a fresh bounded retry budget.\n"
        "                ext_port->dev_reset_attempts = 0;\n"
        "                ext_port->enum_retry_attempts = 0;\n"
        "                ext_port->flags.enum_retry_pending = 0;\n"
        "                ext_port->state = USB_PORT_STATE_RESETTING;\n",
        "physical connection reset",
    )
    source = replace_once(
        source,
        "    case USB_PORT_STATE_DISABLED:\n"
        "        // We don't need to do anything, as port will be handled after completing USB_FEATURE_PORT_ENABLE\n"
        "        if (ext_port->flags.is_gone) {\n"
        "            handle_complete(ext_port);\n"
        "        }\n"
        "        break;\n",
        "    case USB_PORT_STATE_DISABLED:\n"
        "        // Enumeration failure and ClearFeature(PORT_ENABLE) run in parallel.\n"
        "        // Do not issue the retry from DEV_FREE while that shared hub EP0 URB or\n"
        "        // its required GetStatus chain can still be active. The ordinary feature\n"
        "        // completion callback will request port processing once status is current.\n"
        "        if (ext_port->flags.is_gone) {\n"
        "            handle_complete(ext_port);\n"
        "        } else if (ext_port->flags.enum_retry_pending &&\n"
        "                   !ext_port->flags.status_lock &&\n"
        "                   !ext_port->flags.status_outdated) {\n"
        "            port_set_actions(ext_port, PORT_ACTION_HANDLE);\n"
        "        }\n"
        "        break;\n",
        "recycle continuation",
    )
    source = replace_once(
        source,
        "    case USB_PORT_STATE_DISABLED:\n"
        "        if (port_has_connection(ext_port)) {\n"
        "            // This logic does depend on the moment, when we propagate the EXT_PORT_DISCONNECTED event\n"
        "            // during the port disable.\n"
        "            if (!ext_port->flags.has_enum_device && ext_port->flags.waiting_recycle) {\n"
        "                // Port was disabled before enumeration, so the USBH device object was not created.\n"
        "                // Clean the recycle flag and complete port handling with device attached.\n"
        "                ext_port->flags.waiting_recycle = 0;\n"
        "            }\n"
        "        }\n"
        "        break;\n",
        "    case USB_PORT_STATE_DISABLED:\n"
        "        if (port_has_connection(ext_port)) {\n"
        "            if (ext_port->flags.enum_retry_pending) {\n"
        "                // Retry only after DEV_FREE and the parent hub's complete EP0/status\n"
        "                // chain. This preserves the upstream one-control-URB invariant.\n"
        "                if (!ext_port->flags.waiting_recycle &&\n"
        "                    !ext_port->flags.status_lock &&\n"
        "                    !ext_port->flags.status_outdated) {\n"
        "                    ext_port->flags.enum_retry_pending = 0;\n"
        "                    ext_port->enum_retry_attempts++;\n"
        "                    ext_port->dev_reset_attempts = 0;\n"
        "                    new_state = USB_PORT_STATE_DISCONNECTED;\n"
        "                    need_handling = true;\n"
        "                    ESP_LOGW(EXT_PORT_TAG,\n"
        "                             \"P4_EXT_PORT_ENUM_RETRY_SERIALIZED Port%d attempt=%d/%d\",\n"
        "                             ext_port->constant.port_num,\n"
        "                             ext_port->enum_retry_attempts,\n"
        "                             EXT_PORT_ENUM_RETRY_ATTEMPTS);\n"
        "                    port_set_actions(ext_port, PORT_ACTION_HANDLE);\n"
        "                }\n"
        "            } else if (!ext_port->flags.has_enum_device &&\n"
        "                       ext_port->flags.waiting_recycle) {\n"
        "                // Preserve the upstream no-device cleanup when no retry is pending.\n"
        "                ext_port->flags.waiting_recycle = 0;\n"
        "            }\n"
        "        }\n"
        "        break;\n",
        "disabled-port retry",
    )
    source = replace_once(
        source,
        "            // Do not try to reset port anymore\n"
        "            ext_port->dev_reset_attempts = EXT_PORT_RESET_ATTEMPTS;\n\n"
        "            if (ext_port->dev_state == PORT_DEV_PRESENT) {\n",
        "            // Do not let the ordinary reset-failure path loop. A separate bounded\n"
        "            // budget applies only to failed enumeration of a connected child.\n"
        "            ext_port->dev_reset_attempts = EXT_PORT_RESET_ATTEMPTS;\n"
        "            const bool retry_allowed = ext_port_enum_retry_allowed();\n"
        "            ext_port->flags.enum_retry_pending =\n"
        "                (!ext_port->flags.has_enum_device && retry_allowed &&\n"
        "                 ext_port->enum_retry_attempts < EXT_PORT_ENUM_RETRY_ATTEMPTS)\n"
        "                    ? 1U : 0U;\n"
        "            if (!ext_port->flags.has_enum_device && !retry_allowed &&\n"
        "                    EXT_PORT_ENUM_RETRY_ATTEMPTS > 0) {\n"
        "                ESP_LOGW(EXT_PORT_TAG,\n"
        "                         \"P4_EXT_PORT_ENUM_RETRY_SUPPRESSED Port%d guard=safe-mode\",\n"
        "                         ext_port->constant.port_num);\n"
        "            } else if (!ext_port->flags.has_enum_device &&\n"
        "                       !ext_port->flags.enum_retry_pending) {\n"
        "                ESP_LOGE(EXT_PORT_TAG,\n"
        "                         \"P4_EXT_PORT_ENUM_RETRY_EXHAUSTED Port%d attempts=%d\",\n"
        "                         ext_port->constant.port_num,\n"
        "                         ext_port->enum_retry_attempts);\n"
        "            }\n\n"
        "            if (ext_port->dev_state == PORT_DEV_PRESENT) {\n",
        "disable classification",
    )
    source = replace_once(
        source,
        "    ESP_LOGD(EXT_PORT_TAG, \"Port%d has an enumerated device\", ext_port->constant.port_num);\n\n"
        "    ext_port->flags.has_enum_device = 1;\n",
        "    ESP_LOGD(EXT_PORT_TAG, \"Port%d has an enumerated device\", ext_port->constant.port_num);\n"
        "    if (ext_port->enum_retry_attempts != 0) {\n"
        "        ESP_LOGI(EXT_PORT_TAG,\n"
        "                 \"P4_EXT_PORT_ENUM_RECOVERED Port%d attempts=%d\",\n"
        "                 ext_port->constant.port_num,\n"
        "                 ext_port->enum_retry_attempts);\n"
        "    }\n\n"
        "    ext_port->flags.has_enum_device = 1;\n"
        "    ext_port->flags.enum_retry_pending = 0;\n"
        "    ext_port->enum_retry_attempts = 0;\n",
        "successful enumeration",
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
            "refusing USB overlay: upstream ext_port.c is not the pinned 1.5.0 source"
        )
    generated = generate(payload.decode("utf-8")).encode("utf-8")

    if args.check_output:
        if not args.output.is_file() or args.output.read_bytes() != generated:
            raise SystemExit("generated USB overlay differs or is missing")
        return

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.is_file() or args.output.read_bytes() != generated:
        args.output.write_bytes(generated)


if __name__ == "__main__":
    main()
