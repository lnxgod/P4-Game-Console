#!/usr/bin/env python3
"""Analyze a mono PCM16 WAV for the one bounded D2.3 440 Hz tone."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct
import wave


def rms(values: list[float]) -> float:
    if not values:
        return 0.0
    return math.sqrt(sum(value * value for value in values) / len(values))


def tone_amplitude(values: list[float], sample_rate: int, frequency: float) -> float:
    if not values:
        return 0.0
    omega = 2.0 * math.pi * frequency / sample_rate
    cosine = 0.0
    sine = 0.0
    for index, value in enumerate(values):
        angle = omega * index
        cosine += value * math.cos(angle)
        sine += value * math.sin(angle)
    return 2.0 * math.hypot(cosine, sine) / len(values)


def find_tone_events(
    samples: list[float],
    sample_rate: int,
    frequency: float,
    peak_amplitude: float,
    baseline_amplitude: float,
    min_snr_db: float,
) -> list[dict[str, float]]:
    """Return sustained, non-overlapping tone events from a short-time scan."""
    window_frames = max(1, int(round(sample_rate * 0.08)))
    hop_frames = max(1, int(round(sample_rate * 0.02)))
    threshold = max(
        peak_amplitude * 0.25,
        baseline_amplitude * (10.0 ** (min_snr_db / 20.0)),
        1.0,
    )
    active: list[tuple[int, float]] = []
    for start in range(0, len(samples) - window_frames + 1, hop_frames):
        amplitude = tone_amplitude(
            samples[start:start + window_frames], sample_rate, frequency
        )
        if amplitude >= threshold:
            active.append((start, amplitude))

    if not active:
        return []
    groups: list[list[tuple[int, float]]] = [[active[0]]]
    max_gap_frames = 3 * hop_frames
    for item in active[1:]:
        if item[0] - groups[-1][-1][0] <= max_gap_frames:
            groups[-1].append(item)
        else:
            groups.append([item])

    events: list[dict[str, float]] = []
    for group in groups:
        start = group[0][0]
        end = group[-1][0] + window_frames
        duration = (end - start) / sample_rate
        if duration < 0.12:
            continue
        events.append(
            {
                "start_seconds": start / sample_rate,
                "end_seconds": end / sample_rate,
                "duration_seconds": duration,
                "peak_amplitude_pcm": max(item[1] for item in group),
            }
        )
    return events


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--timing-json", type=pathlib.Path, required=True)
    args = parser.parse_args()
    expected_hz = 440.0
    min_snr_db = 12.0
    min_adjacent_db = 6.0

    timing = json.loads(args.timing_json.read_text())
    if timing.get("schema") != 1:
        raise SystemExit("unsupported capture timing schema")
    event_times = timing.get("serial_event_wall_ns")
    if not isinstance(event_times, dict):
        raise SystemExit("capture timing lacks serial events")
    required_markers = [
        "CAPTURE_ARM", "TONE_BEGIN", "POSTROLL", "PASS", "HEARTBEAT"
    ]
    try:
        marker_times = [int(event_times[name]) for name in required_markers]
        audio_first_sample_wall_ns = int(timing["audio_first_sample_wall_ns"])
    except (KeyError, TypeError, ValueError) as error:
        raise SystemExit("capture timing lacks required marker timestamps") from error
    if marker_times != sorted(marker_times) or len(set(marker_times)) != len(marker_times):
        raise SystemExit("serial markers are missing or out of order")

    raw = args.wav.read_bytes()
    with wave.open(str(args.wav), "rb") as recording:
        channels = recording.getnchannels()
        sample_width = recording.getsampwidth()
        sample_rate = recording.getframerate()
        frame_count = recording.getnframes()
        pcm = recording.readframes(frame_count)
    if channels != 1 or sample_width != 2:
        raise SystemExit("capture must be mono PCM16 WAV")
    if sample_rate != 48000 or frame_count < sample_rate:
        raise SystemExit("capture must be exact 48 kHz and at least one second")

    samples = [float(value[0]) for value in struct.iter_unpack("<h", pcm)]
    audio_end_wall_ns = audio_first_sample_wall_ns + round(
        len(samples) * 1_000_000_000 / sample_rate
    )
    if audio_first_sample_wall_ns > marker_times[0]:
        raise SystemExit("WAV did not start by CAPTURE_ARM")
    if audio_end_wall_ns < marker_times[-1]:
        raise SystemExit("WAV did not span through HEARTBEAT")
    window_frames = max(1, int(round(sample_rate * 0.4)))
    hop_frames = max(1, int(round(sample_rate * 0.01)))
    if len(samples) < window_frames:
        raise SystemExit("capture is shorter than the tone window")

    expected_start_seconds = (
        marker_times[1] - audio_first_sample_wall_ns
    ) / 1_000_000_000.0
    expected_start_frame = int(round(expected_start_seconds * sample_rate))
    search_slop_frames = int(round(sample_rate * 0.35))
    search_start = max(0, expected_start_frame - search_slop_frames)
    search_end = min(
        len(samples) - window_frames,
        expected_start_frame + search_slop_frames,
    )
    if search_end < search_start:
        raise SystemExit("serial-correlated tone window is outside the WAV")

    best_start = search_start
    best_amplitude = -1.0
    for start in range(search_start, search_end + 1, hop_frames):
        amplitude = tone_amplitude(
            samples[start:start + window_frames], sample_rate, expected_hz
        )
        if amplitude > best_amplitude:
            best_amplitude = amplitude
            best_start = start
    tone_window = samples[best_start:best_start + window_frames]

    frequency_candidates = [
        expected_hz - 8.0 + (index * 0.25) for index in range(65)
    ]
    amplitudes = {
        frequency: tone_amplitude(tone_window, sample_rate, frequency)
        for frequency in frequency_candidates
    }
    dominant_hz = max(amplitudes, key=amplitudes.get)
    dominant_amplitude = amplitudes[dominant_hz]

    noise_window_frames = int(round(sample_rate * 0.5))
    pre_end = expected_start_frame - int(round(sample_rate * 0.40))
    pre_start = pre_end - noise_window_frames
    post_start = expected_start_frame + int(round(sample_rate * 0.85))
    post_end = post_start + noise_window_frames
    if pre_start < 0 or post_end > len(samples):
        raise SystemExit("capture lacks guaranteed pre-tone or post-tone baseline")
    noise_samples = samples[pre_start:pre_end] + samples[post_start:post_end]
    noise_rms = rms(noise_samples)
    baseline_440_amplitude = max(
        tone_amplitude(samples[pre_start:pre_end], sample_rate, expected_hz),
        tone_amplitude(samples[post_start:post_end], sample_rate, expected_hz),
    )
    tone_rms = rms(tone_window)
    snr_db = 20.0 * math.log10(
        max(dominant_amplitude, 1e-12) / max(noise_rms, 1e-12)
    )
    events = find_tone_events(
        samples,
        sample_rate,
        expected_hz,
        dominant_amplitude,
        baseline_440_amplitude,
        min_snr_db,
    )
    adjacent_candidates = [
        frequency for frequency in range(400, 481, 5)
        if abs(frequency - expected_hz) > 5.0
    ]
    adjacent_amplitude = max(
        tone_amplitude(tone_window, sample_rate, float(frequency))
        for frequency in adjacent_candidates
    )
    tone_over_baseline_db = 20.0 * math.log10(
        max(dominant_amplitude, 1e-12)
        / max(baseline_440_amplitude, 1e-12)
    )
    tone_over_adjacent_db = 20.0 * math.log10(
        max(dominant_amplitude, 1e-12) / max(adjacent_amplitude, 1e-12)
    )
    correlated_event = (
        events[0] if len(events) == 1
        and abs(events[0]["start_seconds"] - expected_start_seconds) <= 0.5
        else None
    )
    event_duration_in_range = (
        correlated_event is not None
        and 0.30 <= correlated_event["duration_seconds"] <= 0.65
    )

    result = {
        "schema": 1,
        "wav": str(args.wav),
        "wav_bytes": len(raw),
        "wav_sha256": hashlib.sha256(raw).hexdigest(),
        "sample_rate_hz": sample_rate,
        "channels": channels,
        "sample_width_bytes": sample_width,
        "capture_seconds": len(samples) / sample_rate,
        "timing_json": str(args.timing_json),
        "timing_json_sha256": hashlib.sha256(
            args.timing_json.read_bytes()
        ).hexdigest(),
        "serial_markers_ordered": True,
        "wav_spans_capture_arm_through_heartbeat": True,
        "serial_correlated_tone_start_seconds": expected_start_seconds,
        "tone_window_start_seconds": best_start / sample_rate,
        "tone_window_seconds": window_frames / sample_rate,
        "expected_hz": expected_hz,
        "dominant_hz": dominant_hz,
        "dominant_amplitude_pcm": dominant_amplitude,
        "tone_rms_pcm": tone_rms,
        "noise_rms_pcm": noise_rms,
        "baseline_440_amplitude_pcm": baseline_440_amplitude,
        "adjacent_band_max_amplitude_pcm": adjacent_amplitude,
        "dominant_over_noise_db": snr_db,
        "dominant_over_baseline_440_db": tone_over_baseline_db,
        "dominant_over_adjacent_band_db": tone_over_adjacent_db,
        "tone_event_count": len(events),
        "tone_events": events,
        "exactly_one_tone_event": len(events) == 1,
        "tone_event_correlated_to_serial": correlated_event is not None,
        "tone_event_duration_0_30_to_0_65_seconds": event_duration_in_range,
        "frequency_within_5_hz": abs(dominant_hz - expected_hz) <= 5.0,
        "snr_at_least_12_db": snr_db >= min_snr_db,
        "baseline_440_margin_at_least_12_db": (
            tone_over_baseline_db >= min_snr_db
        ),
        "adjacent_band_margin_at_least_6_db": (
            tone_over_adjacent_db >= min_adjacent_db
        ),
    }
    result["result"] = (
        "pass" if result["frequency_within_5_hz"]
        and result["snr_at_least_12_db"]
        and result["baseline_440_margin_at_least_12_db"]
        and result["adjacent_band_margin_at_least_6_db"]
        and result["exactly_one_tone_event"]
        and result["tone_event_correlated_to_serial"]
        and result["tone_event_duration_0_30_to_0_65_seconds"] else "fail"
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json is not None:
        args.json.write_text(rendered)
    print(rendered, end="")
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
