#!/usr/bin/env python3
"""Train a minimal multi-label imitation model from an NSMB MvL AI dataset CSV."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np


BUTTONS = [
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


def load_csv(path: Path) -> tuple[np.ndarray, np.ndarray, list[str], np.ndarray]:
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if not rows:
        raise ValueError(f"empty dataset: {path}")

    label_names = {f"label_{name}" for name in BUTTONS}
    ignored = label_names | {"label_held", "recording_index", "recording_frame_index"}
    feature_names = [name for name in rows[0].keys() if name not in ignored]

    x = np.array([[float(row[name]) for name in feature_names] for row in rows], dtype=np.float32)
    y = np.array([[float(row[f"label_{name}"]) for name in BUTTONS] for row in rows], dtype=np.float32)
    recording_ids = np.array([int(float(row.get("recording_index", 0) or 0)) for row in rows])
    return x, y, feature_names, recording_ids


def split_indices(count: int, validation_fraction: float, seed: int) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    indices = np.arange(count)
    rng.shuffle(indices)
    val_count = int(round(count * validation_fraction))
    if count > 1:
        val_count = min(max(val_count, 1), count - 1)
    else:
        val_count = 0
    return indices[val_count:], indices[:val_count]


def split_indices_by_recording(
    recording_ids: np.ndarray,
    validation_fraction: float,
    seed: int,
) -> tuple[np.ndarray, np.ndarray]:
    unique = np.unique(recording_ids)
    if len(unique) <= 1:
        return split_indices(len(recording_ids), validation_fraction, seed)
    rng = np.random.default_rng(seed)
    shuffled = unique.copy()
    rng.shuffle(shuffled)
    val_count = int(round(len(shuffled) * validation_fraction))
    val_count = min(max(val_count, 1), len(shuffled) - 1)
    val_recordings = set(int(value) for value in shuffled[:val_count])
    val_mask = np.array([int(value) in val_recordings for value in recording_ids])
    val_idx = np.nonzero(val_mask)[0]
    train_idx = np.nonzero(~val_mask)[0]
    return train_idx, val_idx


def metrics(x: np.ndarray, y: np.ndarray, w: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    if len(x) == 0:
        return 0.0, 0.0
    pred = sigmoid(x @ w + b) >= 0.5
    target = y >= 0.5
    per_button = float(np.mean(pred == target))
    exact = float(np.mean(np.all(pred == target, axis=1)))
    return per_button, exact


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("model", type=Path)
    parser.add_argument("--epochs", type=int, default=500)
    parser.add_argument("--lr", type=float, default=0.05)
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument(
        "--split-by-recording",
        action="store_true",
        help="hold out whole recording_index groups instead of shuffled rows",
    )
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    x_raw, y, feature_names, recording_ids = load_csv(args.dataset)
    if args.split_by_recording:
        train_idx, val_idx = split_indices_by_recording(
            recording_ids,
            args.validation_fraction,
            args.seed,
        )
    else:
        train_idx, val_idx = split_indices(len(x_raw), args.validation_fraction, args.seed)

    mean = x_raw[train_idx].mean(axis=0)
    scale = x_raw[train_idx].std(axis=0)
    scale[scale < 1e-6] = 1.0
    x = (x_raw - mean) / scale

    x_train = x[train_idx]
    y_train = y[train_idx]
    x_val = x[val_idx]
    y_val = y[val_idx]

    w = np.zeros((x.shape[1], len(BUTTONS)), dtype=np.float32)
    b = np.zeros((len(BUTTONS),), dtype=np.float32)

    for _ in range(max(1, args.epochs)):
        probs = sigmoid(x_train @ w + b)
        error = probs - y_train
        w -= args.lr * (x_train.T @ error) / max(1, len(x_train))
        b -= args.lr * error.mean(axis=0)

    train_button_acc, train_exact = metrics(x_train, y_train, w, b)
    val_button_acc, val_exact = metrics(x_val, y_val, w, b)

    args.model.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        args.model,
        weights=w,
        bias=b,
        mean=mean,
        scale=scale,
        feature_names=np.array(feature_names),
        buttons=np.array(BUTTONS),
    )
    print(
        "rows={} train={} val={} train_button_acc={:.3f} train_exact={:.3f} "
        "val_button_acc={:.3f} val_exact={:.3f} model={}".format(
            len(x_raw),
            len(train_idx),
            len(val_idx),
            train_button_acc,
            train_exact,
            val_button_acc,
            val_exact,
            args.model,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
