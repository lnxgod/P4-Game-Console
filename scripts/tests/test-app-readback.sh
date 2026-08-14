#!/bin/sh

set -eu

P4_TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_SCRIPT_DIR=$(CDPATH= cd -- "$P4_TEST_DIR/.." && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/app-readback.sh"

P4_TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/p4-readback-test.XXXXXX")
p4_test_cleanup() {
    p4_remove_readback || true
    rm -rf -- "$P4_TEST_ROOT"
}
trap p4_test_cleanup EXIT HUP INT TERM

mkdir "$P4_TEST_ROOT/tmp"
P4_TEST_SOURCE="$P4_TEST_ROOT/application.bin"
python3 -c '
import pathlib, sys
size = 2 * 524288 + 37
pathlib.Path(sys.argv[1]).write_bytes(
    bytes(((index * 29 + index // 524288 * 71) & 0xff) for index in range(size))
)
' "$P4_TEST_SOURCE"
P4_TEST_SOURCE_BYTES=$(wc -c < "$P4_TEST_SOURCE" | tr -d ' ')
P4_TEST_SOURCE_HASH=$(p4_sha256_file "$P4_TEST_SOURCE")

PATH="$P4_TEST_DIR/fixtures:$PATH"
export PATH
TMPDIR="$P4_TEST_ROOT/tmp"
export TMPDIR
P4_MOCK_READBACK_SOURCE=$P4_TEST_SOURCE
P4_MOCK_READBACK_BASE_OFFSET=65536
P4_MOCK_READBACK_LOG="$P4_TEST_ROOT/success.calls"
P4_MOCK_READBACK_MODE=success
P4_MOCK_READBACK_FAIL_BAUD=460800
export P4_MOCK_READBACK_SOURCE P4_MOCK_READBACK_BASE_OFFSET
export P4_MOCK_READBACK_LOG P4_MOCK_READBACK_MODE P4_MOCK_READBACK_FAIL_BAUD

P4_TEST_SUCCESS_OUTPUT="$P4_TEST_ROOT/success.output"
if ! p4_verify_chunked_application_readback \
    "$P4_TEST_SOURCE" 0x10000 "$P4_TEST_SOURCE_BYTES" /dev/mock \
    no_reset '460800 230400 115200' \
    >"$P4_TEST_SUCCESS_OUTPUT" 2>&1; then
    printf 'chunked readback success/retry mock failed\n' >&2
    sed -n '1,240p' "$P4_TEST_SUCCESS_OUTPUT" >&2
    exit 1
fi
if [ "$P4_READBACK_HASH" != "$P4_TEST_SOURCE_HASH" ] || \
   [ "$P4_READBACK_ACTUAL_BYTES" != "$P4_TEST_SOURCE_BYTES" ] || \
   [ "$P4_READBACK_CHUNKS" != 3 ]; then
    printf 'ordered aggregate result differs from source image\n' >&2
    exit 1
fi
if grep -F 'aa:bb:cc:dd:ee:ff' "$P4_TEST_SUCCESS_OUTPUT" >/dev/null || \
   ! grep -F 'MAC: [redacted]' "$P4_TEST_SUCCESS_OUTPUT" >/dev/null; then
    printf 'mocked readback output exposed a raw device identifier\n' >&2
    exit 1
fi
if [ "$(wc -l < "$P4_MOCK_READBACK_LOG" | tr -d ' ')" != 6 ] || \
   ! awk '$2 > 524288 { exit 1 }' "$P4_MOCK_READBACK_LOG"; then
    printf 'readback was not bounded to three 512 KiB-or-smaller chunks with one retry each\n' >&2
    exit 1
fi
P4_TEST_EXPECTED_CALLS="$P4_TEST_ROOT/expected.calls"
printf '%s\n' \
    '65536 524288 460800 success' \
    '65536 524288 230400 success' \
    '589824 524288 460800 success' \
    '589824 524288 230400 success' \
    '1114112 37 460800 success' \
    '1114112 37 230400 success' | \
    awk '{ print $0 " default_reset no_reset" }' >"$P4_TEST_EXPECTED_CALLS"
if ! cmp -s "$P4_TEST_EXPECTED_CALLS" "$P4_MOCK_READBACK_LOG"; then
    printf 'lower-baud retry was not scoped independently to each ordered chunk\n' >&2
    exit 1
fi

# Reordering two equal-sized canonical chunks must change the aggregate hash.
P4_TEST_SWAP="$P4_READBACK_DIR/chunk-swap.bin"
mv "$P4_READBACK_DIR/chunk-000000.bin" "$P4_TEST_SWAP"
mv "$P4_READBACK_DIR/chunk-000001.bin" "$P4_READBACK_DIR/chunk-000000.bin"
mv "$P4_TEST_SWAP" "$P4_READBACK_DIR/chunk-000001.bin"
P4_TEST_REORDERED=$(python3 "$P4_SCRIPT_DIR/verify-readback-chunks.py" aggregate \
    --directory "$P4_READBACK_DIR" --chunks 3 --bytes "$P4_TEST_SOURCE_BYTES" \
    --chunk-bytes 524288)
P4_TEST_REORDERED_HASH=${P4_TEST_REORDERED#* }
if [ "$P4_TEST_REORDERED_HASH" = "$P4_TEST_SOURCE_HASH" ]; then
    printf 'ordered aggregate hash did not detect reordered chunks\n' >&2
    exit 1
fi
P4_TEST_FIRST_DIR=$P4_READBACK_DIR
p4_remove_readback
if [ -e "$P4_TEST_FIRST_DIR" ]; then
    printf 'successful readback temporary directory was not removed\n' >&2
    exit 1
fi

# Exact-length corrupt reads must exhaust the bounded baud list and fail closed.
P4_MOCK_READBACK_LOG="$P4_TEST_ROOT/corrupt.calls"
P4_MOCK_READBACK_MODE=corrupt
P4_MOCK_READBACK_FAIL_BAUD=
export P4_MOCK_READBACK_LOG P4_MOCK_READBACK_MODE P4_MOCK_READBACK_FAIL_BAUD
P4_TEST_FAIL_OUTPUT="$P4_TEST_ROOT/failure.output"
if p4_verify_chunked_application_readback \
    "$P4_TEST_SOURCE" 0x10000 "$P4_TEST_SOURCE_BYTES" /dev/mock \
    no_reset '460800 230400 115200' no_reset \
    >"$P4_TEST_FAIL_OUTPUT" 2>&1; then
    printf 'corrupt readback unexpectedly passed\n' >&2
    exit 1
fi
if ! awk '$5 != "no_reset" || $6 != "no_reset" { exit 1 }' \
    "$P4_MOCK_READBACK_LOG"; then
    printf 'explicit no-reset readback action was not passed to every attempt\n' >&2
    exit 1
fi
if [ "$(wc -l < "$P4_MOCK_READBACK_LOG" | tr -d ' ')" != 3 ] || \
   grep -F 'aa:bb:cc:dd:ee:ff' "$P4_TEST_FAIL_OUTPUT" >/dev/null || \
   ! grep -F 'MAC: [redacted]' "$P4_TEST_FAIL_OUTPUT" >/dev/null; then
    printf 'corrupt-readback retry/redaction behavior differs\n' >&2
    exit 1
fi
P4_TEST_SECOND_DIR=$P4_READBACK_DIR
if [ "$P4_TEST_SECOND_DIR" = "$P4_TEST_FIRST_DIR" ]; then
    printf 'readback reused a prior temporary directory\n' >&2
    exit 1
fi
p4_remove_readback
if [ -e "$P4_TEST_SECOND_DIR" ]; then
    printf 'failed readback temporary directory was not removed\n' >&2
    exit 1
fi

# The central wrapper must exit on verifier failure before either hash acceptance
# or the explicit deferred run.  This complements the transport mock above.
python3 -c '
import pathlib, sys
flash = pathlib.Path(sys.argv[1]).read_text()
guard = flash.index("if ! p4_verify_chunked_application_readback")
failure = flash.index("image was not launched.", guard)
failure_exit = flash.index("exit 1", failure)
hash_gate = flash.index("if [ \"$P4_READBACK_HASH\" != \"$P4_BUILT_APP_HASH\" ]", failure_exit)
run = flash.index("P4_RUN_OUTPUT=$(esptool.py", hash_gate)
if not guard < failure < failure_exit < hash_gate < run:
    raise SystemExit("central flash wrapper is not fail-closed before explicit run")
' "$P4_SCRIPT_DIR/flash.sh"

printf 'application readback tests: PASS chunks=3 max_chunk_bytes=524288 retry=per-chunk fail_closed=true\n'
