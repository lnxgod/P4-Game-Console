#!/usr/bin/env python3
"""Exercise the production status formatter/protocol and guarded transport wait.

Only the formatter is extracted; the actual debug control codec, status response
parser, shell types and guarded_batch implementation are used unchanged. A
virtual clock and command callback replace device I/O. P4_CONSOLE_BASE supplies
unchanged dependencies when running this file from a partial staging tree.
"""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get("P4_CONSOLE_BASE", ROOT))
SOURCE = Path(os.environ.get("P4_CONSOLE_DEBUG_SOURCE",
    ROOT / "apps/console_os/main/console_os_main.c"))
GUARD = Path(os.environ.get("P4_GUARDED_BATCH_SOURCE",
    ROOT / "build-host/native-multiplayer-runner-067/guarded_batch.py"))


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def function(source, name):
    match = re.search(r"^static\s+[^;{}]*?\b" + name +
                      r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"Missing production function {name}")
    brace, depth = match.end() - 1, 0
    for token in re.finditer(
            r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
            source[brace:]):
        depth += (token.group() == "{") - (token.group() == "}")
        if depth == 0:
            return source[match.start():brace + token.end()]
    raise ValueError("Unclosed production function")


FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console/shell.h"
#include "p4/debug_control.h"
static console_shell_t s_shell;
static const char *s_debug_runtime;
static char s_debug_game_id[P4_GAME_ID_MAX_BYTES];
'''

DRIVER = r'''
static bool send_reply(void *context, const uint8_t *bytes, size_t size) {
    (void)context;
    assert(size == P4_DEBUG_RESPONSE_BYTES);
    return fwrite(bytes, 1, size, stdout) == size;
}
static uint32_t request_crc(const uint8_t *p, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}
int main(int argc, char **argv) {
    assert(argc == 6);
    s_debug_runtime = argv[1];
    s_shell.page = CONSOLE_PAGE_MULTIPLAYER;
    s_shell.multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_ROLE;
    s_shell.runtime.multiplayer_transport_kind = 2;
    s_shell.runtime.multiplayer_transport_ready = atoi(argv[2]) != 0;
    s_shell.runtime.multiplayer_transport_starting = atoi(argv[3]) != 0;
    s_shell.runtime.multiplayer_lobby_ready = atoi(argv[4]) != 0;
    memset(s_debug_game_id, 'x', sizeof(s_debug_game_id) - 1);
    if (atoi(argv[5])) {
        s_shell.page = CONSOLE_PAGE_APPEARANCE;
        s_shell.active_app_id = UINT32_MAX;
        s_shell.multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_TRANSPORT;
        s_shell.multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_COUNT;
        s_shell.runtime.multiplayer_game_selection = CONSOLE_MULTIPLAYER_MAX_GAMES - 1;
        s_shell.runtime.multiplayer_lobby_count = CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX;
        if (atoi(argv[5]) == 2) s_shell.runtime.multiplayer_game_selection = UINT8_MAX;
    }
    /* Check snprintf's bound for every short buffer, including zero capacity. */
    for (size_t capacity = 0; capacity <= P4_DEBUG_STATUS_BYTES; ++capacity) {
        unsigned char guard[P4_DEBUG_STATUS_BYTES + 2];
        memset(guard, 0xa5, sizeof(guard));
        debug_usb_status(NULL, (char *)guard + 1, capacity);
        assert(guard[0] == 0xa5);
        for (size_t i = capacity + 1; i < sizeof(guard); ++i) assert(guard[i] == 0xa5);
        if (capacity) assert(memchr(guard + 1, 0, capacity) != NULL);
    }
    p4_debug_control_t control;
    p4_debug_control_init(&control, send_reply, debug_usb_status, NULL);
    uint8_t request[P4_DEBUG_REQUEST_BYTES] = {'P','4','D','1',1,P4_DEBUG_STATUS};
    const uint32_t crc = request_crc(request, 28);
    for (unsigned i = 0; i < 4; ++i) request[28 + i] = (uint8_t)(crc >> (8 * i));
    assert(p4_debug_control_consume(&control, request, sizeof(request), 1));
    return 0;
}
'''


class TransportStatusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-transport-status-")
        cls.addClassCleanup(cls.temp.cleanup)
        generated = Path(cls.temp.name) / "status.c"
        generated.write_text(FIXTURE + function(SOURCE.read_text(), "debug_usb_status") + DRIVER)
        cls.binary = generated.with_suffix("")
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-DCONFIG_P4_BOARD_M5STACK_TAB5=1"]
        for component in ("console_shell", "p4_game_api", "p4_desktop", "p4_usb_content_transfer"):
            command += ["-I", str(BASE / "components" / component / "include")]
        subprocess.run(command + [str(generated), str(BASE /
            "components/p4_usb_content_transfer/src/debug_control.c"), "-o", str(cls.binary)], check=True)
        cls.guard = module("transport_guard", GUARD)
        cls.client = module("transport_debug_client", BASE / "scripts/p4-debug-control.py")

    def status(self, ready=0, starting=0, lobby=0, mode="shell", maximum=0):
        wire = subprocess.check_output([str(self.binary), mode, str(ready),
                                       str(starting), str(lobby), str(maximum)])
        self.assertEqual(len(wire), 128)
        response = self.client.parse_response(wire)
        self.assertEqual(response["result"], "ok")
        return response

    def test_transport_flags_are_independent_of_lobby_ready(self):
        for ready in (0, 1):
            for starting in (0, 1):
                for lobby in (0, 1):
                    with self.subTest(ready=ready, starting=starting, lobby=lobby):
                        fields = self.guard.status_fields(self.status(ready, starting, lobby))
                        self.assertEqual(fields, dict(mode="shell", page="9", app="0", view="0",
                            row="0", game="0", link="2", rooms="0", ready=str(lobby),
                            tr=str(ready), ts=str(starting)))

    def test_largest_valid_values_fit_the_existing_response(self):
        for mode in ("shell", "loading", "doom"):
            for maximum in (1, 2):
                response = self.status(1, 1, 1, mode, maximum)
                fields = self.guard.status_fields(response)
                self.assertEqual(fields["app"], "4294967295")
                self.assertEqual(fields["game"], "18" if maximum == 1 else "255")
                self.assertEqual(fields["rooms"], "4")
                self.assertEqual((fields["tr"], fields["ts"]), ("1", "1"))
                self.assertLessEqual(len(response["text"].encode()), 91)

    def test_native_status_remains_unchanged(self):
        fields = self.guard.status_fields(self.status(1, 1, 1, "native", 1))
        self.assertEqual(fields, {"mode": "native", "id": "x" * 47})

    def batch(self, initial, final=None, ready_after=0, timeout=4000):
        clock, calls = [0.0], []
        def dispatch(request):
            calls.append((clock[0], dict(request)))
            response = final if final is not None and clock[0] >= ready_after else initial
            return {"response": {unit: response for unit in
                    (("A", "B") if request["unit"] == "both" else (request["unit"],))}}
        def sleep(seconds):
            clock[0] += seconds
        result = self.guard.run(dispatch, {"command": "guarded_batch", "name": "wifi-role",
            "steps": [{"expect": {"B": {"mode": "shell", "page": 9, "view": 0,
                         "link": 2, "tr": 1, "ts": 0}}, "timeout_ms": timeout},
                      {"request": {"command": "tap", "unit": "B", "x": 1010,
                                   "y": 402, "hold_ms": 150}}]},
            monotonic=lambda: clock[0], sleep=sleep)
        return result, calls, clock[0]

    def test_cold_start_waits_then_taps_without_lobby_ready(self):
        result, calls, elapsed = self.batch(self.status(0, 1, 0),
            self.status(1, 0, 0), ready_after=2.460)
        self.assertTrue(result["ok"], result)
        taps = [(when, req) for when, req in calls if req["command"] == "tap"]
        self.assertEqual(len(taps), 1)
        self.assertEqual(taps[0][0], 2.5)
        self.assertEqual(elapsed, 2.5)

    def test_failed_or_stuck_transport_times_out_before_tap(self):
        for ready, starting in ((0, 0), (0, 1), (1, 1)):
            with self.subTest(ready=ready, starting=starting):
                result, calls, elapsed = self.batch(self.status(ready, starting, 1), timeout=700)
                self.assertFalse(result["ok"], result)
                self.assertIn("status wait expired", result["error"])
                self.assertEqual(elapsed, .7)
                self.assertNotIn("tap", [req["command"] for _, req in calls])
                self.assertEqual(calls[-1][1], {"command": "release", "unit": "both"})

    def test_older_firmware_missing_transport_fields_fails_closed(self):
        response = self.status(1, 0, 1)
        response["text"] = " ".join(part for part in response["text"].split()
                                    if not part.startswith(("tr=", "ts=")))
        result, calls, elapsed = self.batch(response, timeout=700)
        self.assertFalse(result["ok"])
        self.assertEqual(elapsed, .7)
        self.assertNotIn("tap", [req["command"] for _, req in calls])

    def test_new_predicates_preserve_existing_validation(self):
        for key, value in (("tr", {"min": 1}), ("ts", True), ("tready", 1)):
            with self.subTest(key=key, value=value):
                calls = []
                with self.assertRaises(ValueError):
                    self.guard.run(calls.append, {"command": "guarded_batch", "name": "invalid",
                        "steps": [{"expect": {"B": {"mode": "shell", key: value}}}]})
                self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
