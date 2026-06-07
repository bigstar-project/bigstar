#!/usr/bin/env python3
"""Build a first-pass imitation-learning CSV from NSMB MvL AI play logs."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
from typing import Any


BUTTON_BITS = {
    "a": 0,
    "b": 1,
    "select": 2,
    "start": 3,
    "right": 4,
    "left": 5,
    "up": 6,
    "down": 7,
    "r": 8,
    "l": 9,
    "x": 10,
    "y": 11,
}

NEAREST_CATEGORIES = [
    "big_star_actor",
    "big_star_candidate",
    "world_item",
    "neutral_item",
    "dropped_star_item",
    "moving_hazard",
    "enemy_koopa",
]


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def pos(entity: dict[str, Any]) -> dict[str, int]:
    value = entity.get("pos") or {}
    return {
        "x": num(value.get("x")),
        "y": num(value.get("y")),
        "z": num(value.get("z")),
    }


def vel(entity: dict[str, Any]) -> dict[str, int]:
    value = entity.get("vel") or {}
    return {
        "x": num(value.get("x")),
        "y": num(value.get("y")),
        "z": num(value.get("z")),
    }


def nearest_object(
    objects: list[dict[str, Any]],
    category: str,
    self_pos: dict[str, int],
) -> tuple[int, int, int, int]:
    best: tuple[int, int, int, int] | None = None
    for obj in objects:
        if obj.get("category") != category:
            continue
        obj_pos = pos(obj)
        dx = obj_pos["x"] - self_pos["x"]
        dy = obj_pos["y"] - self_pos["y"]
        dist2 = dx * dx + dy * dy
        if best is None or dist2 < best[3]:
            best = (1, dx, dy, dist2)
    if best is None:
        return (0, 0, 0, 0)
    return best


def build_row(record: dict[str, Any], player: int) -> dict[str, int]:
    inputs = record["inputs"]
    applied = inputs.get(f"appliedPlayer{player}", {})
    held = num(applied.get("held"))
    players = record["players"]
    self_player = players[player]
    opponent = players[player ^ 1]
    self_pos = pos(self_player)
    opponent_pos = pos(opponent)
    self_vel = vel(self_player)
    opponent_vel = vel(opponent)
    target = record["targets"]["bigStarActor"]
    if not target.get("found"):
        target = record["targets"]["bigStarCandidate"]
    target_pos = pos(target)
    camera = record.get("camera") or {}
    object_summary = record.get("objectSummary") or {}

    row: dict[str, int] = {
        "frame": num(record.get("frame")),
        "stage_id": num((record.get("stage") or {}).get("id")),
        "stage_group": num((record.get("stage") or {}).get("group")),
        "player": player,
        "in_gameplay": num(record.get("inGameplay")),
        "self_found": num(self_player.get("found")),
        "self_x": self_pos["x"],
        "self_y": self_pos["y"],
        "self_z": self_pos["z"],
        "self_vx": self_vel["x"],
        "self_vy": self_vel["y"],
        "self_vz": self_vel["z"],
        "self_action": num(self_player.get("actionFlag")),
        "self_sub_action": num(self_player.get("subActionFlag")),
        "self_physics": num(self_player.get("physicsFlag")),
        "self_collision": num(self_player.get("collisionFlag")),
        "self_environment": num(self_player.get("environmentFlag")),
        "self_powerup": num(self_player.get("powerup")),
        "self_dead": num(self_player.get("dead")),
        "self_battle_stars": num(self_player.get("battleStars")),
        "self_coins": num(self_player.get("coins")),
        "opponent_found": num(opponent.get("found")),
        "opponent_x": opponent_pos["x"],
        "opponent_y": opponent_pos["y"],
        "opponent_z": opponent_pos["z"],
        "opponent_vx": opponent_vel["x"],
        "opponent_vy": opponent_vel["y"],
        "opponent_vz": opponent_vel["z"],
        "opponent_battle_stars": num(opponent.get("battleStars")),
        "opponent_coins": num(opponent.get("coins")),
        "target_found": num(target.get("found")),
        "target_dx": target_pos["x"] - self_pos["x"],
        "target_dy": target_pos["y"] - self_pos["y"],
        "target_dz": target_pos["z"] - self_pos["z"],
        "camera_x0": num(camera.get("globalX0")),
        "camera_y0": num(camera.get("globalY0")),
        "camera_width0": num(camera.get("width0")),
        "camera_height0": num(camera.get("height0")),
        "object_total": num(object_summary.get("total")),
        "object_active": num(object_summary.get("active")),
        "object_dead": num(object_summary.get("dead")),
        "label_held": held,
    }

    for name, bit in BUTTON_BITS.items():
        row[f"label_{name}"] = 1 if (held & (1 << bit)) else 0

    objects = record.get("objects") or []
    for category in NEAREST_CATEGORIES:
        found, dx, dy, dist2 = nearest_object(objects, category, self_pos)
        prefix = f"nearest_{category}"
        row[f"{prefix}_found"] = found
        row[f"{prefix}_dx"] = dx
        row[f"{prefix}_dy"] = dy
        row[f"{prefix}_dist"] = int(math.isqrt(dist2))

    return row


def iter_records(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="AI play log JSONL")
    parser.add_argument("output", type=Path, help="output CSV")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--min-frame", type=int, default=0)
    parser.add_argument("--require-player-found", action="store_true")
    args = parser.parse_args()

    rows: list[dict[str, int]] = []
    for record in iter_records(args.input):
        if num(record.get("frame")) < args.min_frame:
            continue
        applied = (record.get("inputs") or {}).get(f"appliedPlayer{args.player}", {})
        if not applied.get("valid"):
            continue
        if args.require_player_found and not record["players"][args.player].get("found"):
            continue
        rows.append(build_row(record, args.player))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        args.output.write_text("", encoding="utf-8")
        print("rows=0")
        return 0

    with args.output.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"rows={len(rows)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
