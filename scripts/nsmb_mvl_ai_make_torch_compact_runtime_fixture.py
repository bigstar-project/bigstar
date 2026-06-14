#!/usr/bin/env python3
"""Create JSONL fixtures for C++ parity tests of torch compact action policies."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import torch

import nsmb_mvl_ai_predict_compact_imitation as predict_compact
import nsmb_mvl_ai_train_torch_imitation as train_torch


def raw_features(data: np.lib.npyio.NpzFile, index: int) -> np.ndarray:
    return np.concatenate(
        [
            data["scalar"][index].astype(np.float32).reshape(-1),
            data["terrain"][index].astype(np.float32).reshape(-1),
            data["opponent_terrain"][index].astype(np.float32).reshape(-1),
            data["entities"][index].astype(np.float32).reshape(-1),
        ]
    )


def tensors_from_raw_features(
    features: np.ndarray,
    metadata: dict,
    normalization: dict,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
    input_meta = metadata["input"]
    scalar_dim = int(input_meta["scalarDim"])
    height = int(input_meta["terrainHeight"])
    width = int(input_meta["terrainWidth"])
    terrain_channels = len(input_meta["terrainChannels"])
    opponent_terrain_channels = len(input_meta["opponentTerrainChannels"])
    entity_count = int(input_meta["entityCount"])
    entity_features = len(input_meta["entityFeatures"])

    scalar_end = scalar_dim
    terrain_end = scalar_end + height * width * terrain_channels
    opponent_terrain_end = terrain_end + height * width * opponent_terrain_channels
    entity_end = opponent_terrain_end + entity_count * entity_features

    scalar = features[:, :scalar_end].astype(np.float32)
    terrain = features[:, scalar_end:terrain_end].reshape(len(features), height, width, terrain_channels)
    opponent_terrain = features[:, terrain_end:opponent_terrain_end].reshape(
        len(features), height, width, opponent_terrain_channels
    )
    entities = features[:, opponent_terrain_end:entity_end].reshape(len(features), entity_count, entity_features)

    scalar = (scalar - np.array(normalization["scalar_mean"], dtype=np.float32)) / np.array(
        normalization["scalar_scale"], dtype=np.float32
    )
    entities_normalized = (entities - np.array(normalization["entity_mean"], dtype=np.float32)) / np.array(
        normalization["entity_scale"], dtype=np.float32
    )
    terrain_tensor = np.transpose(
        np.concatenate([terrain, opponent_terrain], axis=3).astype(np.float32),
        (0, 3, 1, 2),
    )
    return (
        torch.from_numpy(scalar),
        torch.from_numpy(terrain_tensor),
        torch.from_numpy(entities_normalized.astype(np.float32)),
        torch.from_numpy(entities[:, :, 0] != 0),
    )


def synthetic_raw_features(metadata: dict, count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    input_meta = metadata["input"]
    scalar_dim = int(input_meta["scalarDim"])
    height = int(input_meta["terrainHeight"])
    width = int(input_meta["terrainWidth"])
    terrain_channels = len(input_meta["terrainChannels"])
    opponent_terrain_channels = len(input_meta["opponentTerrainChannels"])
    entity_count = int(input_meta["entityCount"])
    entity_features = len(input_meta["entityFeatures"])
    total = (
        scalar_dim
        + height * width * terrain_channels
        + height * width * opponent_terrain_channels
        + entity_count * entity_features
    )
    features = np.zeros((count, total), dtype=np.float32)
    features[:, :scalar_dim] = rng.normal(0.0, 1.0, size=(count, scalar_dim)).astype(np.float32)
    terrain_start = scalar_dim
    terrain_end = terrain_start + height * width * terrain_channels
    opponent_end = terrain_end + height * width * opponent_terrain_channels
    features[:, terrain_start:terrain_end] = (rng.random((count, terrain_end - terrain_start)) < 0.08).astype(np.float32)
    features[:, terrain_end:opponent_end] = (rng.random((count, opponent_end - terrain_end)) < 0.08).astype(np.float32)
    entity_start = opponent_end
    for row in range(count):
        active = min(entity_count, 1 + (row % 5))
        for entity in range(active):
            base = entity_start + entity * entity_features
            features[row, base] = 1 + (entity % 8)
            features[row, base + 1:base + entity_features] = rng.normal(
                0.0,
                0.5,
                size=(entity_features - 1,),
            ).astype(np.float32)
    return features


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path, help="torch checkpoint .pt")
    parser.add_argument("dataset", type=Path, help="compact dataset NPZ")
    parser.add_argument("output", type=Path, help="fixture JSONL")
    parser.add_argument("--limit", type=int, default=100)
    parser.add_argument("--synthetic", action="store_true", help="generate synthetic feature rows instead of reading dataset")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    checkpoint = torch.load(args.checkpoint, map_location="cpu")
    metadata = checkpoint["metadata"]
    normalization = metadata["normalization"]
    head_names = [str(value) for value in metadata["input"]["actionHeads"]]
    class_counts = [
        int(len(metadata["input"]["actionClasses"][head]))
        for head in head_names
    ]
    model = train_torch.CompactPolicyNet(
        scalar_dim=int(metadata["input"]["scalarDim"]),
        terrain_channels=len(metadata["input"]["terrainChannels"]) + len(metadata["input"]["opponentTerrainChannels"]),
        entity_features=len(metadata["input"]["entityFeatures"]),
        head_class_counts=class_counts,
        dropout=0.0,
    )
    model.load_state_dict(checkpoint["state_dict"])
    model.eval()

    if args.synthetic:
        count = args.limit if args.limit > 0 else 128
        features = synthetic_raw_features(metadata, count, args.seed)
        frames = np.arange(count, dtype=np.int64)
        actions = np.zeros((count, len(head_names)), dtype=np.int64)
        scalar_batch, terrain_batch, entity_batch, entity_mask_batch = tensors_from_raw_features(
            features,
            metadata,
            normalization,
        )
    else:
        data = np.load(args.dataset, allow_pickle=False)
        actions = data["actions"].astype(np.int64)
        frames = data["frames"].astype(np.int64)
        count = len(data["actions"]) if args.limit <= 0 else min(len(data["actions"]), args.limit)
        features = np.stack([raw_features(data, index) for index in range(count)], axis=0)
        scalar_batch, terrain_batch, entity_batch, entity_mask_batch = tensors_from_raw_features(
            features,
            metadata,
            normalization,
        )
    with torch.no_grad():
        logits = model(
            scalar_batch,
            terrain_batch,
            entity_batch,
            entity_mask_batch.bool(),
        )
        predictions = {
            head: logit.argmax(dim=1).detach().cpu().numpy()
            for head, logit in zip(head_names, logits)
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        for index in range(count):
            pred_ids = {
                head: int(predictions[head][index])
                for head in head_names
            }
            target_ids = {
                head: int(actions[index, head_index])
                for head_index, head in enumerate(head_names)
            }
            record = {
                "schema": "nsmb_mvl_torch_compact_runtime_fixture_v1",
                "frame": int(frames[index]),
                "features": features[index].astype(float).tolist(),
                "target": [target_ids[head] for head in head_names],
                "targetHeld": predict_compact.action_held(target_ids),
                "pythonPred": [pred_ids[head] for head in head_names],
                "pythonHeld": predict_compact.action_held(pred_ids),
            }
            f.write(json.dumps(record, separators=(",", ":")) + "\n")
    print(f"rows={count} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
