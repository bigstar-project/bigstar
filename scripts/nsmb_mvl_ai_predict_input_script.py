#!/usr/bin/env python3
"""Generate a melonDS input script from an NSMB MvL imitation model."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from nsmb_mvl_ai_build_dataset import build_row, input_playlog_paths, iter_records, label_held, num


@dataclass(frozen=True)
class PredictedInput:
    frame: int
    held: int
    probabilities: list[float]


@dataclass(frozen=True)
class InputSpan:
    start: int
    end: int
    held: int


BUTTON_FALLBACK = [
    "a",
    "b",
    "select",
    "start",
    "right",
    "left",
    "up",
    "down",
    "r",
    "l",
    "x",
    "y",
]


def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(x, -40.0, 40.0)))


def input_text(held: int, buttons: list[str]) -> str:
    names = [name.upper() for bit, name in enumerate(buttons) if held & (1 << bit)]
    return "+".join(names) if names else "NONE"


def target_prefix(target: str) -> str:
    if target == "none":
        return ""
    if target == "all":
        return "ALL "
    if target.startswith("inst"):
        return f"{target} "
    raise ValueError(f"unsupported target: {target}")


def load_model(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, list[str], list[str]]:
    model = np.load(path, allow_pickle=False)
    weights = model["weights"]
    bias = model["bias"]
    mean = model["mean"]
    scale = model["scale"]
    feature_names = [str(value) for value in model["feature_names"]]
    buttons = [str(value) for value in model["buttons"]] if "buttons" in model.files else BUTTON_FALLBACK
    return weights, bias, mean, scale, feature_names, buttons


def row_features(row: dict[str, int], feature_names: list[str]) -> np.ndarray:
    missing = [name for name in feature_names if name not in row]
    if missing:
        raise ValueError(f"play log row is missing model features: {', '.join(missing[:8])}")
    return np.array([float(row[name]) for name in feature_names], dtype=np.float32)


def iter_feature_rows(
    input_path: Path,
    player: int,
    label_source: str,
    min_frame: int,
    max_frame: int,
    require_player_found: bool,
) -> Any:
    for recording_index, playlog_path in enumerate(input_playlog_paths(input_path)):
        recording_frame_index = 0
        for record in iter_records(playlog_path):
            frame = num(record.get("frame"))
            if frame < min_frame:
                continue
            if max_frame > 0 and frame > max_frame:
                continue
            players = record.get("players") or []
            if require_player_found and (len(players) <= player or not players[player].get("found")):
                continue
            if label_held(record, player, label_source) is None:
                continue
            record["_recording_index"] = recording_index
            record["_recording_frame_index"] = recording_frame_index
            yield build_row(record, player, label_source)
            recording_frame_index += 1


def predict_inputs(
    rows: list[dict[str, int]],
    weights: np.ndarray,
    bias: np.ndarray,
    mean: np.ndarray,
    scale: np.ndarray,
    feature_names: list[str],
    threshold: float,
    reaction_delay_frames: int,
    mistake_rate: float,
    mistake_mode: str,
    seed: int,
) -> list[PredictedInput]:
    if not rows:
        return []

    x_raw = np.stack([row_features(row, feature_names) for row in rows])
    x = (x_raw - mean) / scale
    probs = sigmoid(x @ weights + bias)
    bits = probs >= threshold

    rng = np.random.default_rng(seed)
    result: list[PredictedInput] = []
    previous_held = 0
    for row, bit_values, prob in zip(rows, bits, probs):
        held = 0
        for bit, enabled in enumerate(bit_values):
            if enabled:
                held |= 1 << bit
        if mistake_rate > 0.0 and rng.random() < mistake_rate:
            if mistake_mode == "neutral":
                held = 0
            elif mistake_mode == "previous":
                held = previous_held
            elif mistake_mode == "drop-buttons":
                for bit in range(len(bit_values)):
                    if held & (1 << bit) and rng.random() < 0.5:
                        held &= ~(1 << bit)
        frame = num(row.get("frame")) + reaction_delay_frames
        result.append(PredictedInput(frame=frame, held=held, probabilities=[float(value) for value in prob]))
        previous_held = held
    return result


def compress_predictions(predictions: list[PredictedInput], end_frame: int, max_gap_fill: int) -> list[InputSpan]:
    if not predictions:
        return []

    spans: list[InputSpan] = []
    for index, prediction in enumerate(predictions):
        if index + 1 < len(predictions):
            next_frame = predictions[index + 1].frame
            gap = next_frame - prediction.frame
            if max_gap_fill > 0 and gap > max_gap_fill:
                span_end = prediction.frame
            else:
                span_end = max(prediction.frame, next_frame - 1)
        elif end_frame > 0:
            span_end = max(prediction.frame, end_frame)
        else:
            span_end = prediction.frame

        if spans and spans[-1].held == prediction.held and spans[-1].end + 1 >= prediction.frame:
            spans[-1] = InputSpan(spans[-1].start, span_end, prediction.held)
        else:
            spans.append(InputSpan(prediction.frame, span_end, prediction.held))
    return spans


RANGE_RE = re.compile(r"(?P<start>0x[0-9a-fA-F]+|\d+)-(?P<end>0x[0-9a-fA-F]+|\d+)")


def prefix_lines(path: Path, before_frame: int) -> list[str]:
    if before_frame <= 0:
        return []
    lines: list[str] = []
    with path.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.rstrip("\r\n")
            stripped = line.split("#", 1)[0].strip()
            if not stripped:
                lines.append(line)
                continue
            match = RANGE_RE.search(stripped)
            if not match:
                lines.append(line)
                continue
            start = int(match.group("start"), 0)
            end = int(match.group("end"), 0)
            if start >= before_frame:
                continue
            if end >= before_frame:
                line = line.replace(f"{match.group('start')}-{match.group('end')}", f"{match.group('start')}-{before_frame - 1}", 1)
            lines.append(line)
    return lines


def write_input_script(
    output: Path,
    spans: list[InputSpan],
    buttons: list[str],
    target: str,
    prefix_script: Path | None,
    metadata: dict[str, str | int | float],
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    first_frame = spans[0].start if spans else 0
    prefix = target_prefix(target)
    lines: list[str] = [
        "# NSMB MvL imitation-model input script.",
        "# Generated by scripts/nsmb_mvl_ai_predict_input_script.py.",
    ]
    for key, value in metadata.items():
        lines.append(f"# {key}: {value}")
    lines.append("")
    if prefix_script is not None:
        copied = prefix_lines(prefix_script, first_frame)
        if copied:
            lines.extend(copied)
            lines.append("")
    for span in spans:
        lines.append(f"{prefix}{span.start}-{span.end} {input_text(span.held, buttons)}")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_predictions_csv(output: Path, predictions: list[PredictedInput], buttons: list[str]) -> None:
    import csv

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as f:
        fieldnames = ["frame", "pred_held", "pred_text"] + [f"prob_{button}" for button in buttons]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for prediction in predictions:
            row: dict[str, str | int] = {
                "frame": prediction.frame,
                "pred_held": prediction.held,
                "pred_text": input_text(prediction.held, buttons),
            }
            for button, probability in zip(buttons, prediction.probabilities):
                row[f"prob_{button}"] = f"{probability:.6f}"
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("input", type=Path, help="AI play log JSONL, recording manifest JSON, or recordings-index JSON")
    parser.add_argument("output", type=Path, help="output melonDS input script")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--label-source", choices=["auto", "applied", "player", "console"], default="auto")
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--min-frame", type=int, default=0)
    parser.add_argument("--max-frame", type=int, default=0)
    parser.add_argument("--end-frame", type=int, default=0)
    parser.add_argument("--target", default="", help="input target: inst0, inst1, all, or none. Default is inst<player>.")
    parser.add_argument("--prefix-script", type=Path, help="copy existing bootstrap spans before the first predicted frame")
    parser.add_argument("--predictions-csv", type=Path)
    parser.add_argument("--reaction-delay-frames", type=int, default=0)
    parser.add_argument("--max-gap-fill", type=int, default=0, help="if >0, do not hold predictions across larger frame gaps")
    parser.add_argument("--require-player-found", action="store_true")
    parser.add_argument("--mistake-rate", type=float, default=0.0)
    parser.add_argument("--mistake-mode", choices=["neutral", "previous", "drop-buttons"], default="neutral")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    target = args.target or f"inst{args.player}"
    if args.reaction_delay_frames < 0:
        raise ValueError("--reaction-delay-frames must be >= 0")
    if not 0.0 <= args.mistake_rate <= 1.0:
        raise ValueError("--mistake-rate must be in [0, 1]")

    weights, bias, mean, scale, feature_names, buttons = load_model(args.model)
    rows = list(
        iter_feature_rows(
            args.input,
            args.player,
            args.label_source,
            args.min_frame,
            args.max_frame,
            args.require_player_found,
        )
    )
    predictions = predict_inputs(
        rows,
        weights,
        bias,
        mean,
        scale,
        feature_names,
        args.threshold,
        args.reaction_delay_frames,
        args.mistake_rate,
        args.mistake_mode,
        args.seed,
    )
    spans = compress_predictions(predictions, args.end_frame, args.max_gap_fill)
    write_input_script(
        args.output,
        spans,
        buttons,
        target,
        args.prefix_script,
        {
            "model": str(args.model),
            "input": str(args.input),
            "player": args.player,
            "threshold": args.threshold,
            "reactionDelayFrames": args.reaction_delay_frames,
            "mistakeRate": args.mistake_rate,
            "spans": len(spans),
        },
    )
    if args.predictions_csv is not None:
        write_predictions_csv(args.predictions_csv, predictions, buttons)
    print(f"rows={len(rows)} predictions={len(predictions)} spans={len(spans)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
