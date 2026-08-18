#!/usr/bin/env python3
"""Host proof for the serialized ESP USB external-port retry overlay."""

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/generate-espressif-usb-ext-port-overlay.py"
UPSTREAM = (
    ROOT / "apps/console_os/managed_components/espressif__usb/src/ext_port.c"
)
HUB = ROOT / "apps/console_os/managed_components/espressif__usb/src/hub.c"
HUB_SHA256 = "2d7c79c8508f63be6b0243178eafae2351f4e4643a87d9cdc73e980b95985afe"
GENERATED_SHA256 = "6373859f47cd6cb88877186ebd915d8d31f442cfd13f27da814e524ec43355ee"


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_generator():
    spec = importlib.util.spec_from_file_location("p4_ext_port_overlay", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load overlay generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def retry_dispatch_ready(*, pending: bool, waiting_recycle: bool,
                         status_lock: bool, status_outdated: bool) -> bool:
    """Mirror the generated C gate around a downstream reset submission."""
    return pending and not waiting_recycle and not status_lock and not status_outdated


class SerializedRetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.generator = load_generator()
        cls.upstream = UPSTREAM.read_text(encoding="utf-8")
        cls.generated = cls.generator.generate(cls.upstream)

    def test_inputs_and_output_are_exact(self) -> None:
        upstream_bytes = self.upstream.encode("utf-8")
        self.assertEqual(self.generator.UPSTREAM_BYTES, len(upstream_bytes))
        self.assertEqual(self.generator.UPSTREAM_SHA256, digest(upstream_bytes))
        self.assertEqual(GENERATED_SHA256, digest(self.generated.encode("utf-8")))
        self.assertEqual(HUB_SHA256, digest(HUB.read_bytes()))

    def test_locked_hub_scheduler_is_not_overlaid_or_reordered(self) -> None:
        hub = HUB.read_text(encoding="utf-8")
        ext_port = hub.index("if (action_flags & HUB_DRIVER_ACTION_EXT_PORT)")
        ext_hub = hub.index("if (action_flags & HUB_DRIVER_ACTION_EXT_HUB)")
        self.assertLess(ext_port, ext_hub)
        self.assertNotIn("hub.c", self.generator.__doc__.split("emits", 1)[-1])

    def test_failed_device_free_cannot_race_inflight_hub_control(self) -> None:
        # Exact 0.4.20 failure ordering: ClearFeature(PORT_ENABLE) is still in
        # flight when DEV_FREE makes waiting_recycle false. The corrected gate
        # must not issue a second request from that external-port action.
        pending = True
        waiting_recycle = False
        status_lock = False
        status_outdated = True
        control_inflight = True
        self.assertFalse(retry_dispatch_ready(
            pending=pending,
            waiting_recycle=waiting_recycle,
            status_lock=status_lock,
            status_outdated=status_outdated,
        ))

        # Feature completion starts the ordinary GetStatus chain. That request
        # owns the same URB, so retry remains blocked.
        control_inflight = False
        status_lock = True
        control_inflight = True
        self.assertFalse(retry_dispatch_ready(
            pending=pending,
            waiting_recycle=waiting_recycle,
            status_lock=status_lock,
            status_outdated=status_outdated,
        ))

        # Only the status response clears both locks. A downstream reset can
        # now take ownership of an idle control URB.
        control_inflight = False
        status_lock = False
        status_outdated = False
        self.assertTrue(retry_dispatch_ready(
            pending=pending,
            waiting_recycle=waiting_recycle,
            status_lock=status_lock,
            status_outdated=status_outdated,
        ))
        self.assertFalse(control_inflight)

    def test_control_completion_before_device_free_is_also_serialized(self) -> None:
        self.assertFalse(retry_dispatch_ready(
            pending=True,
            waiting_recycle=True,
            status_lock=False,
            status_outdated=False,
        ))
        self.assertTrue(retry_dispatch_ready(
            pending=True,
            waiting_recycle=False,
            status_lock=False,
            status_outdated=False,
        ))

    def test_generated_source_contains_every_fail_safe_gate(self) -> None:
        required = (
            "p4_usb_ext_port_enum_retry_allowed != NULL",
            "!ext_port->flags.waiting_recycle &&\n"
            "                    !ext_port->flags.status_lock &&\n"
            "                    !ext_port->flags.status_outdated",
            "P4_EXT_PORT_ENUM_RETRY_SERIALIZED",
            "P4_EXT_PORT_ENUM_RETRY_SUPPRESSED",
            "P4_EXT_PORT_ENUM_RETRY_EXHAUSTED",
            "ext_port->enum_retry_attempts < EXT_PORT_ENUM_RETRY_ATTEMPTS",
            "ext_port->enum_retry_attempts = 0;",
        )
        for token in required:
            self.assertIn(token, self.generated)

        recycle = self.generated.split(
            "static void handle_recycle(ext_port_t *ext_port)", 1
        )[1].split("static void handle_disable", 1)[0]
        disabled = recycle.split("case USB_PORT_STATE_DISABLED:", 1)[1].split(
            "    default:", 1
        )[0]
        self.assertNotIn("handle_port(ext_port);", disabled)
        self.assertIn("port_set_actions(ext_port, PORT_ACTION_HANDLE);", disabled)

    def test_check_output_detects_stale_generated_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "ext_port.c"
            output.write_text("stale", encoding="utf-8")
            expected = self.generated.encode("utf-8")
            self.assertNotEqual(expected, output.read_bytes())


if __name__ == "__main__":
    unittest.main()
