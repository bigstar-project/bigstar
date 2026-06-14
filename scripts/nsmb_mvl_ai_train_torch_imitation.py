#!/usr/bin/env python3
"""Train a GPU-friendly compact imitation policy with terrain CNN features."""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path
from typing import Any

import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset, Subset

import nsmb_mvl_ai_train_compact_imitation as compact_train


class CompactImitationDataset(Dataset[tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]]):
    def __init__(
        self,
        path: Path,
        *,
        scalar_mean: np.ndarray | None = None,
        scalar_scale: np.ndarray | None = None,
        entity_mean: np.ndarray | None = None,
        entity_scale: np.ndarray | None = None,
    ) -> None:
        self.path = path
        self.data = np.load(path, allow_pickle=False)
        self.scalar = self.data["scalar"].astype(np.float32)
        self.terrain = self.data["terrain"].astype(np.uint8)
        self.opponent_terrain = self.data["opponent_terrain"].astype(np.uint8)
        self.entities = self.data["entities"].astype(np.float32)
        self.actions = self.data["actions"].astype(np.int64)

        self.scalar_mean = np.zeros((self.scalar.shape[1],), dtype=np.float32) if scalar_mean is None else scalar_mean
        self.scalar_scale = np.ones((self.scalar.shape[1],), dtype=np.float32) if scalar_scale is None else scalar_scale
        self.entity_mean = np.zeros((self.entities.shape[2],), dtype=np.float32) if entity_mean is None else entity_mean
        self.entity_scale = np.ones((self.entities.shape[2],), dtype=np.float32) if entity_scale is None else entity_scale

    def __len__(self) -> int:
        return int(len(self.actions))

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        scalar = (self.scalar[index] - self.scalar_mean) / self.scalar_scale
        entities = (self.entities[index] - self.entity_mean) / self.entity_scale
        terrain = np.concatenate(
            [self.terrain[index], self.opponent_terrain[index]],
            axis=2,
        )
        terrain = np.transpose(terrain, (2, 0, 1)).astype(np.float32)
        return (
            torch.from_numpy(scalar),
            torch.from_numpy(terrain),
            torch.from_numpy(entities),
            torch.from_numpy(self.entities[index, :, 0] != 0),
            torch.from_numpy(self.actions[index]),
        )


class CompactPolicyNet(nn.Module):
    def __init__(
        self,
        *,
        scalar_dim: int,
        terrain_channels: int,
        entity_features: int,
        head_class_counts: list[int],
        dropout: float,
    ) -> None:
        super().__init__()
        self.scalar_net = nn.Sequential(
            nn.Linear(scalar_dim, 128),
            nn.LayerNorm(128),
            nn.SiLU(),
            nn.Dropout(dropout),
            nn.Linear(128, 128),
            nn.SiLU(),
        )
        self.terrain_net = nn.Sequential(
            nn.Conv2d(terrain_channels, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.SiLU(),
            nn.Conv2d(64, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.SiLU(),
            nn.MaxPool2d(kernel_size=2),
            nn.Conv2d(64, 128, kernel_size=3, padding=1),
            nn.BatchNorm2d(128),
            nn.SiLU(),
            nn.Conv2d(128, 128, kernel_size=3, padding=1),
            nn.BatchNorm2d(128),
            nn.SiLU(),
            nn.AdaptiveAvgPool2d((4, 4)),
            nn.Flatten(),
            nn.Linear(128 * 4 * 4, 256),
            nn.SiLU(),
            nn.Dropout(dropout),
        )
        self.entity_net = nn.Sequential(
            nn.Linear(entity_features, 96),
            nn.LayerNorm(96),
            nn.SiLU(),
            nn.Linear(96, 96),
            nn.SiLU(),
        )
        self.fusion = nn.Sequential(
            nn.Linear(128 + 256 + 96 * 2, 384),
            nn.LayerNorm(384),
            nn.SiLU(),
            nn.Dropout(dropout),
            nn.Linear(384, 256),
            nn.SiLU(),
        )
        self.heads = nn.ModuleList(nn.Linear(256, count) for count in head_class_counts)

    def forward(
        self,
        scalar: torch.Tensor,
        terrain: torch.Tensor,
        entities: torch.Tensor,
        entity_mask: torch.Tensor,
    ) -> list[torch.Tensor]:
        scalar_features = self.scalar_net(scalar)
        terrain_features = self.terrain_net(terrain)
        entity_features = self.entity_net(entities)
        mask = entity_mask.unsqueeze(-1)
        masked = entity_features.masked_fill(~mask, 0.0)
        count = mask.sum(dim=1).clamp_min(1)
        entity_mean = masked.sum(dim=1) / count
        has_entity = entity_mask.any(dim=1, keepdim=True)
        entity_max = entity_features.masked_fill(~mask, -1.0e4).max(dim=1).values
        entity_max = torch.where(has_entity, entity_max, torch.zeros_like(entity_max))
        fused = self.fusion(torch.cat([scalar_features, terrain_features, entity_mean, entity_max], dim=1))
        return [head(fused) for head in self.heads]


def split_indices(count: int, validation_fraction: float, seed: int) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    indices = np.arange(count)
    rng.shuffle(indices)
    val_count = int(round(count * validation_fraction))
    val_count = min(max(val_count, 1), count - 1)
    return indices[val_count:], indices[:val_count]


def normalization_stats(data: np.lib.npyio.NpzFile, train_indices: np.ndarray) -> dict[str, np.ndarray]:
    scalar = data["scalar"].astype(np.float32)
    entities = data["entities"].astype(np.float32)
    scalar_train = scalar[train_indices]
    entity_train = entities[train_indices].reshape(-1, entities.shape[-1])
    entity_mask = entity_train[:, 0] != 0
    if np.any(entity_mask):
        entity_train = entity_train[entity_mask]
    stats = {
        "scalar_mean": scalar_train.mean(axis=0).astype(np.float32),
        "scalar_scale": scalar_train.std(axis=0).astype(np.float32),
        "entity_mean": entity_train.mean(axis=0).astype(np.float32),
        "entity_scale": entity_train.std(axis=0).astype(np.float32),
    }
    for key in ("scalar_scale", "entity_scale"):
        stats[key][stats[key] < 1.0e-6] = 1.0
    return stats


def class_weight_tensors(
    actions: np.ndarray,
    train_indices: np.ndarray,
    class_counts: list[int],
    mode: str,
    device: torch.device,
) -> list[torch.Tensor | None]:
    if mode == "none":
        return [None for _ in class_counts]
    result: list[torch.Tensor | None] = []
    train_actions = actions[train_indices]
    for head_index, count in enumerate(class_counts):
        values = np.bincount(train_actions[:, head_index], minlength=count).astype(np.float32)
        values[values < 1.0] = 1.0
        weights = values.sum() / (count * values)
        if mode == "sqrt-balanced":
            weights = np.sqrt(weights)
        weights = np.clip(weights, 0.25, 8.0)
        weights = weights / weights.mean()
        result.append(torch.tensor(weights, dtype=torch.float32, device=device))
    return result


def batch_to_device(
    batch: tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor],
    device: torch.device,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
    scalar, terrain, entities, entity_mask, actions = batch
    return (
        scalar.to(device, non_blocking=True),
        terrain.to(device, non_blocking=True),
        entities.to(device, non_blocking=True),
        entity_mask.to(device, non_blocking=True).bool(),
        actions.to(device, non_blocking=True),
    )


def evaluate(
    model: CompactPolicyNet,
    loader: DataLoader,
    losses: list[nn.CrossEntropyLoss],
    device: torch.device,
    head_names: list[str],
    class_names: dict[str, list[str]],
) -> dict[str, Any]:
    model.eval()
    total_loss = 0.0
    rows = 0
    exact = 0
    per_head_correct = np.zeros((len(head_names),), dtype=np.int64)
    per_head_total = np.zeros((len(head_names),), dtype=np.int64)
    confusion = {
        head: np.zeros((len(class_names[head]), len(class_names[head])), dtype=np.int64)
        for head in head_names
    }
    with torch.no_grad():
        for raw_batch in loader:
            scalar, terrain, entities, entity_mask, actions = batch_to_device(raw_batch, device)
            logits = model(scalar, terrain, entities, entity_mask)
            loss = sum(loss_fn(logit, actions[:, i]) for i, (loss_fn, logit) in enumerate(zip(losses, logits)))
            batch_rows = int(actions.shape[0])
            total_loss += float(loss.item()) * batch_rows
            rows += batch_rows
            preds = torch.stack([logit.argmax(dim=1) for logit in logits], dim=1)
            exact += int(torch.all(preds == actions, dim=1).sum().item())
            for i, head in enumerate(head_names):
                pred = preds[:, i].detach().cpu().numpy()
                target = actions[:, i].detach().cpu().numpy()
                per_head_correct[i] += int((pred == target).sum())
                per_head_total[i] += len(target)
                for actual, predicted in zip(target, pred):
                    confusion[head][int(actual), int(predicted)] += 1
    metrics: dict[str, Any] = {
        "loss": total_loss / max(1, rows),
        "exact": exact / max(1, rows),
        "heads": {},
    }
    for i, head in enumerate(head_names):
        matrix = confusion[head]
        classes = class_names[head]
        per_class = {}
        for class_index, name in enumerate(classes):
            actual = int(matrix[class_index, :].sum())
            predicted = int(matrix[:, class_index].sum())
            true_positive = int(matrix[class_index, class_index])
            per_class[name] = {
                "actual": actual,
                "predicted": predicted,
                "truePositive": true_positive,
                "recall": true_positive / actual if actual else 0.0,
                "precision": true_positive / predicted if predicted else 0.0,
            }
        metrics["heads"][head] = {
            "accuracy": per_head_correct[i] / max(1, per_head_total[i]),
            "perClass": per_class,
        }
    return metrics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path, help="compact dataset NPZ")
    parser.add_argument("output", type=Path, help="output torch checkpoint .pt")
    parser.add_argument("--epochs", type=int, default=40)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--lr", type=float, default=2.0e-3)
    parser.add_argument("--weight-decay", type=float, default=1.0e-4)
    parser.add_argument("--dropout", type=float, default=0.1)
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--class-weight-mode", choices=["none", "balanced", "sqrt-balanced"], default="sqrt-balanced")
    parser.add_argument("--patience", type=int, default=8)
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    if torch.cuda.is_available():
        torch.backends.cudnn.benchmark = True

    raw = np.load(args.dataset, allow_pickle=False)
    actions = raw["actions"].astype(np.int64)
    train_indices, val_indices = split_indices(len(actions), args.validation_fraction, args.seed)
    stats = normalization_stats(raw, train_indices)
    dataset = CompactImitationDataset(args.dataset, **stats)

    train_loader = DataLoader(
        Subset(dataset, train_indices.tolist()),
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=0,
        pin_memory=args.device.startswith("cuda"),
    )
    val_loader = DataLoader(
        Subset(dataset, val_indices.tolist()),
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=0,
        pin_memory=args.device.startswith("cuda"),
    )

    head_names = [str(value) for value in raw["action_heads"]]
    class_names = {
        head: [str(value) for value in raw[compact_train.HEAD_CLASS_KEYS[head]]]
        for head in head_names
    }
    class_counts = [len(class_names[head]) for head in head_names]

    device = torch.device(args.device)
    model = CompactPolicyNet(
        scalar_dim=int(raw["scalar"].shape[1]),
        terrain_channels=int(raw["terrain"].shape[3] + raw["opponent_terrain"].shape[3]),
        entity_features=int(raw["entities"].shape[2]),
        head_class_counts=class_counts,
        dropout=args.dropout,
    ).to(device)

    weights = class_weight_tensors(actions, train_indices, class_counts, args.class_weight_mode, device)
    losses = [nn.CrossEntropyLoss(weight=weight) for weight in weights]
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    total_steps = max(1, args.epochs * len(train_loader))
    scheduler = torch.optim.lr_scheduler.OneCycleLR(
        optimizer,
        max_lr=args.lr,
        total_steps=total_steps,
        pct_start=0.15,
        div_factor=10.0,
        final_div_factor=20.0,
    )
    scaler = torch.amp.GradScaler("cuda", enabled=device.type == "cuda")

    best_state: dict[str, torch.Tensor] | None = None
    best_metrics: dict[str, Any] | None = None
    best_epoch = 0
    best_loss = math.inf
    stale_epochs = 0
    started = time.time()
    history = []

    for epoch in range(1, args.epochs + 1):
        model.train()
        train_loss = 0.0
        train_rows = 0
        for raw_batch in train_loader:
            scalar, terrain, entities, entity_mask, batch_actions = batch_to_device(raw_batch, device)
            optimizer.zero_grad(set_to_none=True)
            with torch.amp.autocast("cuda", enabled=device.type == "cuda"):
                logits = model(scalar, terrain, entities, entity_mask)
                loss = sum(loss_fn(logit, batch_actions[:, i]) for i, (loss_fn, logit) in enumerate(zip(losses, logits)))
            scaler.scale(loss).backward()
            scaler.unscale_(optimizer)
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            scaler.step(optimizer)
            scaler.update()
            scheduler.step()
            batch_rows = int(batch_actions.shape[0])
            train_loss += float(loss.item()) * batch_rows
            train_rows += batch_rows

        val_metrics = evaluate(model, val_loader, losses, device, head_names, class_names)
        train_loss /= max(1, train_rows)
        row = {
            "epoch": epoch,
            "trainLoss": train_loss,
            "valLoss": val_metrics["loss"],
            "valExact": val_metrics["exact"],
            "valHeadAccuracy": {
                head: val_metrics["heads"][head]["accuracy"]
                for head in head_names
            },
            "elapsedSeconds": time.time() - started,
        }
        history.append(row)
        print(
            "epoch={:03d} train_loss={:.4f} val_loss={:.4f} val_exact={:.3f} {}".format(
                epoch,
                train_loss,
                val_metrics["loss"],
                val_metrics["exact"],
                " ".join(
                    f"{head}={val_metrics['heads'][head]['accuracy']:.3f}"
                    for head in head_names
                ),
            ),
            flush=True,
        )
        if val_metrics["loss"] < best_loss:
            best_loss = float(val_metrics["loss"])
            best_metrics = val_metrics
            best_epoch = epoch
            best_state = {key: value.detach().cpu().clone() for key, value in model.state_dict().items()}
            stale_epochs = 0
        else:
            stale_epochs += 1
            if stale_epochs >= args.patience:
                print(f"early_stop epoch={epoch} best_val_loss={best_loss:.4f}", flush=True)
                break

    if best_state is None or best_metrics is None:
        raise RuntimeError("training did not produce a checkpoint")
    model.load_state_dict(best_state)

    metadata = {
        "schema": "nsmb_mvl_torch_compact_policy_v1",
        "dataset": str(args.dataset),
        "rows": int(len(actions)),
        "trainRows": int(len(train_indices)),
        "validationRows": int(len(val_indices)),
        "device": str(device),
        "torchVersion": torch.__version__,
        "input": {
            "scalarDim": int(raw["scalar"].shape[1]),
            "terrainHeight": int(raw["terrain"].shape[1]),
            "terrainWidth": int(raw["terrain"].shape[2]),
            "terrainChannels": [str(value) for value in raw["terrain_channels"]],
            "opponentTerrainChannels": [str(value) for value in raw["terrain_channels"]],
            "entityCount": int(raw["entities"].shape[1]),
            "entityFeatures": [str(value) for value in raw["entity_features"]],
            "actionHeads": head_names,
            "actionClasses": class_names,
        },
        "normalization": {key: value.tolist() for key, value in stats.items()},
        "args": vars(args) | {"dataset": str(args.dataset), "output": str(args.output)},
        "history": history,
        "bestEpoch": best_epoch,
        "bestValLoss": best_loss,
        "bestMetrics": best_metrics,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "metadata": metadata,
            "state_dict": best_state,
        },
        args.output,
    )
    metrics_path = args.output.with_suffix(".metrics.json")
    with metrics_path.open("w", encoding="utf-8") as f:
        json.dump(metadata, f, ensure_ascii=False, indent=2)
    print(f"saved checkpoint={args.output} metrics={metrics_path} best_val_loss={best_loss:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
