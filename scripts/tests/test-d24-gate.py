#!/usr/bin/env python3
"""Host-only adversarial checks for the inactive D2.4 one-shot gate."""

from __future__ import annotations

import datetime as dt
import hashlib
import json
import math
import pathlib
import stat
import struct
import subprocess
import sys
import tempfile
import wave


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
import d24_capture_transport as transport  # noqa: E402


def write_private(path: pathlib.Path, value: dict) -> None:
    path.write_text(json.dumps(value) + "\n")
    path.chmod(0o600)


def must_fail(callable_, message: str) -> None:
    try:
        callable_()
    except Exception:
        return
    raise AssertionError(message)


def write_wav(path: pathlib.Path, samples: list[int]) -> bytes:
    pcm = b"".join(struct.pack("<h", sample) for sample in samples)
    with wave.open(str(path), "wb") as recording:
        recording.setnchannels(1)
        recording.setsampwidth(2)
        recording.setframerate(48_000)
        recording.writeframes(pcm)
    return pcm


def analyzer_timing(
    wav: pathlib.Path, pcm: bytes, ledger_path: pathlib.Path,
    clipped: bool = False,
) -> dict:
    del clipped
    base = 1_800_000_000_000_000_000
    names = ("SERIAL_ATTACH", "WAIT_ARM", "ARM_ACCEPTED", *transport.MARKER_NAMES)
    offsets = {
        "SERIAL_ATTACH": 0.1,
        "WAIT_ARM": 0.2,
        "ARM_ACCEPTED": 0.3,
        "START": 0.4,
        "SAFE": 0.5,
        "BACKEND_READY": 0.6,
        "POWER_READY": 0.7,
        "CAPTURE_ARM": 1.0,
        "TONE_BEGIN": 3.0,
        "POSTROLL": 3.52,
        "PASS": 4.0,
        "HEARTBEAT": 5.0,
    }
    raw = wav.read_bytes()
    analyzer = SCRIPTS / "analyze-audio-level-tone.py"
    lines = [
        transport.SERIAL_ATTACH_LINE,
        transport.WAIT_ARM_LINE,
        "P4_AUDIO D2.4 ARM_ACCEPTED auth=" + transport.AUTH_ID
        + " nonce=" + transport.ARM_NONCE + " tx_count=1",
        *transport.FIXED_MARKER_LINES,
    ]
    callbacks = len(pcm) // (480 * 2)
    assert callbacks * 480 * 2 == len(pcm)
    return {
        "schema": 1,
        "result": "pass",
        "transport_finalized": True,
        "ledger_path": str(ledger_path.resolve()),
        "authorization_id": transport.AUTH_ID,
        "analyzer": {
            "path": str(analyzer.resolve()),
            "sha256": hashlib.sha256(analyzer.read_bytes()).hexdigest(),
        },
        "serial_exact_marker_lines": lines,
        "serial_markers_ordered": True,
        "serial_reject_markers_absent": True,
        "serial_transmitted_frames": 1,
        "serial_transmitted_bytes": transport.ARM_FRAME_BYTES,
        "serial_transmitted_sha256": transport.ARM_FRAME_SHA256,
        "serial_event_wall_ns": {
            name: base + round(offsets[name] * 1_000_000_000) for name in names
        },
        "audio_first_sample_wall_ns": base,
        "audio_sample_rate_hz": 48_000,
        "audio_channels": 1,
        "audio_sample_width_bytes": 2,
        "audio_frames": len(pcm) // 2,
        "audio_bytes": len(pcm),
        "audio_sha256_pcm": hashlib.sha256(pcm).hexdigest(),
        "audio_callback_count": callbacks,
        "audio_callback_status_flags": [0] * callbacks,
        "serial_close_count": 1,
        "audio_close_count": 1,
        "serial_close_monotonic_ns": base + 6_000_000_000,
        "audio_close_monotonic_ns": base + 6_100_000_000,
        "wav": str(wav.resolve()),
        "wav_bytes": len(raw),
        "wav_sha256": hashlib.sha256(raw).hexdigest(),
    }


def persist_completed_timing(
    timing_path: pathlib.Path, timing: dict, ledger_path: pathlib.Path,
) -> None:
    timing_path.write_text(json.dumps(timing) + "\n")
    timing_path.chmod(0o600)
    write_private(ledger_path, {
        "schema": 1,
        "authorization_id": transport.AUTH_ID,
        "status": "completed",
        "launch_count": 1,
        "outcome": "completed",
        "timing_path": str(timing_path.resolve()),
        "timing_sha256": hashlib.sha256(timing_path.read_bytes()).hexdigest(),
    })


def run_analyzer(wav: pathlib.Path, timing: pathlib.Path, output: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(SCRIPTS / "analyze-audio-level-tone.py"),
            str(wav),
            "--timing-json", str(timing),
            "--json", str(output),
            "--operator-audibility", "unavailable",
            "--operator-pop", "unavailable",
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> None:
    assert transport.APP == "audio_level_diag"
    assert transport.AUTH_ID == "audio-level-diag-d24-one-shot-authorization-2026-08-13"
    assert transport.ARM_NONCE == hashlib.sha256(
        transport.ARM_NONCE_SEED.encode("ascii")
    ).hexdigest()
    assert len(transport.ARM_FRAME.encode("ascii")) == 137
    assert hashlib.sha256(transport.ARM_FRAME.encode("ascii")).hexdigest() \
        == transport.ARM_FRAME_SHA256
    assert transport.MARKER_NAMES == (
        "START", "SAFE", "BACKEND_READY", "POWER_READY", "CAPTURE_ARM",
        "TONE_BEGIN", "POSTROLL", "PASS", "HEARTBEAT",
    )
    assert "direct_amp_vdd5v_unswitched=1" in transport.FIXED_MARKER_LINES[-1]

    capture_source = (SCRIPTS / "d24_capture_transport.py").read_text()
    flash_source = (SCRIPTS / "flash-audio-level-diag.sh").read_text()
    assert "d23_capture_transport" not in capture_source
    assert "audio_direct_diag" not in capture_source
    assert capture_source.count("esp.run()") == 1
    assert capture_source.count("esp.hard_reset()") == 1
    assert capture_source.index("atomic_consume(receipt_path") \
        < capture_source.index("esp.run()")
    assert capture_source.count("open_serial_once(args.port)") == 1
    assert "subprocess" not in capture_source
    assert capture_source.index(
        'update_terminal_state(\n                        args.state, "completed"'
    ) < capture_source.rindex(
        "atomic_write_json(args.timing_json, timing, exclusive=False)"
    )
    assert "--after no_reset write_flash" in flash_source
    assert "esptool.py --chip esp32p4 --port \"$P4_PORT\" run" not in flash_source
    assert 'while [ "$P4_PREFLIGHT_RUN" -le 3 ]' in flash_source
    verifier_call = 'python3 "$P4_VERIFIER" "$P4_BUILD_DIR" app-flash'
    assert flash_source.count(verifier_call) == 3
    first_verifier = flash_source.index(verifier_call)
    second_verifier = flash_source.index(verifier_call, first_verifier + 1)
    third_verifier = flash_source.index(verifier_call, second_verifier + 1)
    assert first_verifier \
        < flash_source.index('chmod 400 "$P4_SNAPSHOT"') \
        < flash_source.index('"$P4_CAPTURE" reserve') \
        < second_verifier \
        < flash_source.index("--after no_reset write_flash") \
        < flash_source.index("p4_verify_chunked_application_readback") \
        < third_verifier \
        < flash_source.index('"$P4_CAPTURE" emit-receipt')
    assert flash_source.count(
        verifier_call + '\npython3 "$P4_CAPTURE" emit-receipt'
    ) == 1

    accepted = transport.validate_initial_prefix(b"ESP-ROM:esp32p4\nrst:0x3 boot\n")
    assert accepted["reset_count"] == 1
    must_fail(lambda: transport.validate_initial_prefix(b"rst:0x7 unsafe\n"),
              "unsafe reset reason was accepted")

    with tempfile.TemporaryDirectory(dir="/private/tmp") as directory:
        temp = pathlib.Path(directory)
        auth = temp / "auth.json"
        auth_value = {
            "id": transport.AUTH_ID,
            "exact_artifact": {
                "offset": "0x10000", "bytes": 196176,
                "sha256": "a" * 64,
            },
        }
        write_private(auth, auth_value)
        state = temp / "state.json"
        receipt = temp / "receipt.json"

        class Args:
            pass

        args = Args()
        args.state = state
        args.receipt = receipt
        args.authorization = auth
        args.device_identity_sha256 = "b" * 64
        args.offset = 0x10000
        args.bytes = 196176
        args.sha256 = "a" * 64
        transport.reserve(args)
        must_fail(lambda: transport.reserve(args), "duplicate reservation passed")
        ledger = json.loads(state.read_text())
        assert ledger["status"] == "preparing" and ledger["launch_count"] == 0
        transport.fail_reservation(state, auth, "host-test")
        assert json.loads(state.read_text())["status"] == "failed"
        must_fail(
            lambda: transport.validate_no_reset_after_attach(
                (transport.SERIAL_ATTACH_LINE + "\nrst:0x3 replay\n").encode()
            ),
            "post-attach reset/replay prefix was accepted",
        )
        transport.validate_no_reset_after_attach(
            (transport.SERIAL_ATTACH_LINE + "\n" + transport.WAIT_ARM_LINE + "\n").encode()
        )
        malformed = transport.parse_contract_lines(
            (transport.SERIAL_ATTACH_LINE + "\n"
             + transport.WAIT_ARM_LINE + " changed\n").encode()
        )
        assert malformed[-1]["contract_line"] != transport.WAIT_ARM_LINE

        # Full receipt validation rejects malformed marker/ARM identities,
        # expired receipts, and every attempt to reuse a consumed receipt.
        receipt_state = temp / "receipt-state.json"
        launch_receipt = temp / "launch.json"
        now = dt.datetime.now(dt.timezone.utc)
        auth_hash = hashlib.sha256(auth.read_bytes()).hexdigest()
        runtime_identity = {"host-test-runtime": True}
        transport._runtime_identity = lambda: runtime_identity  # type: ignore[attr-defined]
        expected_lines = [
            transport.SERIAL_ATTACH_LINE,
            transport.WAIT_ARM_LINE,
            "P4_AUDIO D2.4 ARM_ACCEPTED auth=" + transport.AUTH_ID
            + " nonce=" + transport.ARM_NONCE + " tx_count=1",
            *transport.FIXED_MARKER_LINES,
        ]
        receipt_value = {
            "schema": 1,
            "state": "ready",
            "nonce": transport.ARM_NONCE,
            "created_at_utc": transport.utc_text(now),
            "expires_at_utc": transport.utc_text(now + dt.timedelta(minutes=10)),
            "app": transport.APP,
            "port": "/dev/cu.host-test",
            "device_identity_sha256": "b" * 64,
            "offset": 0x10000,
            "bytes": 196176,
            "sha256": "a" * 64,
            "authorization_id": transport.AUTH_ID,
            "authorization_path": str(auth),
            "authorization_sha256": auth_hash,
            "verifier": transport.regular_sha_entry(SCRIPTS / "verify-audio-level-diag.py"),
            "capture_tool": transport.regular_sha_entry(SCRIPTS / "capture-audio-level-diag.py"),
            "capture_transport": transport.regular_sha_entry(SCRIPTS / "d24_capture_transport.py"),
            "analyzer": transport.regular_sha_entry(SCRIPTS / "analyze-audio-level-tone.py"),
            "esptool_version": transport.ESPTOOL_VERSION,
            "idf_commit": transport.IDF_COMMIT,
            "portaudio": {
                "path": str(transport.PORTAUDIO_PATH),
                "sha256": transport.PORTAUDIO_SHA256,
                "version": transport.PORTAUDIO_VERSION,
                "version_text": transport.PORTAUDIO_VERSION_TEXT,
            },
            "arm_nonce_derivation": "sha256-ascii-authorization-id-space-host-arm-space-v1",
            "arm_frame": {
                "bytes": transport.ARM_FRAME_BYTES,
                "sha256": transport.ARM_FRAME_SHA256,
                "line_ending": "LF-only",
            },
            "marker_lines": expected_lines,
            "readback": {
                "bytes": 196176,
                "sha256": "a" * 64,
                "chunks": 1,
                "max_chunk_bytes": 196176,
            },
            "launch_count": 0,
            "runtime": runtime_identity,
        }

        def install_receipt(value: dict, path: pathlib.Path, state_path: pathlib.Path) -> None:
            write_private(path, value)
            state_value = {
                "schema": 1,
                "authorization_id": transport.AUTH_ID,
                "authorization_sha256": auth_hash,
                "arm_nonce": transport.ARM_NONCE,
                "nonce_sha256": hashlib.sha256(transport.ARM_NONCE.encode()).hexdigest(),
                "device_identity_sha256": "b" * 64,
                "offset": 0x10000,
                "bytes": 196176,
                "sha256": "a" * 64,
                "status": "pending",
                "created_at_utc": transport.utc_text(now),
                "receipt_path": str(path),
                "receipt_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "launch_count": 0,
            }
            write_private(state_path, state_value)

        install_receipt(receipt_value, launch_receipt, receipt_state)
        transport.validate_receipt(launch_receipt, receipt_state, "/dev/cu.host-test")

        bad_markers = dict(receipt_value)
        bad_markers["marker_lines"] = [*expected_lines[:-1], expected_lines[-1] + " changed"]
        bad_path, bad_state = temp / "bad-markers.json", temp / "bad-markers-state.json"
        install_receipt(bad_markers, bad_path, bad_state)
        must_fail(lambda: transport.validate_receipt(bad_path, bad_state, "/dev/cu.host-test"),
                  "malformed exact marker receipt passed")

        bad_arm = dict(receipt_value)
        bad_arm["arm_frame"] = dict(receipt_value["arm_frame"])
        bad_arm["arm_frame"]["sha256"] = "0" * 64
        arm_path, arm_state = temp / "bad-arm.json", temp / "bad-arm-state.json"
        install_receipt(bad_arm, arm_path, arm_state)
        must_fail(lambda: transport.validate_receipt(arm_path, arm_state, "/dev/cu.host-test"),
                  "malformed ARM identity receipt passed")

        expired = dict(receipt_value)
        expired["created_at_utc"] = "2026-01-01T00:00:00Z"
        expired["expires_at_utc"] = "2026-01-01T00:10:00Z"
        expired_path, expired_state = temp / "expired.json", temp / "expired-state.json"
        install_receipt(expired, expired_path, expired_state)
        must_fail(lambda: transport.validate_receipt(
            expired_path, expired_state, "/dev/cu.host-test"
        ), "expired receipt passed")

        consumed = transport.atomic_consume(launch_receipt, receipt_state, receipt_value)
        assert consumed.is_file() and not launch_receipt.exists()
        assert json.loads(receipt_state.read_text())["launch_count"] == 1
        must_fail(lambda: transport.atomic_consume(
            launch_receipt, receipt_state, receipt_value
        ), "consumed receipt was reusable")

        # Ten seconds of silence with one clean, serial-correlated 440 Hz tone.
        sample_count = 48_000 * 10
        samples = [0] * sample_count
        tone_start = 48_000 * 3
        tone_frames = int(48_000 * 0.4)
        for index in range(tone_frames):
            envelope = min(1.0, index / 3840.0, (tone_frames - 1 - index) / 3840.0)
            samples[tone_start + index] = round(
                5000.0 * envelope * math.sin(2.0 * math.pi * 440.0 * index / 48_000)
            )
        wav = temp / "clean.wav"
        pcm = write_wav(wav, samples)
        timing = temp / "timing.json"
        acoustic_ledger = temp / "acoustic-ledger.json"
        persist_completed_timing(
            timing, analyzer_timing(wav, pcm, acoustic_ledger), acoustic_ledger
        )
        result_path = temp / "result.json"
        passed = run_analyzer(wav, timing, result_path)
        assert passed.returncode == 0, passed.stderr + passed.stdout
        result = json.loads(result_path.read_text())
        assert result["acceptance_classification"] \
            == "pass-serial-microphone-operator-pending"
        assert result["microphone_detection_result"] == "pass"
        assert result["full_acoustic_acceptance"] is False
        assert stat.S_IMODE(result_path.stat().st_mode) == 0o600

        # A copied WAV cannot be substituted for the timing-bound path.
        substitute = temp / "substitute.wav"
        substitute.write_bytes(wav.read_bytes())
        substituted = run_analyzer(substitute, timing, temp / "substitute.json")
        assert substituted.returncode != 0
        assert "WAV identity differs" in substituted.stderr

        # One saturated sample must defeat otherwise identical mic acceptance.
        clipped_samples = list(samples)
        clipped_samples[tone_start + (tone_frames // 2)] = 32767
        clipped_wav = temp / "clipped.wav"
        clipped_pcm = write_wav(clipped_wav, clipped_samples)
        clipped_timing = temp / "clipped-timing.json"
        clipped_ledger = temp / "clipped-ledger.json"
        persist_completed_timing(
            clipped_timing,
            analyzer_timing(clipped_wav, clipped_pcm, clipped_ledger),
            clipped_ledger,
        )
        clipped_result = temp / "clipped-result.json"
        clipped = run_analyzer(clipped_wav, clipped_timing, clipped_result)
        assert clipped.returncode != 0
        clipped_json = json.loads(clipped_result.read_text())
        assert clipped_json["capture_unclipped"] is False
        assert clipped_json["microphone_detection_result"] == "fail"

        # Analyzer evidence is exclusive/no-clobber.
        original = result_path.read_bytes()
        overwrite = run_analyzer(wav, timing, result_path)
        assert overwrite.returncode != 0
        assert result_path.read_bytes() == original

        # A heard tone with a pop is explicitly mixed, never full acceptance.
        pop_result = temp / "pop-result.json"
        marker_wall = json.loads(timing.read_text())["serial_event_wall_ns"]
        confirm = dt.datetime.fromtimestamp(
            marker_wall["HEARTBEAT"] / 1_000_000_000,
            tz=dt.timezone.utc,
        ).isoformat().replace("+00:00", "Z")
        pop = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "analyze-audio-level-tone.py"), str(wav),
                "--timing-json", str(timing), "--json", str(pop_result),
                "--operator-audibility", "heard", "--operator-pop", "observed",
                "--operator-confirmed-at-utc", confirm,
                "--operator-confirmed-by", "host-test-operator",
            ],
            text=True, capture_output=True, check=False,
        )
        assert pop.returncode == 0, pop.stderr + pop.stdout
        pop_json = json.loads(pop_result.read_text())
        assert pop_json["acceptance_classification"] \
            == "mixed-microphone-heard-pop-observed"
        assert pop_json["full_acoustic_acceptance"] is False

        # A capture that has not completed transport close/finalization or
        # whose terminal ledger hash differs is never analyzer-acceptable.
        unfinished_timing = temp / "unfinished-timing.json"
        unfinished_ledger = temp / "unfinished-ledger.json"
        unfinished_value = analyzer_timing(wav, pcm, unfinished_ledger)
        unfinished_value["result"] = "pending-finalization"
        unfinished_value["transport_finalized"] = False
        persist_completed_timing(unfinished_timing, unfinished_value, unfinished_ledger)
        unfinished = run_analyzer(wav, unfinished_timing, temp / "unfinished.json")
        assert unfinished.returncode != 0

        unclosed_timing = temp / "unclosed-timing.json"
        unclosed_ledger = temp / "unclosed-ledger.json"
        unclosed_value = analyzer_timing(wav, pcm, unclosed_ledger)
        unclosed_value["audio_close_monotonic_ns"] = None
        persist_completed_timing(unclosed_timing, unclosed_value, unclosed_ledger)
        unclosed = run_analyzer(wav, unclosed_timing, temp / "unclosed.json")
        assert unclosed.returncode != 0

        stale_ledger = json.loads(acoustic_ledger.read_text())
        stale_ledger["timing_sha256"] = "0" * 64
        write_private(acoustic_ledger, stale_ledger)
        stale = run_analyzer(wav, timing, temp / "stale-ledger.json")
        assert stale.returncode != 0

    print("D2.4 gate tests: PASS receipt_one_shot=1 same_handle=1 "
          "wav_bound=1 clipping_rejected=1 analyzer_no_clobber=1 direct_run=0")


if __name__ == "__main__":
    main()
