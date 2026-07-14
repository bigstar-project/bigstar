#!/usr/bin/env python3
"""Run offline predictions for compact-observation imitation models."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np

import nsmb_mvl_ai_convert_playlog_v2 as compact_v2
import nsmb_mvl_ai_train_compact_imitation as train_compact


def action_held(row: dict[str, int]) -> int:
    held = 0
    if row["horizontal"] == 1:
        held |= 1 << compact_v2.ALLOWED_BUTTON_BITS["left"]
    elif row["horizontal"] == 2:
        held |= 1 << compact_v2.ALLOWED_BUTTON_BITS["right"]
    if row["vertical"] == 1:
        held |= 1 << compact_v2.ALLOWED_BUTTON_BITS["up"]
    elif row["vertical"] == 2:
        held |= 1 << compact_v2.ALLOWED_BUTTON_BITS["down"]
    if row["jump"] != 0:
        held |= 1 << compact_v2.ALLOWED_BUTTON_BITS["b"]
    if row["run"] != 0 or row["fire"] != 0:
        held |= 1 << compact_v2.ALLOWED_BUTTON_BITS["y"]
    return held


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("output_csv", type=Path)
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    model = np.load(args.model, allow_pickle=False)
    data = np.load(args.dataset, allow_pickle=False)
    x_raw, _ = train_compact.flatten_inputs(data)
    actions = data["actions"].astype(np.int64)
    frames = data["frames"].astype(np.int64)
    x = (x_raw - model["mean"]) / model["scale"]
    count = len(x) if args.limit <= 0 else min(len(x), args.limit)

    preds: dict[str, np.ndarray] = {}
    classes: dict[str, np.ndarray] = {}
    for head in train_compact.HEADS:
        preds[head] = train_compact.predict_head(
            x[:count],
            model[f"weights_{head}"],
            model[f"bias_{head}"],
        )
        classes[head] = model[f"classes_{head}"]

    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.output_csv.open("w", encoding="utf-8", newline="") as f:
        fieldnames = [
            "frame",
            "pred_held",
            "target_held",
            "exact",
        ]
        for head in train_compact.HEADS:
            fieldnames.extend([f"pred_{head}", f"target_{head}", f"match_{head}"])
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        exact_count = 0
        for index in range(count):
            pred_ids = {head: int(preds[head][index]) for head in train_compact.HEADS}
            target_ids = {
                head: int(actions[index, head_index])
                for head_index, head in enumerate(train_compact.HEADS)
            }
            exact = all(pred_ids[head] == target_ids[head] for head in train_compact.HEADS)
            exact_count += int(exact)
            row: dict[str, int | str] = {
                "frame": int(frames[index]),
                "pred_held": action_held(pred_ids),
                "target_held": action_held(target_ids),
                "exact": int(exact),
            }
            for head in train_compact.HEADS:
                pred_id = pred_ids[head]
                target_id = target_ids[head]
                row[f"pred_{head}"] = str(classes[head][pred_id])
                row[f"target_{head}"] = str(classes[head][target_id])
                row[f"match_{head}"] = int(pred_id == target_id)
            writer.writerow(row)
    print(
        "rows={} exact={:.3f} output={}".format(
            count,
            exact_count / max(1, count),
            args.output_csv,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
