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
    "item",
    "coin",
    "moving_hazard",
    "hazard",
    "enemy_goomba",
    "enemy_koopa",
    "platform",
]

CATEGORY_COUNT_NAMES = [
    "player",
    "big_star_actor",
    "big_star_candidate",
    "world_item",
    "neutral_item",
    "dropped_star_item",
    "item",
    "coin",
    "moving_hazard",
    "hazard",
    "enemy_goomba",
    "enemy_koopa",
    "platform",
    "warp_entrance",
    "item_spawn_effect",
    "camera",
    "stage_scene",
    "stage_fx",
    "stage_actor_manager",
    "stage_controller",
    "mvl_object267",
    "vs_connect",
    "course_select",
    "object",
]

CONTACT_NAMES = [
    "ground",
    "tileGround",
    "hoverTileGround",
    "colliderGround",
    "predictGround",
    "ceiling",
    "pushWall",
    "wallLeft",
    "wallRight",
    "edgeGrab",
    "slipperyGround",
    "water",
    "liquid",
    "submerged",
    "quicksandTop",
    "quicksand",
    "rope",
    "tightrope",
    "ledge",
    "pole",
    "spikesLeft",
    "spikesRight",
    "slowGround",
    "conveyorLeft",
    "conveyorRight",
    "snowyGround",
    "sandyGround",
    "destroyedGround",
    "climbableBottom",
    "climbableTop",
    "destroyedCeiling",
    "wrapLeft",
    "wrapRight",
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


def screen(entity: dict[str, Any], camera: str) -> dict[str, int]:
    value = ((entity.get("screen") or {}).get(camera)) or {}
    return {
        "x": num(value.get("x")),
        "y": num(value.get("y")),
        "inViewX": num(value.get("inViewX")),
        "inView": num(value.get("inView")),
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
    self_screen0 = screen(self_player, "camera0")
    self_screen1 = screen(self_player, "camera1")
    opponent_screen0 = screen(opponent, "camera0")
    opponent_screen1 = screen(opponent, "camera1")
    self_fall = self_player.get("fallRisk") or {}
    opponent_fall = opponent.get("fallRisk") or {}
    target = record["targets"]["bigStarActor"]
    if not target.get("found"):
        target = record["targets"]["bigStarCandidate"]
    target_pos = pos(target)
    camera = record.get("camera") or {}
    object_summary = record.get("objectSummary") or {}
    visual_summary = record.get("visualSummary") or {}
    category_counts = visual_summary.get("categoryCounts") or {}
    nearest_summary = {}
    for nearest_player in visual_summary.get("nearest") or []:
        if num(nearest_player.get("player"), -1) == player:
            nearest_summary = nearest_player.get("categories") or {}
            break

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
        "self_screen0_x": self_screen0["x"],
        "self_screen0_y": self_screen0["y"],
        "self_screen0_in_view_x": self_screen0["inViewX"],
        "self_screen0_in_view": self_screen0["inView"],
        "self_screen1_x": self_screen1["x"],
        "self_screen1_y": self_screen1["y"],
        "self_screen1_in_view_x": self_screen1["inViewX"],
        "self_screen1_in_view": self_screen1["inView"],
        "self_camera_bottom_distance0": num(self_fall.get("cameraBottomDistance0")),
        "self_camera_bottom_distance1": num(self_fall.get("cameraBottomDistance1")),
        "self_near_camera_bottom0": num(self_fall.get("nearCameraBottom0")),
        "self_near_camera_bottom1": num(self_fall.get("nearCameraBottom1")),
        "self_below_camera0": num(self_fall.get("belowCamera0")),
        "self_below_camera1": num(self_fall.get("belowCamera1")),
        "self_vel_y_positive": num(self_fall.get("velYPositive")),
        "self_vel_y_negative": num(self_fall.get("velYNegative")),
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
        "opponent_screen0_x": opponent_screen0["x"],
        "opponent_screen0_y": opponent_screen0["y"],
        "opponent_screen0_in_view_x": opponent_screen0["inViewX"],
        "opponent_screen0_in_view": opponent_screen0["inView"],
        "opponent_screen1_x": opponent_screen1["x"],
        "opponent_screen1_y": opponent_screen1["y"],
        "opponent_screen1_in_view_x": opponent_screen1["inViewX"],
        "opponent_screen1_in_view": opponent_screen1["inView"],
        "opponent_camera_bottom_distance0": num(opponent_fall.get("cameraBottomDistance0")),
        "opponent_camera_bottom_distance1": num(opponent_fall.get("cameraBottomDistance1")),
        "opponent_near_camera_bottom0": num(opponent_fall.get("nearCameraBottom0")),
        "opponent_near_camera_bottom1": num(opponent_fall.get("nearCameraBottom1")),
        "opponent_below_camera0": num(opponent_fall.get("belowCamera0")),
        "opponent_below_camera1": num(opponent_fall.get("belowCamera1")),
        "opponent_vel_y_positive": num(opponent_fall.get("velYPositive")),
        "opponent_vel_y_negative": num(opponent_fall.get("velYNegative")),
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
        "visible_camera0": num(visual_summary.get("visibleCamera0")),
        "visible_camera1": num(visual_summary.get("visibleCamera1")),
        "visible_camera0_x": num(visual_summary.get("visibleCamera0X")),
        "visible_camera1_x": num(visual_summary.get("visibleCamera1X")),
        "object_total": num(object_summary.get("total")),
        "object_active": num(object_summary.get("active")),
        "object_dead": num(object_summary.get("dead")),
        "label_held": held,
    }

    for category in CATEGORY_COUNT_NAMES:
        row[f"count_{category}"] = num(category_counts.get(category))

    for prefix, player_state in [("self", self_player), ("opponent", opponent)]:
        contact = player_state.get("contact") or {}
        for name in CONTACT_NAMES:
            row[f"{prefix}_contact_{name}"] = num(contact.get(name))

    for name, bit in BUTTON_BITS.items():
        row[f"label_{name}"] = 1 if (held & (1 << bit)) else 0

    objects = record.get("objects") or []
    for category in NEAREST_CATEGORIES:
        found, dx, dy, dist2 = nearest_object(objects, category, self_pos)
        summary = nearest_summary.get(category) or {}
        if summary:
            found = num(summary.get("found"))
            dx = num(summary.get("dx"))
            dy = num(summary.get("dy"))
            dist2 = num(summary.get("dist2"))
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
