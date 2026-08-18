#!/usr/bin/env python3
"""Host proof for the guarded ESP32-P4 HS-root FS/LS timing overlay."""

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/generate-espressif-usb-hcd-fsls-overlay.py"
UPSTREAM = (
    ROOT / "apps/console_os/managed_components/espressif__usb/src/hcd_dwc.c"
)
HUB = ROOT / "apps/console_os/managed_components/espressif__usb/src/hub.c"
HUB_SHA256 = "2d7c79c8508f63be6b0243178eafae2351f4e4643a87d9cdc73e980b95985afe"
GENERATED_BYTES = 120_226
GENERATED_SHA256 = "c71577cbdcc808828216940671be511a074f51fcd88e4f24ef0948d8aebabb8a"


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_generator():
    spec = importlib.util.spec_from_file_location("p4_hcd_fsls_overlay", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load HCD overlay generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GuardedFslsOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.generator = load_generator()
        cls.upstream = UPSTREAM.read_text(encoding="utf-8")
        cls.generated = cls.generator.generate(cls.upstream)

    def test_locked_inputs_are_exact(self) -> None:
        upstream = self.upstream.encode("utf-8")
        self.assertEqual(self.generator.UPSTREAM_BYTES, len(upstream))
        self.assertEqual(self.generator.UPSTREAM_SHA256, digest(upstream))
        self.assertEqual(HUB_SHA256, digest(HUB.read_bytes()))
        generated = self.generated.encode("utf-8")
        self.assertEqual(GENERATED_BYTES, len(generated))
        self.assertEqual(GENERATED_SHA256, digest(generated))

    def test_every_new_register_write_is_durable_guarded(self) -> None:
        helpers = self.generated.split(
            "static bool p4_hs_fsls_reapply_enabled", 1
        )[1].split("static usb_speed_t get_usb_port_speed", 1)[0]
        self.assertIn("p4_usb_hs_fsls_reapply_allowed != NULL", helpers)
        self.assertIn("p4_usb_hs_fsls_reapply_allowed();", helpers)
        self.assertIn("hal->dev == &USB_DWC_HS", helpers)
        for function in (
            "p4_hs_fsls_prepare_reset",
            "p4_hs_fsls_finish_enable",
            "p4_hs_fsls_log_root_ready",
        ):
            body = helpers.split(f"static void {function}", 1)[1].split(
                "\n}\n", 1
            )[0]
            self.assertIn("if (!p4_hs_fsls_reapply_enabled(hal))", body)

    def test_reset_and_enable_order_match_upstream_fix(self) -> None:
        reset = self.generated.split(
            "static esp_err_t _port_cmd_reset(port_t *port)\n{", 1
        )[1].split("static esp_err_t _port_cmd_bus_suspend", 1)[0]
        self.assertLess(
            reset.index("p4_hs_fsls_prepare_reset(port->hal);"),
            reset.index("usb_dwc_hal_port_toggle_reset(port->hal, true);"),
        )
        enabled = self.generated.split(
            "case USB_DWC_HAL_PORT_EVENT_ENABLED:", 1
        )[1].split("case USB_DWC_HAL_PORT_EVENT_DISABLED:", 1)[0]
        self.assertLess(
            enabled.index("usb_dwc_hal_port_enable(port->hal);"),
            enabled.index("p4_hs_fsls_finish_enable(port->hal);"),
        )

    def test_utmi_clock_frame_and_speed_values_are_exact(self) -> None:
        for token in (
            "hal->dev->hcfg_reg.fslssupp = 1U;",
            "hal->dev->hcfg_reg.fslspclksel = 0U;",
            "speed == USB_DWC_SPEED_LOW ? 2U : 0U",
            "speed == USB_DWC_SPEED_LOW ? 5999U : 29999U",
            "speed == USB_DWC_SPEED_HIGH",
            "return USB_DWC_SPEED_FULL;",
        ):
            self.assertIn(token, self.generated)
        self.assertEqual(
            2,
            self.generated.count(
                "conn_speed = p4_hs_fsls_effective_speed(port->hal, conn_speed);"
            ),
        )

    def test_evidence_log_runs_after_hcd_critical_section(self) -> None:
        command = self.generated.split(
            "esp_err_t hcd_port_command", 1
        )[1].split("hcd_port_state_t hcd_port_get_state", 1)[0]
        self.assertLess(
            command.rindex("HCD_EXIT_CRITICAL();"),
            command.index("p4_hs_fsls_log_root_ready(port->hal);"),
        )
        self.assertIn("P4_HS_FSLS_ROOT_READY reset=reapplied", self.generated)

    def test_cli_rejects_mutated_source_and_stale_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            mutated = directory / "hcd_dwc.c"
            output = directory / "generated.c"
            mutated.write_bytes(UPSTREAM.read_bytes() + b"\n")
            rejected = subprocess.run(
                [sys.executable, str(GENERATOR), "--input", str(mutated),
                 "--output", str(output)],
                check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            self.assertNotEqual(0, rejected.returncode)

            output.write_text("stale", encoding="utf-8")
            stale = subprocess.run(
                [sys.executable, str(GENERATOR), "--input", str(UPSTREAM),
                 "--output", str(output), "--check-output"],
                check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            self.assertNotEqual(0, stale.returncode)


if __name__ == "__main__":
    unittest.main()
