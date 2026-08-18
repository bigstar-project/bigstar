#!/usr/bin/env python3
"""Compare final SDL audio output from two deterministic NSMB MvL runs.

The raw PCM stream is paired with callback metadata written by melonDS.  The
comparison deliberately aligns a short wall-clock offset before scoring: SDL
may request the same samples on a slightly different callback boundary even
when the emulated audio is identical.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np


SAMPLE_RATE = 48_000
CHANNELS = 2
SAMPLE_WIDTH = 2


@dataclass(frozen=True)
class CaptureStats:
    callbacks: int
    samples: int
    duration_seconds: float
    muted_callbacks: int
    underrun_callbacks: int
    underrun_samples: int


@dataclass(frozen=True)
class Comparison:
    role: str
    reference: CaptureStats
    candidate: CaptureStats
    alignment_ms: float
    compared_envelope_bins: int
    envelope_correlation: float
    envelope_nrmse: float
    reference_transients: int
    candidate_transients: int
    matched_transients: int
    missing_transients: int
    extra_transients: int
    missing_transient_frames: tuple[int, ...]
    extra_transient_frames: tuple[int, ...]
    correction_frames: tuple[int, ...]
    extra_transients_near_corrections: int
    extra_transient_correction_deltas: tuple[int, ...]
    spu_event_capture_available: bool
    reference_spu_starts: int
    candidate_spu_starts: int
    missing_spu_starts: int
    extra_spu_starts: int
    extra_spu_source_counts: tuple[str, ...]
    candidate_spu_starts_during_rollback: int
    candidate_spu_rollback_source_counts: tuple[str, ...]
    pass_thresholds: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--roles", nargs="+", choices=("host", "client"), default=("host", "client"))
    parser.add_argument("--start-frame", type=int, default=900)
    parser.add_argument("--end-frame", type=int, default=0)
    parser.add_argument("--bin-ms", type=float, default=20.0)
    parser.add_argument("--max-alignment-ms", type=float, default=500.0)
    parser.add_argument("--transient-tolerance-ms", type=float, default=80.0)
    parser.add_argument("--min-envelope-correlation", type=float, default=0.94)
    parser.add_argument("--max-envelope-nrmse", type=float, default=0.25)
    parser.add_argument("--max-unmatched-transients", type=int, default=12)
    parser.add_argument("--correction-window-frames", type=int, default=4)
    parser.add_argument("--ignore-spu-events", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-fail", action="store_true")
    return parser.parse_args()


def read_capture(
    root: Path, role: str, start_frame: int, end_frame: int
) -> tuple[np.ndarray, np.ndarray, CaptureStats]:
    pcm_path = root / role / "audio-output.pcm"
    callback_path = root / role / "audio-output-callbacks.csv"
    if not pcm_path.is_file():
        raise FileNotFoundError(f"PCM capture was not found: {pcm_path}")
    if not callback_path.is_file():
        raise FileNotFoundError(f"callback metadata was not found: {callback_path}")

    pcm = np.fromfile(pcm_path, dtype="<i2")
    if pcm.size % CHANNELS:
        raise ValueError(f"PCM capture has a partial stereo sample: {pcm_path}")
    pcm = pcm.reshape((-1, CHANNELS))

    chunks: list[np.ndarray] = []
    frame_chunks: list[np.ndarray] = []
    callbacks = 0
    muted_callbacks = 0
    underrun_callbacks = 0
    underrun_samples = 0
    with callback_path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            frame = int(row["frame"])
            if frame < start_frame or (end_frame > 0 and frame > end_frame):
                continue
            offset = int(row["sampleOffset"])
            length = int(row["outputSamples"])
            end = offset + length
            if offset < 0 or length < 0 or end > pcm.shape[0]:
                raise ValueError(
                    f"callback sample range [{offset}, {end}) exceeds {pcm.shape[0]} samples in {pcm_path}"
                )
            chunks.append(pcm[offset:end])
            frame_chunks.append(np.full(length, frame, dtype=np.int64))
            callbacks += 1
            muted_callbacks += int(row["muted"] != "0")
            missing = int(row["underrunSamples"])
            underrun_callbacks += int(missing > 0)
            underrun_samples += missing

    if not chunks:
        raise ValueError(
            f"no callbacks were captured for {role} in frame range {start_frame}..{end_frame or 'end'}"
        )
    selected = np.concatenate(chunks, axis=0)
    selected_frames = np.concatenate(frame_chunks)
    stats = CaptureStats(
        callbacks=callbacks,
        samples=int(selected.shape[0]),
        duration_seconds=float(selected.shape[0] / SAMPLE_RATE),
        muted_callbacks=muted_callbacks,
        underrun_callbacks=underrun_callbacks,
        underrun_samples=underrun_samples,
    )
    return selected, selected_frames, stats


def rms_envelope(stereo: np.ndarray, bin_samples: int) -> np.ndarray:
    usable = stereo.shape[0] - (stereo.shape[0] % bin_samples)
    if usable < bin_samples:
        raise ValueError("audio interval is shorter than one analysis bin")
    values = stereo[:usable].astype(np.float64) / 32768.0
    values = values.reshape((-1, bin_samples, CHANNELS))
    return np.sqrt(np.mean(values * values, axis=(1, 2)))


def envelope_frame_labels(sample_frames: np.ndarray, bin_samples: int) -> np.ndarray:
    usable = sample_frames.size - (sample_frames.size % bin_samples)
    if usable < bin_samples:
        raise ValueError("audio interval is shorter than one analysis bin")
    center = bin_samples // 2
    return sample_frames[center:usable:bin_samples]


def correction_frames(root: Path, role: str) -> tuple[int, ...]:
    path = root / role / f"{role}.stdout.txt"
    if not path.is_file():
        return ()
    pattern = re.compile(r"completed ROM-loop correction frame=(\d+)")
    frames: set[int] = set()
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = pattern.search(line)
            if match:
                frames.add(int(match.group(1)))
    return tuple(sorted(frames))


def read_spu_events(
    root: Path, role: str, start_frame: int, end_frame: int
) -> list[tuple[int, tuple[str, ...], bool]] | None:
    path = root / role / "spu-channel-starts.csv"
    if not path.is_file():
        return None
    events: list[tuple[int, tuple[str, ...], bool]] = []
    # Channel allocation, volume/pan and timer can change while referring to
    # the same sample.  Source, loop and length identify the sound payload.
    signature_fields = ("srcAddr", "loopPos", "length")
    with path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            frame = int(row["frame"])
            if frame < start_frame or (end_frame > 0 and frame > end_frame):
                continue
            events.append(
                (
                    frame,
                    tuple(row[field] for field in signature_fields),
                    row.get("rollbackTransaction", "0") != "0",
                )
            )
    return events


def match_spu_events(
    reference: list[tuple[int, tuple[str, ...], bool]],
    candidate: list[tuple[int, tuple[str, ...], bool]],
) -> tuple[int, int, tuple[str, ...]]:
    reference_counts = Counter(signature for _, signature, _ in reference)
    candidate_counts = Counter(signature for _, signature, _ in candidate)
    missing = sum((reference_counts - candidate_counts).values())
    extra = sum((candidate_counts - reference_counts).values())
    extra_source_counts = tuple(
        f"src={signature[0]} loop={signature[1]} length={signature[2]} count={count}"
        for signature, count in sorted((candidate_counts - reference_counts).items())
    )
    return missing, extra, extra_source_counts


def normalized_correlation(left: np.ndarray, right: np.ndarray) -> float:
    if left.size != right.size or left.size == 0:
        return -1.0
    left_centered = left - np.mean(left)
    right_centered = right - np.mean(right)
    denominator = math.sqrt(float(np.dot(left_centered, left_centered) * np.dot(right_centered, right_centered)))
    if denominator <= 1e-15:
        return 1.0 if np.allclose(left, right, atol=1e-12) else 0.0
    return float(np.dot(left_centered, right_centered) / denominator)


def align_envelopes(reference: np.ndarray, candidate: np.ndarray, max_lag_bins: int) -> tuple[int, np.ndarray, np.ndarray]:
    best_lag = 0
    best_score = -2.0
    best_pair: tuple[np.ndarray, np.ndarray] | None = None
    for lag in range(-max_lag_bins, max_lag_bins + 1):
        if lag >= 0:
            length = min(reference.size, candidate.size - lag)
            ref_part = reference[:length]
            cand_part = candidate[lag : lag + length]
        else:
            length = min(reference.size + lag, candidate.size)
            ref_part = reference[-lag : -lag + length]
            cand_part = candidate[:length]
        if length < 20:
            continue
        score = normalized_correlation(ref_part, cand_part)
        if score > best_score:
            best_score = score
            best_lag = lag
            best_pair = (ref_part, cand_part)
    if best_pair is None:
        raise ValueError("audio intervals are too short to align")
    return best_lag, best_pair[0], best_pair[1]


def transient_indices(envelope: np.ndarray, min_distance_bins: int) -> np.ndarray:
    if envelope.size < 3:
        return np.empty(0, dtype=np.int64)
    rise = np.maximum(np.diff(envelope, prepend=envelope[0]), 0.0)
    median = float(np.median(rise))
    mad = float(np.median(np.abs(rise - median)))
    threshold = max(median + 8.0 * mad, float(np.max(envelope)) * 0.015, 1e-5)
    candidates = np.flatnonzero(
        (rise >= threshold)
        & (rise >= np.roll(rise, 1))
        & (rise >= np.roll(rise, -1))
    )
    candidates = candidates[(candidates > 0) & (candidates < envelope.size - 1)]
    if candidates.size == 0:
        return candidates.astype(np.int64)

    selected: list[int] = []
    for index in candidates[np.argsort(rise[candidates])[::-1]]:
        if all(abs(int(index) - previous) >= min_distance_bins for previous in selected):
            selected.append(int(index))
    return np.asarray(sorted(selected), dtype=np.int64)


def match_transients(
    reference: np.ndarray, candidate: np.ndarray, tolerance_bins: int
) -> tuple[int, np.ndarray, np.ndarray]:
    used: set[int] = set()
    matched_reference: set[int] = set()
    for ref_position, ref_index in enumerate(reference):
        available = [
            (abs(int(cand_index) - int(ref_index)), position)
            for position, cand_index in enumerate(candidate)
            if position not in used and abs(int(cand_index) - int(ref_index)) <= tolerance_bins
        ]
        if not available:
            continue
        _, position = min(available)
        used.add(position)
        matched_reference.add(ref_position)
    missing = np.asarray(
        [value for position, value in enumerate(reference) if position not in matched_reference],
        dtype=np.int64,
    )
    extra = np.asarray(
        [value for position, value in enumerate(candidate) if position not in used],
        dtype=np.int64,
    )
    return len(used), missing, extra


def compare_role(args: argparse.Namespace, role: str) -> Comparison:
    reference, reference_frames, reference_stats = read_capture(
        args.reference, role, args.start_frame, args.end_frame
    )
    candidate, candidate_frames, candidate_stats = read_capture(
        args.candidate, role, args.start_frame, args.end_frame
    )
    bin_samples = max(1, round(SAMPLE_RATE * args.bin_ms / 1000.0))
    reference_envelope = rms_envelope(reference, bin_samples)
    candidate_envelope = rms_envelope(candidate, bin_samples)
    reference_bin_frames = envelope_frame_labels(reference_frames, bin_samples)
    candidate_bin_frames = envelope_frame_labels(candidate_frames, bin_samples)
    max_lag_bins = max(0, round(args.max_alignment_ms / args.bin_ms))
    lag, aligned_reference, aligned_candidate = align_envelopes(
        reference_envelope, candidate_envelope, max_lag_bins
    )
    correlation = normalized_correlation(aligned_reference, aligned_candidate)
    reference_rms = math.sqrt(float(np.mean(aligned_reference * aligned_reference)))
    nrmse = math.sqrt(float(np.mean((aligned_reference - aligned_candidate) ** 2))) / max(reference_rms, 1e-12)

    minimum_peak_distance = max(1, round(20.0 / args.bin_ms))
    reference_peaks = transient_indices(aligned_reference, minimum_peak_distance)
    candidate_peaks = transient_indices(aligned_candidate, minimum_peak_distance)
    tolerance_bins = max(0, round(args.transient_tolerance_ms / args.bin_ms))
    matched, missing_peaks, extra_peaks = match_transients(
        reference_peaks, candidate_peaks, tolerance_bins
    )
    reference_offset = -lag if lag < 0 else 0
    candidate_offset = lag if lag > 0 else 0
    missing_frames = tuple(
        int(reference_bin_frames[reference_offset + index]) for index in missing_peaks
    )
    extra_frames = tuple(
        int(candidate_bin_frames[candidate_offset + index]) for index in extra_peaks
    )
    corrections = correction_frames(args.candidate, role)
    correction_deltas = tuple(
        min((frame - correction for correction in corrections), key=abs)
        for frame in extra_frames
        if corrections
        and min(abs(frame - correction) for correction in corrections)
        <= args.correction_window_frames
    )
    missing = len(missing_frames)
    extra = len(extra_frames)
    reference_spu_events = read_spu_events(
        args.reference, role, args.start_frame, args.end_frame
    )
    candidate_spu_events = read_spu_events(
        args.candidate, role, args.start_frame, args.end_frame
    )
    spu_available = reference_spu_events is not None and candidate_spu_events is not None
    missing_spu_starts = 0
    extra_spu_starts = 0
    extra_spu_source_counts: tuple[str, ...] = ()
    if spu_available:
        missing_spu_starts, extra_spu_starts, extra_spu_source_counts = (
            match_spu_events(reference_spu_events, candidate_spu_events)
        )
    rollback_spu_counts = Counter(
        signature
        for _, signature, rollback in (candidate_spu_events or ())
        if rollback
    )
    rollback_spu_source_counts = tuple(
        f"src={signature[0]} loop={signature[1]} length={signature[2]} count={count}"
        for signature, count in sorted(rollback_spu_counts.items())
    )
    passed = (
        correlation >= args.min_envelope_correlation
        and nrmse <= args.max_envelope_nrmse
        and missing <= args.max_unmatched_transients
        and extra <= args.max_unmatched_transients
        and candidate_stats.underrun_samples == 0
        and (
            args.ignore_spu_events
            or not spu_available
            or (missing_spu_starts == 0 and extra_spu_starts == 0)
        )
    )
    return Comparison(
        role=role,
        reference=reference_stats,
        candidate=candidate_stats,
        alignment_ms=float(lag * args.bin_ms),
        compared_envelope_bins=int(aligned_reference.size),
        envelope_correlation=correlation,
        envelope_nrmse=nrmse,
        reference_transients=int(reference_peaks.size),
        candidate_transients=int(candidate_peaks.size),
        matched_transients=matched,
        missing_transients=missing,
        extra_transients=extra,
        missing_transient_frames=missing_frames,
        extra_transient_frames=extra_frames,
        correction_frames=corrections,
        extra_transients_near_corrections=len(correction_deltas),
        extra_transient_correction_deltas=correction_deltas,
        spu_event_capture_available=spu_available,
        reference_spu_starts=len(reference_spu_events or ()),
        candidate_spu_starts=len(candidate_spu_events or ()),
        missing_spu_starts=missing_spu_starts,
        extra_spu_starts=extra_spu_starts,
        extra_spu_source_counts=extra_spu_source_counts,
        candidate_spu_starts_during_rollback=sum(rollback_spu_counts.values()),
        candidate_spu_rollback_source_counts=rollback_spu_source_counts,
        pass_thresholds=passed,
    )


def main() -> int:
    args = parse_args()
    if args.start_frame < 0 or args.end_frame < 0 or (args.end_frame and args.end_frame < args.start_frame):
        raise ValueError("invalid frame range")
    if (
        args.bin_ms <= 0
        or args.max_alignment_ms < 0
        or args.transient_tolerance_ms < 0
        or args.correction_window_frames < 0
    ):
        raise ValueError("analysis intervals must be non-negative and bin-ms must be positive")

    results = [compare_role(args, role) for role in args.roles]
    payload = {
        "reference": str(args.reference.resolve()),
        "candidate": str(args.candidate.resolve()),
        "frameRange": {"start": args.start_frame, "end": args.end_frame},
        "analysis": {
            "sampleRate": SAMPLE_RATE,
            "binMs": args.bin_ms,
            "maxAlignmentMs": args.max_alignment_ms,
            "transientToleranceMs": args.transient_tolerance_ms,
            "correctionWindowFrames": args.correction_window_frames,
            "ignoreSpuEvents": args.ignore_spu_events,
            "thresholds": {
                "minEnvelopeCorrelation": args.min_envelope_correlation,
                "maxEnvelopeNrmse": args.max_envelope_nrmse,
                "maxUnmatchedTransients": args.max_unmatched_transients,
                "candidateUnderrunSamples": 0,
            },
        },
        "roles": [asdict(result) for result in results],
        "pass": all(result.pass_thresholds for result in results),
    }
    output_path = args.output or args.candidate / "audio-pcm-comparison.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    for result in results:
        print(
            f"{result.role}: correlation={result.envelope_correlation:.6f} "
            f"nrmse={result.envelope_nrmse:.6f} alignment={result.alignment_ms:+.1f}ms "
            f"transients={result.reference_transients}/{result.candidate_transients} "
            f"matched={result.matched_transients} missing={result.missing_transients} "
            f"extra={result.extra_transients} underrun={result.candidate.underrun_samples} "
            f"extraNearCorrection={result.extra_transients_near_corrections} "
            f"spuStarts={result.reference_spu_starts}/{result.candidate_spu_starts} "
            f"spuMissing={result.missing_spu_starts} spuExtra={result.extra_spu_starts} "
            f"spuDuringRollback={result.candidate_spu_starts_during_rollback} "
            f"pass={result.pass_thresholds}"
        )
    print(f"audio PCM comparison written: {output_path}")
    return 0 if payload["pass"] or args.no_fail else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError, KeyError) as error:
        print(f"audio PCM comparison failed: {error}", file=sys.stderr)
        raise SystemExit(2)
