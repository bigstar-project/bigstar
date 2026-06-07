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
    "big_star_related",
    "big_star_candidate",
    "world_item",
    "neutral_item",
    "dropped_star_item",
    "item",
    "coin",
    "moving_hazard",
    "hazard",
    "projectile",
    "player_fireball",
    "enemy_fireball",
    "enemy_goomba",
    "enemy_koopa",
    "platform",
]

CATEGORY_COUNT_NAMES = [
    "player",
    "big_star_actor",
    "big_star_related",
    "big_star_candidate",
    "world_item",
    "neutral_item",
    "dropped_star_item",
    "item",
    "coin",
    "moving_hazard",
    "hazard",
    "projectile",
    "player_fireball",
    "enemy_fireball",
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
    "stage_layout",
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

BOTTOM_TILE_NAMES = [
    "solid",
    "coin",
    "questionBlock",
    "breakableBlock",
    "brickBlock",
    "slope",
    "ceilingSlope",
    "scanSolid",
    "entrance",
    "water",
    "climbable",
    "partialSolid",
    "harmful",
    "invisibleBlock",
    "solidOnBottom",
    "solidOnTop",
]

TILE_NUMERIC_NAMES = [
    "modifier",
    "lowType",
    "storageContents",
]

TILE_PROBE_SAMPLE_NAMES = [
    "center",
    "feet",
    "below",
    "aheadBody",
    "aheadFeet",
    "aheadBelow",
    "ahead2Feet",
    "ahead2Below",
    "above",
    "leftBody",
    "leftFeet",
    "leftBelow",
    "left2Below",
    "rightBody",
    "rightFeet",
    "rightBelow",
    "right2Below",
]

TILE_PROBE_SUMMARY_NAMES = [
    "groundBelowSolid",
    "aheadBodySolid",
    "aheadFeetSolid",
    "aheadBelowSolid",
    "ahead2BelowSolid",
    "wallAhead",
    "holeAhead",
    "wallLeft",
    "holeLeft",
    "wallRight",
    "holeRight",
    "contactGround",
    "effectiveGroundBelowSolid",
    "holeSuppressedByContact",
    "effectiveHoleAhead",
    "effectiveHoleLeft",
    "effectiveHoleRight",
]

TILE_PROBE_BLOCK_NAMES = [
    "any",
    "itemBox",
    "question",
    "breakable",
    "brick",
    "invisible",
    "hasStorageContents",
    "storageContents",
    "modifier",
    "currentTileId",
    "currentBehavior",
]


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def sane_bottom_tile(tile_type: int) -> bool:
    category_masks = [
        0x00010000,
        0x00020000,
        0x00040000,
        0x00080000,
        0x00100000,
        0x00200000,
        0x00400000,
        0x01000000,
        0x02000000,
        0x04000000,
        0x08000000,
        0x10000000,
        0x20000000,
        0x40000000,
        0x80000000,
    ]
    set_categories = sum(1 for mask in category_masks if tile_type & mask)
    return set_categories <= 4


def by_name(items: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    return {str(item.get("name")): item for item in items if isinstance(item, dict)}


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
        "inViewY": num(value.get("inViewY")),
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


def label_held(record: dict[str, Any], player: int, source: str) -> int | None:
    inputs = record["inputs"]
    applied = inputs.get(f"appliedPlayer{player}", {})
    if source == "applied":
        return num(applied.get("held")) if applied.get("valid") else None
    if source == "player":
        return num((inputs.get(f"player{player}") or {}).get("held"))
    if source == "console":
        return num((inputs.get(f"console{player}") or {}).get("held"))
    if applied.get("valid"):
        return num(applied.get("held"))
    return num((inputs.get(f"player{player}") or {}).get("held"))


def build_row(record: dict[str, Any], player: int, label_source: str) -> dict[str, int]:
    held = label_held(record, player, label_source)
    if held is None:
        raise ValueError("record has no usable label")
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
    special_objects = record.get("specialObjects") or {}
    fireballs = special_objects.get("fireballs") or {}
    projectiles = special_objects.get("projectiles") or {}
    visual_summary = record.get("visualSummary") or {}
    category_counts = visual_summary.get("categoryCounts") or {}
    nearest_summary = {}
    for nearest_player in visual_summary.get("nearest") or []:
        if num(nearest_player.get("player"), -1) == player:
            nearest_summary = nearest_player.get("categories") or {}
            break

    row: dict[str, int] = {
        "recording_index": num(record.get("_recording_index")),
        "recording_frame_index": num(record.get("_recording_frame_index")),
        "frame": num(record.get("frame")),
        "stage_id": num((record.get("stage") or {}).get("id")),
        "stage_group": num((record.get("stage") or {}).get("group")),
        "player": player,
        "label_source": {"auto": 0, "applied": 1, "player": 2, "console": 3}[label_source],
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
        "self_screen0_in_view_y": self_screen0["inViewY"],
        "self_screen0_in_view": self_screen0["inView"],
        "self_screen1_x": self_screen1["x"],
        "self_screen1_y": self_screen1["y"],
        "self_screen1_in_view_x": self_screen1["inViewX"],
        "self_screen1_in_view_y": self_screen1["inViewY"],
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
        "opponent_screen0_in_view_y": opponent_screen0["inViewY"],
        "opponent_screen0_in_view": opponent_screen0["inView"],
        "opponent_screen1_x": opponent_screen1["x"],
        "opponent_screen1_y": opponent_screen1["y"],
        "opponent_screen1_in_view_x": opponent_screen1["inViewX"],
        "opponent_screen1_in_view_y": opponent_screen1["inViewY"],
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
        "fireballs_active": num(fireballs.get("active")),
        "fireballs_handler_word0": num((fireballs.get("words") or [0])[0]),
        "projectiles_handler_word0": num((projectiles.get("words") or [0])[0]),
        "label_held": held,
    }

    for category in CATEGORY_COUNT_NAMES:
        row[f"count_{category}"] = num(category_counts.get(category))

    for prefix, player_state in [("self", self_player), ("opponent", opponent)]:
        contact = player_state.get("contact") or {}
        for name in CONTACT_NAMES:
            row[f"{prefix}_contact_{name}"] = num(contact.get(name))
        collision_mgr = player_state.get("collisionMgr") or {}
        tile_probe = player_state.get("tileProbe") or {}
        tile_damage = player_state.get("tileDamage") or {}
        bottom_tile_type = num(
            collision_mgr.get("bottomModifierTileType", collision_mgr.get("bottomTileType"))
        )
        bottom_tile = (
            collision_mgr.get("bottomModifierTile")
            or collision_mgr.get("bottomTile")
            or {}
        )
        row[f"{prefix}_collision_mgr_found"] = num(collision_mgr.get("found"))
        row[f"{prefix}_collision_mgr_collision_result"] = num(collision_mgr.get("collisionResult"))
        row[f"{prefix}_collision_mgr_ground_collision"] = num(collision_mgr.get("groundCollision"))
        row[f"{prefix}_collision_mgr_delta_x"] = num(collision_mgr.get("deltaX"))
        row[f"{prefix}_collision_mgr_delta_y"] = num(collision_mgr.get("deltaY"))
        row[f"{prefix}_collision_mgr_bottom_modifier_tile_type"] = bottom_tile_type
        bottom_tile_sane = sane_bottom_tile(bottom_tile_type)
        row[f"{prefix}_collision_mgr_bottom_modifier_tile_sane"] = int(bottom_tile_sane)
        row[f"{prefix}_collision_mgr_attached_tile_x"] = num(collision_mgr.get("attachedTileX"))
        row[f"{prefix}_collision_mgr_attached_tile_y"] = num(collision_mgr.get("attachedTileY"))
        row[f"{prefix}_collision_mgr_top_modifier_tile_type"] = num(collision_mgr.get("topModifierTileType"))
        row[f"{prefix}_collision_mgr_side_modifier_tile_type_left"] = num(
            collision_mgr.get("sideModifierTileTypeLeft")
        )
        row[f"{prefix}_collision_mgr_side_modifier_tile_type_right"] = num(
            collision_mgr.get("sideModifierTileTypeRight")
        )
        row[f"{prefix}_collision_mgr_bottom_slope_type"] = num(collision_mgr.get("bottomSlopeType"))
        row[f"{prefix}_collision_mgr_top_slope_type"] = num(collision_mgr.get("topSlopeType"))
        row[f"{prefix}_collision_mgr_flags_a8"] = num(collision_mgr.get("flagsA8"))
        row[f"{prefix}_collision_mgr_tile_byte_ab"] = num(collision_mgr.get("tileByteAB"))
        row[f"{prefix}_collision_mgr_modifier_state"] = num(collision_mgr.get("modifierState"))
        row[f"{prefix}_tile_damage_flags"] = num(tile_damage.get("flags"))
        row[f"{prefix}_tile_damage_type"] = num(tile_damage.get("type"))
        row[f"{prefix}_tile_damage_active"] = num(tile_damage.get("active"))
        for name in BOTTOM_TILE_NAMES:
            row[f"{prefix}_bottom_modifier_tile_{name}"] = (
                num(bottom_tile.get(name)) if bottom_tile_sane else 0
            )
        for name in TILE_NUMERIC_NAMES:
            row[f"{prefix}_bottom_modifier_tile_{name}"] = (
                num(bottom_tile.get(name)) if bottom_tile_sane else 0
            )

        tile_probe_summary = tile_probe.get("summary") or {}
        row[f"{prefix}_tile_probe_found"] = num(tile_probe.get("found"))
        row[f"{prefix}_tile_probe_direction"] = num(tile_probe.get("direction"), 1)
        for name in TILE_PROBE_SUMMARY_NAMES:
            row[f"{prefix}_tile_probe_{name}"] = num(tile_probe_summary.get(name))
        tile_probe_samples = by_name(tile_probe.get("samples") or [])
        for sample_name in TILE_PROBE_SAMPLE_NAMES:
            sample = tile_probe_samples.get(sample_name) or {}
            tile = sample.get("tile") or {}
            block = sample.get("block") or {}
            sample_prefix = f"{prefix}_tile_probe_{sample_name}"
            behavior = num(sample.get("behavior"))
            row[f"{sample_prefix}_found"] = num(sample.get("found"))
            row[f"{sample_prefix}_status"] = num(sample.get("status"))
            row[f"{sample_prefix}_tile_id"] = num(sample.get("tileId"))
            row[f"{sample_prefix}_behavior"] = behavior
            row[f"{sample_prefix}_solidish"] = num(sample.get("solidish"))
            row[f"{sample_prefix}_pixel_x"] = num(sample.get("pixelX"))
            row[f"{sample_prefix}_pixel_y"] = num(sample.get("pixelY"))
            for name in BOTTOM_TILE_NAMES:
                row[f"{sample_prefix}_{name}"] = num(tile.get(name))
            for name in TILE_NUMERIC_NAMES:
                row[f"{sample_prefix}_{name}"] = num(tile.get(name))
            for name in TILE_PROBE_BLOCK_NAMES:
                row[f"{sample_prefix}_block_{name}"] = num(block.get(name))

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


def manifest_playlog_paths(path: Path) -> list[Path]:
    with path.open("r", encoding="utf-8") as f:
        manifest = json.load(f)

    base = path.parent
    if isinstance(manifest, dict) and "recordings" in manifest:
        results: list[Path] = []
        for item in manifest.get("recordings") or []:
            if not isinstance(item, dict):
                continue
            recording_path = item.get("manifest") or item.get("path")
            if not recording_path:
                continue
            nested = Path(str(recording_path))
            if not nested.is_absolute():
                nested = base / nested
            results.extend(manifest_playlog_paths(nested))
        return results

    if isinstance(manifest, dict):
        playlog = (
            manifest.get("playLog")
            or manifest.get("playLogPath")
            or manifest.get("aiPlayLog")
            or manifest.get("aiPlayLogPath")
        )
        if playlog:
            playlog_path = Path(str(playlog))
            if not playlog_path.is_absolute():
                playlog_path = base / playlog_path
            return [playlog_path]

    raise ValueError(f"{path}: manifest does not contain playLog/playLogPath or recordings")


def input_playlog_paths(path: Path) -> list[Path]:
    if path.suffix.lower() in {".json", ".manifest"}:
        return manifest_playlog_paths(path)
    return [path]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "input",
        type=Path,
        help="AI play log JSONL, recording manifest JSON, or recordings-index JSON",
    )
    parser.add_argument("output", type=Path, help="output CSV")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument(
        "--label-source",
        choices=["auto", "applied", "player", "console"],
        default="auto",
        help=(
            "input label source: auto uses appliedPlayerN when present and playerN otherwise; "
            "player/console are useful for human play logs"
        ),
    )
    parser.add_argument("--min-frame", type=int, default=0)
    parser.add_argument("--require-player-found", action="store_true")
    args = parser.parse_args()

    rows: list[dict[str, int]] = []
    for recording_index, playlog_path in enumerate(input_playlog_paths(args.input)):
        recording_frame_index = 0
        for record in iter_records(playlog_path):
            if num(record.get("frame")) < args.min_frame:
                continue
            if args.require_player_found and not record["players"][args.player].get("found"):
                continue
            if label_held(record, args.player, args.label_source) is None:
                continue
            record["_recording_index"] = recording_index
            record["_recording_frame_index"] = recording_frame_index
            rows.append(build_row(record, args.player, args.label_source))
            recording_frame_index += 1

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
