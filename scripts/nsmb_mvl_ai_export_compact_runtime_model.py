#!/usr/bin/env python3
"""Export a compact imitation model to the C++ compact action runtime JSON format."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

import nsmb_mvl_ai_train_compact_imitation as train_compact


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path, help="compact imitation model NPZ")
    parser.add_argument("output", type=Path, help="runtime model JSON")
    args = parser.parse_args()

    model = np.load(args.model, allow_pickle=False)
    payload: dict[str, object] = {
        "schema": "nsmb_mvl_compact_action_policy_v1",
        "label_schema": str(model["label_schema"]),
        "head_names": [str(value) for value in model["head_names"]],
        "input_layout": json.loads(str(model["input_layout"])),
        "mean": model["mean"].astype(float).tolist(),
        "scale": model["scale"].astype(float).tolist(),
    }
    for head in train_compact.HEADS:
        payload[f"classes_{head}"] = [str(value) for value in model[f"classes_{head}"]]
        payload[f"weights_{head}"] = model[f"weights_{head}"].astype(float).tolist()
        payload[f"bias_{head}"] = model[f"bias_{head}"].astype(float).tolist()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, separators=(",", ":"))
    print(
        "features={} heads={} output={}".format(
            len(payload["mean"]),  # type: ignore[arg-type]
            len(payload["head_names"]),  # type: ignore[arg-type]
            args.output,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
