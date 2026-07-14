#!/usr/bin/env python3
"""Train a compact-observation imitation baseline from NSMB MvL compact NPZ datasets."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


HEADS = ["horizontal", "vertical", "jump", "run", "fire"]
HEAD_CLASS_KEYS = {
    "horizontal": "action_classes_horizontal",
    "vertical": "action_classes_vertical",
    "jump": "action_classes_jump",
    "run": "action_classes_run",
    "fire": "action_classes_fire",
}


def softmax(logits: np.ndarray) -> np.ndarray:
    logits = logits - logits.max(axis=1, keepdims=True)
    exp = np.exp(np.clip(logits, -40.0, 40.0))
    return exp / np.maximum(exp.sum(axis=1, keepdims=True), 1e-8)


def flatten_inputs(data: np.lib.npyio.NpzFile) -> tuple[np.ndarray, dict[str, int]]:
    scalar = data["scalar"].astype(np.float32)
    terrain = data["terrain"].astype(np.float32).reshape(len(scalar), -1)
    opponent_terrain = data["opponent_terrain"].astype(np.float32).reshape(len(scalar), -1)
    entities = data["entities"].astype(np.float32).reshape(len(scalar), -1)
    x = np.concatenate([scalar, terrain, opponent_terrain, entities], axis=1)
    return x, {
        "scalar": int(scalar.shape[1]),
        "terrain": int(terrain.shape[1]),
        "opponentTerrain": int(opponent_terrain.shape[1]),
        "entities": int(entities.shape[1]),
        "total": int(x.shape[1]),
    }


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


def train_softmax_head(
    x: np.ndarray,
    y: np.ndarray,
    *,
    class_count: int,
    epochs: int,
    lr: float,
) -> tuple[np.ndarray, np.ndarray]:
    w = np.zeros((x.shape[1], class_count), dtype=np.float32)
    b = np.zeros((class_count,), dtype=np.float32)
    target = np.eye(class_count, dtype=np.float32)[np.clip(y, 0, class_count - 1)]
    for _ in range(max(1, epochs)):
        probs = softmax(x @ w + b)
        error = probs - target
        w -= lr * (x.T @ error) / max(1, len(x))
        b -= lr * error.mean(axis=0)
    return w, b


def predict_head(x: np.ndarray, w: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.argmax(softmax(x @ w + b), axis=1)


def metrics(x: np.ndarray, actions: np.ndarray, model: dict[str, tuple[np.ndarray, np.ndarray]]) -> dict[str, float]:
    if len(x) == 0:
        return {f"{head}_acc": 0.0 for head in HEADS} | {"exact": 0.0}
    preds = []
    result: dict[str, float] = {}
    for index, head in enumerate(HEADS):
        w, b = model[head]
        pred = predict_head(x, w, b)
        preds.append(pred)
        result[f"{head}_acc"] = float(np.mean(pred == actions[:, index]))
    pred_matrix = np.stack(preds, axis=1)
    result["exact"] = float(np.mean(np.all(pred_matrix == actions, axis=1)))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path, help="compact dataset NPZ")
    parser.add_argument("model", type=Path, help="output compact imitation model NPZ")
    parser.add_argument("--epochs", type=int, default=300)
    parser.add_argument("--lr", type=float, default=0.03)
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    data = np.load(args.dataset, allow_pickle=False)
    dataset_metadata = json.loads(str(data["metadata"].item()))
    dataset_schema = str(dataset_metadata.get("schema", ""))
    input_schema = str(dataset_metadata.get("observationSchema", "")) or {
        "nsmb_mvl_compact_dataset_v2": "nsmb_mvl_compact_observation_v2",
        "nsmb_mvl_compact_dataset_v3": "nsmb_mvl_compact_observation_v3",
    }.get(dataset_schema, "")
    if not input_schema:
        raise ValueError(f"unsupported dataset schema for compact policy: {dataset_schema or '<missing>'}")
    if "actions" not in data:
        raise ValueError(f"{args.dataset}: missing action labels; rebuild compact dataset first")
    x_raw, layout = flatten_inputs(data)
    actions = data["actions"].astype(np.int64)
    if len(x_raw) == 0:
        raise ValueError(f"{args.dataset}: empty dataset")

    train_idx, val_idx = split_indices(len(x_raw), args.validation_fraction, args.seed)
    mean = x_raw[train_idx].mean(axis=0)
    scale = x_raw[train_idx].std(axis=0)
    scale[scale < 1e-6] = 1.0
    x = (x_raw - mean) / scale

    model: dict[str, tuple[np.ndarray, np.ndarray]] = {}
    save_payload: dict[str, np.ndarray] = {
        "mean": mean.astype(np.float32),
        "scale": scale.astype(np.float32),
        "head_names": np.array(HEADS),
        "input_layout": np.array(json.dumps(layout, separators=(",", ":"))),
        "input_schema": np.array(input_schema),
        "scalar_schema": np.array(dataset_metadata.get("scalarSchema", "nsmb_mvl_scalar_features_v2")),
        "label_schema": np.array("nsmb_mvl_action_labels_v2"),
    }

    for head_index, head in enumerate(HEADS):
        classes = data[HEAD_CLASS_KEYS[head]]
        class_count = int(len(classes))
        w, b = train_softmax_head(
            x[train_idx],
            actions[train_idx, head_index],
            class_count=class_count,
            epochs=args.epochs,
            lr=args.lr,
        )
        model[head] = (w, b)
        save_payload[f"weights_{head}"] = w.astype(np.float32)
        save_payload[f"bias_{head}"] = b.astype(np.float32)
        save_payload[f"classes_{head}"] = classes

    train_metrics = metrics(x[train_idx], actions[train_idx], model)
    val_metrics = metrics(x[val_idx], actions[val_idx], model)

    args.model.parent.mkdir(parents=True, exist_ok=True)
    np.savez(args.model, **save_payload)
    print(
        "rows={} train={} val={} inputs={} train_exact={:.3f} val_exact={:.3f} "
        "val_horizontal={:.3f} val_vertical={:.3f} val_jump={:.3f} val_run={:.3f} val_fire={:.3f} model={}".format(
            len(x_raw),
            len(train_idx),
            len(val_idx),
            layout["total"],
            train_metrics["exact"],
            val_metrics["exact"],
            val_metrics["horizontal_acc"],
            val_metrics["vertical_acc"],
            val_metrics["jump_acc"],
            val_metrics["run_acc"],
            val_metrics["fire_acc"],
            args.model,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
