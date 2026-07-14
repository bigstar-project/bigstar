#!/usr/bin/env python3
"""Build exact-hitbox NPZ datasets from NSMB MvL compact observation v3 JSONL."""

from __future__ import annotations

import argparse
import gzip
import json
import sys
from pathlib import Path
from typing import Any, Iterable, TextIO

import numpy as np

import nsmb_mvl_ai_build_dataset as legacy
import nsmb_mvl_ai_convert_playlog_v3 as compact_v3


ENTITY_FEATURES = [
    "category_id",
    "kind",
    "owner",
    "dx",
    "dy",
    "vx",
    "vy",
    "object_id",
    "settings",
    "state",
    "flags",
    "screen_mask",
    "confidence",
    "source_id",
]

ENTITY_SOURCE_IDS = {
    "object": 1,
    "fireball": 2,
}

COMPACT_SCALAR_FEATURES = [
    "stage_id", "stage_group", "self_x", "self_y", "self_vx", "self_vy",
    "self_powerup_kind", "self_body_size_class", "self_hitbox_found",
    "self_hitbox_center_offset_x", "self_hitbox_center_offset_y", "self_hitbox_half_width",
    "self_hitbox_half_height", "self_invincible", "self_star_invincible",
    "self_battle_stars", "self_coins", "opponent_dx", "opponent_dy",
    "opponent_powerup_kind", "opponent_body_size_class", "opponent_hitbox_found",
    "opponent_hitbox_center_offset_x", "opponent_hitbox_center_offset_y",
    "opponent_hitbox_half_width", "opponent_hitbox_half_height", "opponent_battle_stars",
    "target_found", "target_dx", "target_dy", "nearest_item_found", "nearest_item_dx",
    "nearest_item_dy", "nearest_item_kind", "nearest_item_avoid", "runtime_hazard_found",
    "runtime_hazard_dx", "runtime_hazard_dy", "runtime_hazard_closing",
    "runtime_hazard_category",
    *[f"tile_{name}" for name in compact_v3.COMPACT_SCALAR_TILE_KEYS],
]


def open_text(path: Path) -> TextIO:
    if path.name.lower().endswith(".gz"):
        return gzip.open(path, "rt", encoding="utf-8")
    return path.open("r", encoding="utf-8")


def iter_observations(path: Path) -> Iterable[dict[str, Any]]:
    with open_text(path) as f:
        for line_no, line in enumerate(f, 1):
            terminated = line.endswith("\n")
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                if not terminated:
                    print(
                        f"warning: {path}:{line_no}: discarded incomplete trailing JSONL record",
                        file=sys.stderr,
                    )
                    return
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if record.get("schema") != "nsmb_mvl_compact_observation_v3":
                raise ValueError(f"{path}:{line_no}: expected nsmb_mvl_compact_observation_v3")
            yield record


def terrain_tensor(player: dict[str, Any]) -> np.ndarray:
    terrain = player.get("terrain") or {}
    height = legacy.num(terrain.get("height"), legacy.TILE_GRID_HEIGHT)
    width = legacy.num(terrain.get("width"), legacy.TILE_GRID_WIDTH)
    channels = terrain.get("channels") or compact_v3.TERRAIN_CHANNELS
    result = np.zeros((height, width, len(channels)), dtype=np.uint8)
    for cell in terrain.get("cells") or []:
        row = legacy.num(cell.get("r"), -1)
        col = legacy.num(cell.get("c"), -1)
        if row < 0 or row >= height or col < 0 or col >= width:
            continue
        mask = legacy.num(cell.get("mask"))
        for channel_index in range(len(channels)):
            if mask & (1 << channel_index):
                result[row, col, channel_index] = 1
    return result


def entity_vector(entity: dict[str, Any], player: int) -> list[float]:
    relative = ((entity.get("relative") or {}).get(f"player{player}")) or {}
    kind = legacy.num(entity.get("kind"), 0)
    confidence = legacy.num(entity.get("ownerConfidence"), 0)
    kind_by_player = entity.get("kindByPlayer") or []
    if len(kind_by_player) > player and isinstance(kind_by_player[player], dict):
        kind = legacy.num(kind_by_player[player].get("kind"), kind)
        confidence = legacy.num(kind_by_player[player].get("confidence"), confidence)
    vel = entity.get("vel") or {}
    return [
        float(legacy.num(entity.get("categoryId"))),
        float(kind),
        float(legacy.num(entity.get("owner"), -1)),
        float(legacy.num(relative.get("dx"))),
        float(legacy.num(relative.get("dy"))),
        float(legacy.num(vel.get("x"))),
        float(legacy.num(vel.get("y"))),
        float(legacy.num(entity.get("objectId"))),
        float(legacy.num(entity.get("settings"))),
        float(legacy.num(entity.get("state"))),
        float(legacy.num(entity.get("flags"))),
        float(legacy.num(entity.get("screenMask"))),
        float(confidence),
        float(ENTITY_SOURCE_IDS.get(str(entity.get("source") or ""), 0)),
    ]


def entity_distance_key(entity: dict[str, Any], player: int) -> int:
    relative = ((entity.get("relative") or {}).get(f"player{player}")) or {}
    dx = legacy.num(relative.get("dx"))
    dy = legacy.num(relative.get("dy"))
    return dx * dx + dy * dy


def scalar_features(record: dict[str, Any], player: int) -> dict[str, int]:
    features = ((record.get("scalarFeaturesByPlayer") or {}).get(f"player{player}")) or {}
    missing = [name for name in COMPACT_SCALAR_FEATURES if name not in features]
    if missing:
        raise ValueError(
            f"observation v3 player{player} is missing required scalar features: {', '.join(missing)}"
        )
    values = {str(key): legacy.num(value) for key, value in features.items()}
    return {name: values.get(name, 0) for name in COMPACT_SCALAR_FEATURES}


def labels(record: dict[str, Any], player: int) -> tuple[int, list[int], list[int]]:
    label = ((record.get("labels") or {}).get(f"player{player}")) or {}
    held = legacy.num(label.get("allowedHeld"), legacy.num(label.get("held")) & compact_v3.ALLOWED_HELD_MASK)
    buttons = label.get("buttons") or {}
    if not buttons:
        buttons = compact_v3.button_labels(held)
    actions = label.get("actions") or compact_v3.action_labels(
        legacy.num(label.get("held"), held),
        legacy.num(label.get("pressed")),
        can_fire=False,
    )
    fire_id = legacy.num(actions.get("fireId"))
    if fire_id >= len(compact_v3.ACTION_CLASSES["fire"]):
        raise ValueError(
            f"unexpected fire action id {fire_id}; rebuild compact observation v3 with nsmb_mvl_action_labels_v2"
        )
    return (
        held,
        [legacy.num(buttons.get(name)) for name in compact_v3.ALLOWED_BUTTON_BITS.keys()],
        [
            legacy.num(actions.get("horizontalId")),
            legacy.num(actions.get("verticalId")),
            legacy.num(actions.get("jumpId")),
            legacy.num(actions.get("runId")),
            fire_id,
        ],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="compact observation v3 JSONL(.gz)")
    parser.add_argument("output", type=Path, help="output compact dataset NPZ")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--max-entities", type=int, default=32)
    parser.add_argument("--require-player-found", action="store_true")
    args = parser.parse_args()

    scalar_rows: list[list[float]] = []
    terrain_rows: list[np.ndarray] = []
    opponent_terrain_rows: list[np.ndarray] = []
    entity_rows: list[np.ndarray] = []
    label_rows: list[list[int]] = []
    action_rows: list[list[int]] = []
    held_rows: list[int] = []
    frame_rows: list[int] = []
    scalar_names: list[str] | None = None

    for record in iter_observations(args.input):
        players = record.get("players") or []
        if len(players) <= args.player:
            continue
        player_record = players[args.player]
        opponent_record = players[args.player ^ 1] if len(players) > (args.player ^ 1) else {}
        if (
            legacy.num((player_record.get("hitbox") or {}).get("found")) != 1
            or legacy.num((opponent_record.get("hitbox") or {}).get("found")) != 1
        ):
            continue
        if args.require_player_found and not legacy.num(player_record.get("found")):
            continue
        label = ((record.get("labels") or {}).get(f"player{args.player}")) or {}
        if not legacy.num(label.get("valid")):
            continue

        features = scalar_features(record, args.player)
        if scalar_names is None:
            scalar_names = list(features.keys())
        scalar_rows.append([float(features.get(name, 0)) for name in scalar_names])
        terrain_rows.append(terrain_tensor(player_record))
        opponent_terrain_rows.append(terrain_tensor(players[args.player ^ 1]) if len(players) > (args.player ^ 1) else np.zeros_like(terrain_rows[-1]))

        entities = sorted(
            record.get("entities") or [],
            key=lambda entity: entity_distance_key(entity, args.player),
        )
        entity_matrix = np.zeros((args.max_entities, len(ENTITY_FEATURES)), dtype=np.float32)
        for index, entity in enumerate(entities[: args.max_entities]):
            entity_matrix[index] = np.array(entity_vector(entity, args.player), dtype=np.float32)
        entity_rows.append(entity_matrix)

        held, button_values, action_values = labels(record, args.player)
        held_rows.append(held)
        label_rows.append(button_values)
        action_rows.append(action_values)
        frame_rows.append(legacy.num(record.get("frame")))

    if scalar_names is None:
        raise ValueError(f"{args.input}: no usable compact observations")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        args.output,
        scalar=np.array(scalar_rows, dtype=np.float32),
        scalar_names=np.array(scalar_names),
        terrain=np.stack(terrain_rows).astype(np.uint8),
        opponent_terrain=np.stack(opponent_terrain_rows).astype(np.uint8),
        terrain_channels=np.array(compact_v3.TERRAIN_CHANNELS),
        entities=np.stack(entity_rows).astype(np.float32),
        entity_features=np.array(ENTITY_FEATURES),
        labels=np.array(label_rows, dtype=np.float32),
        label_buttons=np.array(list(compact_v3.ALLOWED_BUTTON_BITS.keys())),
        label_held=np.array(held_rows, dtype=np.int32),
        actions=np.array(action_rows, dtype=np.int64),
        action_heads=np.array(compact_v3.ACTION_HEADS),
        action_classes_horizontal=np.array(compact_v3.ACTION_CLASSES["horizontal"]),
        action_classes_vertical=np.array(compact_v3.ACTION_CLASSES["vertical"]),
        action_classes_jump=np.array(compact_v3.ACTION_CLASSES["jump"]),
        action_classes_run=np.array(compact_v3.ACTION_CLASSES["run"]),
        action_classes_fire=np.array(compact_v3.ACTION_CLASSES["fire"]),
        frames=np.array(frame_rows, dtype=np.int32),
        metadata=np.array(
            json.dumps(
                {
                    "schema": "nsmb_mvl_compact_dataset_v3",
                    "observationSchema": "nsmb_mvl_compact_observation_v3",
                    "scalarSchema": "nsmb_mvl_scalar_features_v3",
                    "labelSchema": "nsmb_mvl_action_labels_v2",
                    "source": str(args.input),
                    "player": args.player,
                    "maxEntities": args.max_entities,
                    "allowedButtons": list(compact_v3.ALLOWED_BUTTON_BITS.keys()),
                    "actionHeads": compact_v3.ACTION_HEADS,
                    "rows": len(frame_rows),
                },
                separators=(",", ":"),
            )
        ),
    )
    print(
        "rows={} scalar_features={} terrain_shape={} entities_shape={} output={}".format(
            len(frame_rows),
            len(scalar_names),
            tuple(np.stack(terrain_rows).shape),
            (len(entity_rows), args.max_entities, len(ENTITY_FEATURES)),
            args.output,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
