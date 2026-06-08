#!/usr/bin/env python3
"""Export an NSMB MvL imitation .npz model to the C++ runtime JSON format."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--feature-schema-id", default="nsmb_mvl_ai_dataset_v1")
    args = parser.parse_args()

    model = np.load(args.model, allow_pickle=False)
    payload = {
        "schema": "nsmb_mvl_linear_policy_v1",
        "feature_schema_id": args.feature_schema_id,
        "feature_names": [str(value) for value in model["feature_names"]],
        "buttons": [str(value) for value in model["buttons"]],
        "mean": model["mean"].astype(float).tolist(),
        "scale": model["scale"].astype(float).tolist(),
        "weights": model["weights"].astype(float).tolist(),
        "bias": model["bias"].astype(float).tolist(),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, separators=(",", ":"))
    print(
        "features={} buttons={} output={}".format(
            len(payload["feature_names"]),
            len(payload["buttons"]),
            args.output,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
