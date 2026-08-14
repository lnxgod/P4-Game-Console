#!/usr/bin/env python3

"""Static/adversarial checks for the exclusive gamepad install shell route."""

from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
FLASH = ROOT / "scripts/flash.sh"


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


class GamepadFlashRouteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = FLASH.read_text(encoding="utf-8")

    def test_missing_arm_secret_fails_before_repository_or_serial_access(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fake_port = pathlib.Path(directory) / "not-a-port"
            result = subprocess.run(
                [
                    "sh", str(FLASH), "--app", "gamepad_diag", "--port", str(fake_port),
                    "--app-only", "--defer-launch-for-capture", "--launch-receipt",
                    str(pathlib.Path(directory) / "receipt"),
                ],
                cwd="/",
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
        self.assertEqual(result.returncode, 2)
        self.assertIn("--arm-token-file is required before host or serial access", result.stdout)
        self.assertNotIn("Serial port is not a character device", result.stdout)

    def test_arm_secret_is_strictly_scoped(self) -> None:
        result = subprocess.run(
            ["sh", str(FLASH), "--app", "bringup", "--arm-token-file", "/absent"],
            cwd="/", text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("scoped only to the exact gamepad_diag", result.stdout)

    def test_committed_inactive_app_flash_rejects_before_serial_or_toolchain(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            private = pathlib.Path(directory)
            token = private / "token"
            token.write_text("a" * 64 + "\n", encoding="ascii")
            token.chmod(0o600)
            for port in (private / "not-a-port", pathlib.Path("/dev/null")):
                result = subprocess.run(
                    [
                        "sh", str(FLASH), "--app", "gamepad_diag", "--port", str(port),
                        "--app-only", "--defer-launch-for-capture", "--launch-receipt",
                        str(private / "receipt"), "--arm-token-file", str(token),
                    ],
                    cwd="/", text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, check=False,
                )
                with self.subTest(port=port):
                    self.assertEqual(result.returncode, 1)
                    self.assertIn("flash_app_authorized is false", result.stdout)
                    self.assertNotIn("Serial port is not a character device", result.stdout)
                    self.assertNotIn("ESP-IDF", result.stdout)

    def test_policy_and_private_token_validation_precede_any_port_path(self) -> None:
        policy = self.source.index('p4_require_flash_authorized "$P4_APP"')
        token = self.source.index("check-arm-secret")
        first_port = self.source.index('p4_require_port "$P4_PORT"')
        helper = self.source.index('python3 "$P4_GAMEPAD_INSTALL_TOOL"')
        self.assertLess(policy, first_port)
        self.assertLess(token, first_port)
        self.assertLess(first_port, helper)
        non_gamepad_guard = section(
            self.source,
            'if [ "$P4_APP" != gamepad_diag ]; then',
            'p4_activate_idf',
        )
        self.assertIn('p4_require_port "$P4_PORT"', non_gamepad_guard)

    def test_recovery_bundle_is_durable_and_cleanup_never_removes_it(self) -> None:
        setup = section(
            self.source,
            'if [ "$P4_APP" = gamepad_diag ]; then\n    if [ "$P4_FLASH_TARGET"',
            'if [ "$P4_APP" = audio_diag ]; then',
        )
        for token in (
            'P4_GAMEPAD_RECOVERY_ROOT="$P4_PROJECT_ROOT/hardware/local-state"',
            "gamepad-diag-recovery.XXXXXX",
            'chmod 700 "$P4_GAMEPAD_RECOVERY_DIR"',
            'P4_GAMEPAD_OWNER_TOKEN_FILE="$P4_GAMEPAD_RECOVERY_DIR/owner-token"',
            'P4_GAMEPAD_ARM_SECRET_FILE="$P4_GAMEPAD_RECOVERY_DIR/arm-token"',
            "P4_GAMEPAD_RESERVATION_ACTIVE=true",
        ):
            self.assertIn(token, setup)
        cleanup = section(self.source, "p4_cleanup_flash_temps() {", "p4_handle_hup() {")
        self.assertNotIn("rm -rf", cleanup)
        self.assertNotIn('rm -f "$P4_GAMEPAD_RECOVERY_DIR', cleanup)

    def test_exact_gamepad_route_invokes_only_one_exclusive_installer(self) -> None:
        transaction = section(
            self.source,
            'if [ "$P4_APP" = gamepad_diag ]; then\n        p4_require_port',
            '    P4_FLASH_OUTPUT=$(esptool.py',
        )
        self.assertEqual(transaction.count('python3 "$P4_GAMEPAD_INSTALL_TOOL"'), 1)
        self.assertNotIn("esptool.py", transaction)
        self.assertNotIn("p4_verify_chunked_application_readback", transaction)
        self.assertNotIn("mark-install-write-attempt", transaction)
        self.assertNotIn("mark-installed-verified", transaction)
        self.assertNotIn("emit-receipt", transaction)
        for argument in (
            '--port "$P4_PORT"', '--artifact "$P4_BUILT_APP_PATH"',
            '--arm-token-file "$P4_GAMEPAD_ARM_TOKEN_FILE"',
            '--recovery-dir "$P4_GAMEPAD_RECOVERY_DIR"',
            '--state "$P4_GAMEPAD_STATE"', '--authorization "$P4_GAMEPAD_AUTH"',
            '--project-root "$P4_PROJECT_ROOT"',
            '--owner-token-file "$P4_GAMEPAD_OWNER_TOKEN_FILE"',
            '--receipt "$P4_LAUNCH_RECEIPT"',
            '--result "$P4_GAMEPAD_INSTALL_RESULT"',
        ):
            self.assertIn(argument, transaction)
        self.assertIn("exit 0", transaction)

    def test_gamepad_has_no_shell_esptool_between_build_and_exclusive_helper(self) -> None:
        build = self.source.index('"$P4_SCRIPT_DIR/build.sh" "$P4_APP"')
        helper = self.source.index('python3 "$P4_GAMEPAD_INSTALL_TOOL"')
        self.assertNotIn("esptool.py", self.source[build:helper])
        self.assertNotIn("p4_revalidate_gamepad_live_device", self.source)
        self.assertIn(
            'P4_GAMEPAD_INSTALL_TOOL="$P4_SCRIPT_DIR/gamepad-diag-install.py"',
            self.source,
        )

    def test_exclusive_result_is_exact_before_reservation_release(self) -> None:
        transaction = section(
            self.source,
            'P4_GAMEPAD_INSTALL_SUMMARY=$(python3 -c',
            '        p4_cleanup_flash_temps',
        )
        for token in (
            'value.get("same_uart_handle") is True',
            'value.get("exclusive_uart") is True',
            'value.get("download_reset_count") == 1',
            'value.get("connect_no_reset_attempts") == 1',
            'value.get("write_attempt_count") == 1',
            'value.get("write_after_action") == "no_reset"',
            'value.get("readback_after_action") == "no_reset"',
            'value.get("post_install_reset_count") == 0',
            'P4_GAMEPAD_MUTATION_SPAN" != 311296',
        ):
            self.assertIn(token, transaction)
        self.assertLess(
            transaction.index("exclusive install result is not exact"),
            transaction.index("P4_GAMEPAD_RESERVATION_ACTIVE=false"),
        )

    def test_cleanup_destroys_only_staged_secret_and_preserves_recovery_bundle(self) -> None:
        destroy = section(
            self.source,
            "p4_destroy_gamepad_arm_secret() {",
            "p4_remove_verified_app() {",
        )
        cleanup = section(self.source, "p4_cleanup_flash_temps() {", "p4_handle_hup() {")
        term = section(self.source, "p4_handle_term() {", "trap p4_cleanup_flash_temps EXIT")
        for phase in ("prebind", "postwrite", "term"):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                recovery = root / f"gamepad-diag-recovery.{phase}"
                recovery.mkdir(mode=0o700)
                owner = recovery / "owner-token"
                partition = recovery / "live-partition-table.bin"
                preimage = recovery / "preinstall-mutation-span.bin"
                secret = recovery / "arm-token"
                owner.write_text("owner\n", encoding="ascii")
                partition.write_bytes(b"partition recovery evidence")
                preimage.write_bytes(b"preimage recovery evidence")
                secret.write_bytes(b"a" * 64 + b"\n")
                for path in (owner, partition, preimage, secret):
                    path.chmod(0o600)
                state_log = root / "state.log"
                state_tool = root / "state-tool.py"
                state_tool.write_text(
                    "import os, pathlib, sys\n"
                    "pathlib.Path(os.environ['P4_TEST_STATE_LOG']).write_text("
                    "' '.join(sys.argv[1:]), encoding='utf-8')\n",
                    encoding="utf-8",
                )
                harness = root / "cleanup.sh"
                action = "trap p4_handle_term TERM\nkill -TERM $$\n" if phase == "term" else "p4_cleanup_flash_temps\n"
                harness.write_text(
                    "#!/bin/sh\nset -eu\n"
                    "P4_D23_RESERVATION_ACTIVE=false\nP4_E3_RESERVATION_ACTIVE=false\n"
                    "P4_GAMEPAD_RESERVATION_ACTIVE=true\n"
                    'P4_GAMEPAD_RECOVERY_ROOT="$P4_TEST_RECOVERY_ROOT"\n'
                    'P4_GAMEPAD_RECOVERY_DIR="$P4_TEST_RECOVERY_DIR"\n'
                    'P4_GAMEPAD_ARM_SECRET_FILE="$P4_GAMEPAD_RECOVERY_DIR/arm-token"\n'
                    'P4_GAMEPAD_OWNER_TOKEN_FILE="$P4_GAMEPAD_RECOVERY_DIR/owner-token"\n'
                    'P4_GAMEPAD_STATE_TOOL="$P4_TEST_STATE_TOOL"\nP4_GAMEPAD_STATE=state.json\n'
                    "p4_remove_readback() { :; }\np4_remove_verified_app() { :; }\n"
                    + destroy + cleanup + term + action + "printf 'UNEXPECTED_CONTINUATION\\n'\n",
                    encoding="utf-8",
                )
                environment = os.environ.copy()
                environment.update({
                    "P4_TEST_RECOVERY_ROOT": str(root),
                    "P4_TEST_RECOVERY_DIR": str(recovery),
                    "P4_TEST_STATE_TOOL": str(state_tool),
                    "P4_TEST_STATE_LOG": str(state_log),
                })
                result = subprocess.run(
                    ["sh", str(harness)], text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, env=environment, check=False,
                )
                self.assertEqual(result.returncode, 143 if phase == "term" else 0)
                self.assertFalse(secret.exists())
                self.assertEqual(owner.read_text(encoding="ascii"), "owner\n")
                self.assertEqual(partition.read_bytes(), b"partition recovery evidence")
                self.assertEqual(preimage.read_bytes(), b"preimage recovery evidence")
                self.assertIn("fail --state state.json", state_log.read_text(encoding="utf-8"))

    def test_cleanup_never_terminalizes_postwrite_state(self) -> None:
        cleanup = section(self.source, "p4_cleanup_flash_temps() {", "p4_handle_hup() {")
        self.assertIn('python3 "$P4_GAMEPAD_STATE_TOOL" fail', cleanup)
        self.assertIn("p4_destroy_gamepad_arm_secret", cleanup)
        self.assertNotIn("complete", cleanup)
        self.assertNotIn("terminalize", cleanup)


if __name__ == "__main__":
    unittest.main()
