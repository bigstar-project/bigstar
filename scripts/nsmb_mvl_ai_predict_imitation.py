#!/usr/bin/env python3
"""Run offline inference for a minimal NSMB MvL imitation model."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Any

import numpy as np


def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(x, -40.0, 40.0)))


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str) and value:
        return int(float(value))
    return default


def buttons_text(held: int, buttons: list[str]) -> str:
    names = [name.upper() for bit, name in enumerate(buttons) if held & (1 << bit)]
    return "+".join(names) if names else "-"


def load_dataset(path: Path, feature_names: list[str]) -> tuple[list[dict[str, str]], np.ndarray]:
    with path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError(f"empty dataset: {path}")
    missing = [name for name in feature_names if name not in rows[0]]
    if missing:
        raise ValueError(f"dataset is missing model features: {', '.join(missing[:8])}")
    x = np.array([[float(row[name]) for name in feature_names] for row in rows], dtype=np.float32)
    return rows, x


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    model = np.load(args.model, allow_pickle=False)
    weights = model["weights"]
    bias = model["bias"]
    mean = model["mean"]
    scale = model["scale"]
    feature_names = [str(value) for value in model["feature_names"]]
    buttons = [str(value) for value in model["buttons"]]

    rows, x_raw = load_dataset(args.dataset, feature_names)
    if args.limit > 0:
        rows = rows[: args.limit]
        x_raw = x_raw[: args.limit]
    x = (x_raw - mean) / scale
    probs = sigmoid(x @ weights + bias)
    pred_bits = probs >= args.threshold

    output_rows: list[dict[str, str | int | float]] = []
    exact_matches = 0
    button_matches = 0
    button_total = 0
    has_labels = all(f"label_{button}" in rows[0] for button in buttons)
    for row, bits, prob in zip(rows, pred_bits, probs):
        pred_held = 0
        for bit, enabled in enumerate(bits):
            if enabled:
                pred_held |= 1 << bit
        label_held = num(row.get("label_held"))
        if has_labels:
            target_bits = np.array([num(row.get(f"label_{button}")) != 0 for button in buttons])
            exact_matches += int(np.all(bits == target_bits))
            button_matches += int(np.sum(bits == target_bits))
            button_total += len(buttons)
        output: dict[str, str | int | float] = {
            "frame": row.get("frame", "0"),
            "player": row.get("player", "0"),
            "label_held": label_held,
            "label_text": buttons_text(label_held, buttons),
            "pred_held": pred_held,
            "pred_text": buttons_text(pred_held, buttons),
        }
        for button, value in zip(buttons, prob):
            output[f"prob_{button}"] = f"{float(value):.6f}"
        output_rows.append(output)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(output_rows[0].keys()))
        writer.writeheader()
        writer.writerows(output_rows)

    summary = f"rows={len(output_rows)} output={args.output}"
    if has_labels and output_rows:
        summary += (
            f" button_acc={button_matches / max(1, button_total):.3f}"
            f" exact={exact_matches / len(output_rows):.3f}"
        )
    print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
