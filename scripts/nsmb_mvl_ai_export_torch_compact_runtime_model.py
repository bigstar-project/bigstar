#!/usr/bin/env python3
"""Export a torch compact imitation checkpoint to a lightweight C++ runtime JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import torch


def tensor_list(state: dict[str, torch.Tensor], name: str) -> list[float]:
    return state[name].detach().cpu().float().reshape(-1).tolist()


def linear(state: dict[str, torch.Tensor], prefix: str) -> dict[str, Any]:
    weight = state[f"{prefix}.weight"].detach().cpu().float()
    bias = state[f"{prefix}.bias"].detach().cpu().float()
    return {
        "in": int(weight.shape[1]),
        "out": int(weight.shape[0]),
        "weight": weight.reshape(-1).tolist(),
        "bias": bias.reshape(-1).tolist(),
    }


def layer_norm(state: dict[str, torch.Tensor], prefix: str) -> dict[str, Any]:
    weight = state[f"{prefix}.weight"].detach().cpu().float()
    return {
        "size": int(weight.shape[0]),
        "eps": 1.0e-5,
        "weight": tensor_list(state, f"{prefix}.weight"),
        "bias": tensor_list(state, f"{prefix}.bias"),
    }


def batch_norm(state: dict[str, torch.Tensor], prefix: str) -> dict[str, Any]:
    weight = state[f"{prefix}.weight"].detach().cpu().float()
    return {
        "channels": int(weight.shape[0]),
        "eps": 1.0e-5,
        "weight": tensor_list(state, f"{prefix}.weight"),
        "bias": tensor_list(state, f"{prefix}.bias"),
        "running_mean": tensor_list(state, f"{prefix}.running_mean"),
        "running_var": tensor_list(state, f"{prefix}.running_var"),
    }


def conv2d(state: dict[str, torch.Tensor], prefix: str) -> dict[str, Any]:
    weight = state[f"{prefix}.weight"].detach().cpu().float()
    return {
        "out": int(weight.shape[0]),
        "in": int(weight.shape[1]),
        "kernel_h": int(weight.shape[2]),
        "kernel_w": int(weight.shape[3]),
        "padding": 1,
        "weight": weight.reshape(-1).tolist(),
        "bias": tensor_list(state, f"{prefix}.bias"),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path, help="torch checkpoint .pt")
    parser.add_argument("output", type=Path, help="runtime JSON")
    args = parser.parse_args()

    checkpoint = torch.load(args.checkpoint, map_location="cpu")
    metadata = checkpoint["metadata"]
    state: dict[str, torch.Tensor] = checkpoint["state_dict"]
    input_meta = metadata["input"]
    normalization = metadata["normalization"]

    head_names = [str(value) for value in input_meta["actionHeads"]]
    classes = {
        name: [str(value) for value in input_meta["actionClasses"][name]]
        for name in head_names
    }

    payload: dict[str, Any] = {
        "schema": "nsmb_mvl_torch_compact_policy_runtime_v1",
        "source_schema": metadata.get("schema", ""),
        "input_schema": input_meta.get("inputSchema", ""),
        "scalar_schema": input_meta.get("scalarSchema", ""),
        "label_schema": "nsmb_mvl_action_labels_v2",
        "head_names": head_names,
        "input_layout": {
            "scalar": int(input_meta["scalarDim"]),
            "terrain_height": int(input_meta["terrainHeight"]),
            "terrain_width": int(input_meta["terrainWidth"]),
            "terrain_channels": len(input_meta["terrainChannels"]),
            "opponent_terrain_channels": len(input_meta["opponentTerrainChannels"]),
            "entities": int(input_meta["entityCount"]) * len(input_meta["entityFeatures"]),
            "entity_count": int(input_meta["entityCount"]),
            "entity_features": len(input_meta["entityFeatures"]),
            "total": int(input_meta["scalarDim"])
            + int(input_meta["terrainHeight"]) * int(input_meta["terrainWidth"]) * len(input_meta["terrainChannels"])
            + int(input_meta["terrainHeight"]) * int(input_meta["terrainWidth"]) * len(input_meta["opponentTerrainChannels"])
            + int(input_meta["entityCount"]) * len(input_meta["entityFeatures"]),
        },
        "scalar_mean": normalization["scalar_mean"],
        "scalar_scale": normalization["scalar_scale"],
        "entity_mean": normalization["entity_mean"],
        "entity_scale": normalization["entity_scale"],
        "scalar": {
            "linear0": linear(state, "scalar_net.0"),
            "layer_norm1": layer_norm(state, "scalar_net.1"),
            "linear4": linear(state, "scalar_net.4"),
        },
        "terrain": {
            "conv0": conv2d(state, "terrain_net.0"),
            "batch_norm1": batch_norm(state, "terrain_net.1"),
            "conv3": conv2d(state, "terrain_net.3"),
            "batch_norm4": batch_norm(state, "terrain_net.4"),
            "conv7": conv2d(state, "terrain_net.7"),
            "batch_norm8": batch_norm(state, "terrain_net.8"),
            "conv10": conv2d(state, "terrain_net.10"),
            "batch_norm11": batch_norm(state, "terrain_net.11"),
            "linear15": linear(state, "terrain_net.15"),
        },
        "entity": {
            "linear0": linear(state, "entity_net.0"),
            "layer_norm1": layer_norm(state, "entity_net.1"),
            "linear3": linear(state, "entity_net.3"),
        },
        "fusion": {
            "linear0": linear(state, "fusion.0"),
            "layer_norm1": layer_norm(state, "fusion.1"),
            "linear4": linear(state, "fusion.4"),
        },
        "heads": {},
    }
    flat_layers: dict[str, dict[str, Any]] = {
        "scalar_linear0": payload["scalar"]["linear0"],
        "scalar_layer_norm1": payload["scalar"]["layer_norm1"],
        "scalar_linear4": payload["scalar"]["linear4"],
        "terrain_conv0": payload["terrain"]["conv0"],
        "terrain_batch_norm1": payload["terrain"]["batch_norm1"],
        "terrain_conv3": payload["terrain"]["conv3"],
        "terrain_batch_norm4": payload["terrain"]["batch_norm4"],
        "terrain_conv7": payload["terrain"]["conv7"],
        "terrain_batch_norm8": payload["terrain"]["batch_norm8"],
        "terrain_conv10": payload["terrain"]["conv10"],
        "terrain_batch_norm11": payload["terrain"]["batch_norm11"],
        "terrain_linear15": payload["terrain"]["linear15"],
        "entity_linear0": payload["entity"]["linear0"],
        "entity_layer_norm1": payload["entity"]["layer_norm1"],
        "entity_linear3": payload["entity"]["linear3"],
        "fusion_linear0": payload["fusion"]["linear0"],
        "fusion_layer_norm1": payload["fusion"]["layer_norm1"],
        "fusion_linear4": payload["fusion"]["linear4"],
    }
    for prefix, layer in flat_layers.items():
        for key, value in layer.items():
            payload[f"{prefix}_{key}"] = value
    for index, name in enumerate(head_names):
        payload[f"classes_{name}"] = classes[name]
        payload["heads"][name] = linear(state, f"heads.{index}")
        for key, value in payload["heads"][name].items():
            payload[f"head_{name}_{key}"] = value

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, separators=(",", ":"))
    print(
        "schema={} features={} heads={} output={}".format(
            payload["schema"],
            payload["input_layout"]["total"],
            len(head_names),
            args.output,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
