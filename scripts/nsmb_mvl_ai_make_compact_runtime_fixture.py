#!/usr/bin/env python3
"""Create JSONL fixtures for C++ parity tests of compact action policies."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

import nsmb_mvl_ai_predict_compact_imitation as predict_compact
import nsmb_mvl_ai_train_compact_imitation as train_compact


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path, help="compact imitation model NPZ")
    parser.add_argument("dataset", type=Path, help="compact dataset NPZ")
    parser.add_argument("output", type=Path, help="fixture JSONL")
    parser.add_argument("--limit", type=int, default=100)
    args = parser.parse_args()

    model = np.load(args.model, allow_pickle=False)
    data = np.load(args.dataset, allow_pickle=False)
    x_raw, _ = train_compact.flatten_inputs(data)
    actions = data["actions"].astype(np.int64)
    frames = data["frames"].astype(np.int64)
    x = (x_raw - model["mean"]) / model["scale"]
    count = len(x_raw) if args.limit <= 0 else min(len(x_raw), args.limit)

    predictions: dict[str, np.ndarray] = {}
    for head in train_compact.HEADS:
        predictions[head] = train_compact.predict_head(
            x[:count],
            model[f"weights_{head}"],
            model[f"bias_{head}"],
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        for index in range(count):
            pred_ids = {head: int(predictions[head][index]) for head in train_compact.HEADS}
            target_ids = {
                head: int(actions[index, head_index])
                for head_index, head in enumerate(train_compact.HEADS)
            }
            record = {
                "schema": "nsmb_mvl_compact_runtime_fixture_v1",
                "frame": int(frames[index]),
                "features": x_raw[index].astype(float).tolist(),
                "target": [target_ids[head] for head in train_compact.HEADS],
                "targetHeld": predict_compact.action_held(target_ids),
                "pythonPred": [pred_ids[head] for head in train_compact.HEADS],
                "pythonHeld": predict_compact.action_held(pred_ids),
            }
            f.write(json.dumps(record, separators=(",", ":")) + "\n")
    print(f"rows={count} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
