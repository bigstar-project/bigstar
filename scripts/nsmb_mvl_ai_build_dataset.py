#!/usr/bin/env python3
"""Build a first-pass imitation-learning CSV from NSMB MvL AI play logs."""

from __future__ import annotations

import argparse
import csv
import gzip
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

HORIZONTAL_WRAP_WIDTH = 0x400000
RUNTIME_HAZARD_HORIZONTAL_RANGE = 0x40000
RUNTIME_HAZARD_VERTICAL_RANGE = 0x50000
RUNTIME_HAZARD_CLOSE_RANGE = 0x30000


def wrapped_dx(dx: int, wrap_width: int = HORIZONTAL_WRAP_WIDTH) -> int:
    if wrap_width <= 0:
        return dx
    half = wrap_width // 2
    return ((dx + half) % wrap_width) - half


def delta_x(target_x: int, self_x: int) -> int:
    return wrapped_dx(target_x - self_x)

NEAREST_CATEGORIES = [
    "big_star_actor",
    "world_item",
    "neutral_item",
    "coin_item",
    "dropped_star_item",
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
    "big_star_marker",
    "world_item",
    "neutral_item",
    "coin_item",
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

RUNTIME_HAZARD_CATEGORY_IDS = {
    "moving_hazard": 1,
    "hazard": 2,
    "enemy_goomba": 3,
    "enemy_koopa": 4,
}

ITEM_CATEGORIES = {
    "world_item",
    "neutral_item",
    "coin_item",
    "dropped_star_item",
    "item",
}

# Best-effort item settings interpretation from current stage 0 logs.
# Keep raw settings columns in the dataset because these mappings are not fully verified yet.
ITEM_SETTINGS_FIRE_CANDIDATES = {
    0x00090000,
    0x00011089,
}
ITEM_SETTINGS_COIN_ITEM_CANDIDATES = {
    0x00090002,
}
ITEM_SETTINGS_SUSPECTED_MINI_CANDIDATES = {
    # Observed as an 8-coin reward candidate in human-stage0-item-box-001.
    # Treat as suspected until visually confirmed; training can explicitly avoid it.
    0x0001108B,
}
DROPPED_STAR_ACTOR_SETTINGS_NORMALIZED = {
    0x00001002,
    0x00001012,
    0x00001102,
    0x00001112,
}
COIN_REWARD_ITEM_WINDOW_FRAMES = 480

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

TILE_PROBE_BLOCK_DERIVED_NAMES = [
    "storageBreakableCandidate",
    "hiddenOrRescueCandidate",
    "visibleStorageBreakableCandidate",
    "visibleSolidCandidate",
]

TILE_GRID_WIDTH = 33
TILE_GRID_HEIGHT = 17
TILE_GRID_MIN_REL_X = -16
TILE_GRID_MIN_REL_Y = -10
TILE_GRID_TILE_NAMES = [
    "solid",
    "coin",
    "questionBlock",
    "breakableBlock",
    "brickBlock",
    "slope",
    "scanSolid",
    "water",
    "partialSolid",
    "harmful",
    "invisibleBlock",
    "lowType",
]

TILE_GRID_BLOCK_NAMES = [
    "any",
    "question",
    "breakable",
    "brick",
    "invisible",
    "hiddenOrRescueCandidate",
    "visibleStorageBreakableCandidate",
    "visibleSolidCandidate",
]


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def object_category(obj: dict[str, Any]) -> str:
    category = str(obj.get("category") or "")
    object_id = num(obj.get("objectId"))
    settings = num(obj.get("settings"))
    if object_id == 0x001F and settings == 0x00090002:
        return "coin_item"
    if object_id == 0x0022 and (settings & 0x7FFFFFFF) in DROPPED_STAR_ACTOR_SETTINGS_NORMALIZED:
        return "dropped_star_item"
    if object_id == 0x010C and settings == 0x00001120:
        return "big_star_marker"
    return category


def is_dropped_star_item_candidate(object_id: int, settings: int, category: str) -> int:
    return int(
        category == "dropped_star_item"
        or (object_id == 0x0022 and (settings & 0x7FFFFFFF) in DROPPED_STAR_ACTOR_SETTINGS_NORMALIZED)
    )


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


def by_grid_cell(items: list[dict[str, Any]]) -> dict[tuple[int, int], dict[str, Any]]:
    return {
        (num(item.get("row"), -1), num(item.get("col"), -1)): item
        for item in items
        if isinstance(item, dict)
    }


def tile_sample_solidish(samples: dict[str, dict[str, Any]], name: str) -> int:
    return num((samples.get(name) or {}).get("solidish"))


def recompute_tile_probe_summary(
    tile_probe: dict[str, Any],
    contact: dict[str, Any],
    samples: dict[str, dict[str, Any]],
) -> dict[str, int]:
    found = num(tile_probe.get("found"))
    ground_below = tile_sample_solidish(samples, "below")
    ahead_body = tile_sample_solidish(samples, "aheadBody")
    ahead_feet = tile_sample_solidish(samples, "aheadFeet")
    ahead_below = tile_sample_solidish(samples, "aheadBelow")
    ahead2_below = tile_sample_solidish(samples, "ahead2Below")
    left_body = tile_sample_solidish(samples, "leftBody")
    left_below = tile_sample_solidish(samples, "leftBelow")
    left2_below = tile_sample_solidish(samples, "left2Below")
    right_body = tile_sample_solidish(samples, "rightBody")
    right_below = tile_sample_solidish(samples, "rightBelow")
    right2_below = tile_sample_solidish(samples, "right2Below")
    contact_ground = num(contact.get("ground"))
    contact_wall_left = num(contact.get("wallLeft"))
    contact_wall_right = num(contact.get("wallRight"))

    hole_ahead = int(found and not ahead_below and not ahead2_below)
    hole_left = int(found and not left_below and not left2_below)
    hole_right = int(found and not right_below and not right2_below)
    hole_suppressed = int(contact_ground and not ground_below)

    return {
        "groundBelowSolid": ground_below,
        "aheadBodySolid": ahead_body,
        "aheadFeetSolid": ahead_feet,
        "aheadBelowSolid": ahead_below,
        "ahead2BelowSolid": ahead2_below,
        "wallAhead": int(ahead_body or ahead_feet),
        "holeAhead": hole_ahead,
        "wallLeft": int(left_body or contact_wall_left),
        "holeLeft": hole_left,
        "wallRight": int(right_body or contact_wall_right),
        "holeRight": hole_right,
        "contactGround": contact_ground,
        "effectiveGroundBelowSolid": int(ground_below or contact_ground),
        "holeSuppressedByContact": hole_suppressed,
        "effectiveHoleAhead": int(hole_ahead and not hole_suppressed),
        "effectiveHoleLeft": int(hole_left and not hole_suppressed),
        "effectiveHoleRight": int(hole_right and not hole_suppressed),
    }


def derived_tile_block_features(block: dict[str, Any]) -> dict[str, int]:
    any_block = num(block.get("any"))
    question = num(block.get("question"))
    breakable = num(block.get("breakable"))
    brick = num(block.get("brick"))
    invisible = num(block.get("invisible"))
    storage_contents = num(block.get("storageContents"))
    has_storage = num(block.get("hasStorageContents")) or int(storage_contents != 0)
    storage_breakable = int(any_block and breakable and has_storage)
    visible_storage_breakable = int(storage_breakable and not invisible)
    return {
        "storageBreakableCandidate": storage_breakable,
        "hiddenOrRescueCandidate": int(any_block and invisible),
        "visibleStorageBreakableCandidate": visible_storage_breakable,
        "visibleSolidCandidate": int(any_block and (question or brick or (breakable and not invisible))),
    }


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


def item_powerup_kind_candidate(settings: int) -> int:
    if settings in ITEM_SETTINGS_FIRE_CANDIDATES:
        return 2
    if settings in ITEM_SETTINGS_SUSPECTED_MINI_CANDIDATES:
        return 3
    return -1


def item_avoid_candidate(settings: int) -> int:
    return int(settings in ITEM_SETTINGS_SUSPECTED_MINI_CANDIDATES)


def nearest_object(
    objects: list[dict[str, Any]],
    category: str,
    self_pos: dict[str, int],
) -> tuple[int, int, int, int]:
    best: tuple[int, int, int, int] | None = None
    for obj in objects:
        if object_category(obj) != category:
            continue
        obj_pos = pos(obj)
        dx = delta_x(obj_pos["x"], self_pos["x"])
        dy = obj_pos["y"] - self_pos["y"]
        dist2 = dx * dx + dy * dy
        if best is None or dist2 < best[3]:
            best = (1, dx, dy, dist2)
    if best is None:
        return (0, 0, 0, 0)
    return best


def runtime_hazard_threat(
    objects: list[dict[str, Any]],
    self_pos: dict[str, int],
    self_vel: dict[str, int],
) -> dict[str, int]:
    best: dict[str, int] | None = None
    best_score = 0
    self_vx = self_vel["x"]
    for obj in objects:
        category = object_category(obj)
        category_id = RUNTIME_HAZARD_CATEGORY_IDS.get(category)
        if category_id is None:
            continue
        obj_pos = pos(obj)
        obj_vel = vel(obj)
        dx = delta_x(obj_pos["x"], self_pos["x"])
        dy = obj_pos["y"] - self_pos["y"]
        if abs(dx) > RUNTIME_HAZARD_HORIZONTAL_RANGE or abs(dy) > RUNTIME_HAZARD_VERTICAL_RANGE:
            continue

        rel_vx = obj_vel["x"] - self_vx
        closing = int((dx < 0 and rel_vx > 0) or (dx > 0 and rel_vx < 0))
        very_close = int(abs(dx) <= RUNTIME_HAZARD_CLOSE_RANGE or abs(dy) <= 0x10000)
        score = abs(dx) + abs(dy) * 2
        if closing:
            score -= RUNTIME_HAZARD_HORIZONTAL_RANGE
        if very_close:
            score -= RUNTIME_HAZARD_CLOSE_RANGE

        candidate = {
            "found": 1,
            "closing": closing,
            "very_close": very_close,
            "dx": dx,
            "dy": dy,
            "vx": obj_vel["x"],
            "vy": obj_vel["y"],
            "dist": int(math.isqrt(dx * dx + dy * dy)),
            "category": category_id,
            "object_id": num(obj.get("objectId")),
            "settings": num(obj.get("settings")),
        }
        if best is None or score < best_score:
            best = candidate
            best_score = score

    if best is None:
        return {
            "found": 0,
            "closing": 0,
            "very_close": 0,
            "dx": 0,
            "dy": 0,
            "vx": 0,
            "vy": 0,
            "dist": 0,
            "category": 0,
            "object_id": 0,
            "settings": 0,
        }
    return best


def nearest_item_details(
    objects: list[dict[str, Any]],
    self_pos: dict[str, int],
    *,
    require_item_category: str | None = None,
) -> dict[str, int]:
    best: tuple[int, dict[str, Any], dict[str, int], dict[str, int], dict[str, int]] | None = None
    for obj in objects:
        category = object_category(obj)
        if category not in ITEM_CATEGORIES:
            continue
        if require_item_category is not None and category != require_item_category:
            continue
        obj_pos = pos(obj)
        obj_vel = vel(obj)
        obj_screen = screen(obj, "camera1")
        dx = delta_x(obj_pos["x"], self_pos["x"])
        dy = obj_pos["y"] - self_pos["y"]
        dist2 = dx * dx + dy * dy
        if best is None or dist2 < best[0]:
            best = (dist2, obj, obj_pos, obj_vel, obj_screen)

    if best is None:
        return {
            "found": 0,
            "dx": 0,
            "dy": 0,
            "dist": 0,
            "object_id": 0,
            "settings": 0,
            "settings_low8": 0,
            "vtable": 0,
            "vx": 0,
            "vy": 0,
            "screen_x": 0,
            "screen_y": 0,
            "screen_in_view": 0,
            "powerup_kind_candidate": -1,
            "is_fire_candidate": 0,
            "is_coin_item_candidate": 0,
            "is_dropped_star_candidate": 0,
            "is_suspected_mini_candidate": 0,
            "avoid_candidate": 0,
        }

    dist2, obj, obj_pos, obj_vel, obj_screen = best
    settings = num(obj.get("settings"))
    object_id = num(obj.get("objectId"))
    category = object_category(obj)
    powerup_kind = item_powerup_kind_candidate(settings)
    return {
        "found": 1,
        "dx": delta_x(obj_pos["x"], self_pos["x"]),
        "dy": obj_pos["y"] - self_pos["y"],
        "dist": int(math.isqrt(dist2)),
        "object_id": object_id,
        "settings": settings,
        "settings_low8": settings & 0xFF,
        "vtable": num(obj.get("vtable")),
        "vx": obj_vel["x"],
        "vy": obj_vel["y"],
        "screen_x": obj_screen["x"],
        "screen_y": obj_screen["y"],
        "screen_in_view": obj_screen["inView"],
        "powerup_kind_candidate": powerup_kind,
        "is_fire_candidate": int(settings in ITEM_SETTINGS_FIRE_CANDIDATES),
        "is_coin_item_candidate": int(settings in ITEM_SETTINGS_COIN_ITEM_CANDIDATES),
        "is_dropped_star_candidate": is_dropped_star_item_candidate(object_id, settings, category),
        "is_suspected_mini_candidate": int(settings in ITEM_SETTINGS_SUSPECTED_MINI_CANDIDATES),
        "avoid_candidate": item_avoid_candidate(settings),
    }


def fireball_owner_info(slot: dict[str, Any]) -> tuple[int, int, int]:
    kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
    if kind in (0, 1):
        return kind, 100, 1
    if kind in (2, 3):
        return -1, 100, 1
    return num(slot.get("ownerCandidate"), -1), num(slot.get("ownerConfidence")), num(slot.get("ownerVerified"))


def nearest_special_slot(
    slots: list[dict[str, Any]],
    self_pos: dict[str, int],
    player: int,
) -> tuple[int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int]:
    best: tuple[int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int] | None = None
    for slot in slots:
        slot_pos = pos(slot)
        dx = delta_x(slot_pos["x"], self_pos["x"])
        dy = slot_pos["y"] - self_pos["y"]
        dist2 = dx * dx + dy * dy
        if best is None or dist2 < best[3]:
            owner_candidate, owner_confidence, owner_verified = fireball_owner_info(slot)
            state_bytes = slot.get("stateBytes") or []
            debug_words = slot.get("debugWords") or []
            best = (
                1,
                dx,
                dy,
                dist2,
                num(slot.get("kind")),
                num(slot.get("state")),
                num(slot.get("facing")),
                owner_candidate,
                owner_confidence,
                num(slot.get("ownerHeuristic")),
                int(owner_candidate == player),
                num(slot.get("ownerTracked")),
                num(slot.get("statelessOwnerCandidate"), -1),
                num(slot.get("statelessOwnerConfidence")),
                num(slot.get("statelessOwnerHeuristic")),
                num(state_bytes[2] if len(state_bytes) > 2 else 0),
                num(state_bytes[4] if len(state_bytes) > 4 else 0),
                num(state_bytes[6] if len(state_bytes) > 6 else 0),
                num(debug_words[0] if debug_words else 0),
                owner_verified,
                num(slot.get("sourceKind"), num(slot.get("kind"))),
            )
    if best is None:
        return (0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0)
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
    target_pos = pos(target)
    camera = record.get("camera") or {}
    object_summary = record.get("objectSummary") or {}
    special_objects = record.get("specialObjects") or {}
    fireballs = special_objects.get("fireballs") or {}
    projectiles = special_objects.get("projectiles") or {}
    fireball_slots = fireballs.get("slots") or []
    nearest_fireball = nearest_special_slot(fireball_slots, self_pos, player)
    self_visual = self_player.get("visualState") or {}
    self_visual_powerup = self_visual.get("powerup") or {}
    opponent_visual = opponent.get("visualState") or {}
    opponent_visual_powerup = opponent_visual.get("powerup") or {}
    visual_summary = record.get("visualSummary") or {}
    objects = record.get("objects") or []
    category_counts: dict[str, int] = {}
    for obj in objects:
        category = object_category(obj)
        category_counts[category] = category_counts.get(category, 0) + 1
    nearest_item = nearest_item_details(objects, self_pos)
    coin_reward_item = nearest_item_details(objects, self_pos, require_item_category="item")
    runtime_hazard = runtime_hazard_threat(objects, self_pos, self_vel)
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
        "self_inventory_powerup": num(self_player.get("inventoryPowerup")),
        "self_damage_cooldown": num(self_player.get("damageCooldown")),
        "self_has_reserve_item_candidate": num(self_visual.get("hasReserveItemCandidate")),
        "self_can_shoot_fire_candidate": num(self_visual_powerup.get("canShootFireCandidate")),
        "self_visual_powerup_kind_candidate": num(self_visual.get("visualPowerupKindCandidate")),
        "self_visual_powerup_source_mask": num(self_visual.get("visualPowerupSourceMask")),
        "self_is_fire_visual_candidate": num(self_visual.get("isFireVisualCandidate")),
        "self_can_shoot_fire_visual_candidate": num(self_visual.get("canShootFireVisualCandidate")),
        "self_is_shell_candidate": num(self_visual_powerup.get("isShellCandidate")),
        "self_is_mega_candidate": num(self_visual_powerup.get("isMegaCandidate")),
        "self_actor_powerup_state": num(self_visual.get("actorPowerupState")),
        "self_actor_powerup_form_state": num(self_visual.get("actorPowerupFormState")),
        "self_actor_powerup_aux_state": num(self_visual.get("actorPowerupAuxState")),
        "self_actor_powerup_sub_state": num(self_visual.get("actorPowerupSubState")),
        "self_damage_state": num(self_visual.get("damageState")),
        "self_damage_guard_flag": num(self_visual.get("damageGuardFlag")),
        "self_damage_guard_timer": num(self_visual.get("damageGuardTimer")),
        "self_damage_physics_guard": num(self_visual.get("damagePhysicsGuard")),
        "self_powerup_apply_lock": num(self_visual.get("powerupApplyLock")),
        "self_shell_state": num(self_visual.get("shellState")),
        "self_invincible_known": num(self_visual.get("invincibleKnown")),
        "self_invincible_candidate": num(self_visual.get("invincibleCandidate")),
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
        "opponent_action": num(opponent.get("actionFlag")),
        "opponent_sub_action": num(opponent.get("subActionFlag")),
        "opponent_physics": num(opponent.get("physicsFlag")),
        "opponent_collision": num(opponent.get("collisionFlag")),
        "opponent_environment": num(opponent.get("environmentFlag")),
        "opponent_powerup": num(opponent.get("powerup")),
        "opponent_inventory_powerup": num(opponent.get("inventoryPowerup")),
        "opponent_damage_cooldown": num(opponent.get("damageCooldown")),
        "opponent_has_reserve_item_candidate": num(opponent_visual.get("hasReserveItemCandidate")),
        "opponent_can_shoot_fire_candidate": num(opponent_visual_powerup.get("canShootFireCandidate")),
        "opponent_visual_powerup_kind_candidate": num(opponent_visual.get("visualPowerupKindCandidate")),
        "opponent_visual_powerup_source_mask": num(opponent_visual.get("visualPowerupSourceMask")),
        "opponent_is_fire_visual_candidate": num(opponent_visual.get("isFireVisualCandidate")),
        "opponent_can_shoot_fire_visual_candidate": num(opponent_visual.get("canShootFireVisualCandidate")),
        "opponent_is_shell_candidate": num(opponent_visual_powerup.get("isShellCandidate")),
        "opponent_is_mega_candidate": num(opponent_visual_powerup.get("isMegaCandidate")),
        "opponent_actor_powerup_state": num(opponent_visual.get("actorPowerupState")),
        "opponent_actor_powerup_form_state": num(opponent_visual.get("actorPowerupFormState")),
        "opponent_actor_powerup_aux_state": num(opponent_visual.get("actorPowerupAuxState")),
        "opponent_actor_powerup_sub_state": num(opponent_visual.get("actorPowerupSubState")),
        "opponent_damage_state": num(opponent_visual.get("damageState")),
        "opponent_damage_guard_flag": num(opponent_visual.get("damageGuardFlag")),
        "opponent_damage_guard_timer": num(opponent_visual.get("damageGuardTimer")),
        "opponent_damage_physics_guard": num(opponent_visual.get("damagePhysicsGuard")),
        "opponent_powerup_apply_lock": num(opponent_visual.get("powerupApplyLock")),
        "opponent_shell_state": num(opponent_visual.get("shellState")),
        "opponent_invincible_known": num(opponent_visual.get("invincibleKnown")),
        "opponent_invincible_candidate": num(opponent_visual.get("invincibleCandidate")),
        "opponent_dead": num(opponent.get("dead")),
        "opponent_battle_stars": num(opponent.get("battleStars")),
        "opponent_coins": num(opponent.get("coins")),
        "self_prev_coins": num(record.get("_self_prev_coins"), -1),
        "self_coin_reward_recent": num(record.get("_self_coin_reward_recent")),
        "self_coin_reward_age": num(record.get("_self_coin_reward_age"), -1),
        "nearest_item_found": nearest_item["found"],
        "nearest_item_dx": nearest_item["dx"],
        "nearest_item_dy": nearest_item["dy"],
        "nearest_item_dist": nearest_item["dist"],
        "nearest_item_object_id": nearest_item["object_id"],
        "nearest_item_settings": nearest_item["settings"],
        "nearest_item_settings_low8": nearest_item["settings_low8"],
        "nearest_item_vtable": nearest_item["vtable"],
        "nearest_item_vx": nearest_item["vx"],
        "nearest_item_vy": nearest_item["vy"],
        "nearest_item_screen1_x": nearest_item["screen_x"],
        "nearest_item_screen1_y": nearest_item["screen_y"],
        "nearest_item_screen1_in_view": nearest_item["screen_in_view"],
        "nearest_item_powerup_kind_candidate": nearest_item["powerup_kind_candidate"],
        "nearest_item_is_fire_candidate": nearest_item["is_fire_candidate"],
        "nearest_item_is_coin_item_candidate": nearest_item["is_coin_item_candidate"],
        "nearest_item_is_dropped_star_candidate": nearest_item["is_dropped_star_candidate"],
        "nearest_item_is_suspected_mini_candidate": nearest_item["is_suspected_mini_candidate"],
        "nearest_item_avoid_candidate": nearest_item["avoid_candidate"],
        "runtime_hazard_found": runtime_hazard["found"],
        "runtime_hazard_closing": runtime_hazard["closing"],
        "runtime_hazard_very_close": runtime_hazard["very_close"],
        "runtime_hazard_dx": runtime_hazard["dx"],
        "runtime_hazard_dy": runtime_hazard["dy"],
        "runtime_hazard_vx": runtime_hazard["vx"],
        "runtime_hazard_vy": runtime_hazard["vy"],
        "runtime_hazard_dist": runtime_hazard["dist"],
        "runtime_hazard_category": runtime_hazard["category"],
        "runtime_hazard_object_id": runtime_hazard["object_id"],
        "runtime_hazard_settings": runtime_hazard["settings"],
        "coin_reward_item_found": coin_reward_item["found"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_dx": coin_reward_item["dx"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_dy": coin_reward_item["dy"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_dist": coin_reward_item["dist"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_object_id": coin_reward_item["object_id"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_settings": coin_reward_item["settings"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_settings_low8": coin_reward_item["settings_low8"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_vtable": coin_reward_item["vtable"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_vx": coin_reward_item["vx"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_vy": coin_reward_item["vy"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_screen1_x": coin_reward_item["screen_x"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_screen1_y": coin_reward_item["screen_y"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_screen1_in_view": coin_reward_item["screen_in_view"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_powerup_kind_candidate": coin_reward_item["powerup_kind_candidate"] if num(record.get("_self_coin_reward_recent")) else -1,
        "coin_reward_item_is_fire_candidate": coin_reward_item["is_fire_candidate"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_is_coin_item_candidate": coin_reward_item["is_coin_item_candidate"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_is_dropped_star_candidate": coin_reward_item["is_dropped_star_candidate"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_is_suspected_mini_candidate": coin_reward_item["is_suspected_mini_candidate"] if num(record.get("_self_coin_reward_recent")) else 0,
        "coin_reward_item_avoid_candidate": coin_reward_item["avoid_candidate"] if num(record.get("_self_coin_reward_recent")) else 0,
        "target_found": num(target.get("found")),
        "target_dx": delta_x(target_pos["x"], self_pos["x"]),
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
        "fireballs_active_slots": num(fireballs.get("activeSlots")),
        "fireballs_slot_count": len(fireball_slots),
        "fireballs_handler_word0": num((fireballs.get("words") or [0])[0]),
        "nearest_fireball_found": nearest_fireball[0],
        "nearest_fireball_dx": nearest_fireball[1],
        "nearest_fireball_dy": nearest_fireball[2],
        "nearest_fireball_dist2": nearest_fireball[3],
        "nearest_fireball_kind": nearest_fireball[4],
        "nearest_fireball_state": nearest_fireball[5],
        "nearest_fireball_facing": nearest_fireball[6],
        "nearest_fireball_owner_candidate": nearest_fireball[7],
        "nearest_fireball_owner_confidence": nearest_fireball[8],
        "nearest_fireball_owner_heuristic": nearest_fireball[9],
        "nearest_fireball_owned_by_self_candidate": nearest_fireball[10],
        "nearest_fireball_owner_tracked": nearest_fireball[11],
        "nearest_fireball_stateless_owner_candidate": nearest_fireball[12],
        "nearest_fireball_stateless_owner_confidence": nearest_fireball[13],
        "nearest_fireball_stateless_owner_heuristic": nearest_fireball[14],
        "nearest_fireball_state_byte82": nearest_fireball[15],
        "nearest_fireball_state_byte84": nearest_fireball[16],
        "nearest_fireball_state_byte86": nearest_fireball[17],
        "nearest_fireball_debug_word0": nearest_fireball[18],
        "nearest_fireball_owner_verified": nearest_fireball[19],
        "nearest_fireball_source_kind": nearest_fireball[20],
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

        raw_tile_probe_summary = tile_probe.get("summary") or {}
        tile_probe_samples = by_name(tile_probe.get("samples") or [])
        tile_probe_summary = recompute_tile_probe_summary(tile_probe, contact, tile_probe_samples)
        row[f"{prefix}_tile_probe_found"] = num(tile_probe.get("found"))
        row[f"{prefix}_tile_probe_direction"] = num(tile_probe.get("direction"), 1)
        for name in TILE_PROBE_SUMMARY_NAMES:
            row[f"{prefix}_tile_probe_{name}"] = num(tile_probe_summary.get(name))
            row[f"{prefix}_tile_probe_raw_{name}"] = num(raw_tile_probe_summary.get(name))
            row[f"{prefix}_tile_probe_{name}_recomputed_diff"] = int(
                num(tile_probe_summary.get(name)) != num(raw_tile_probe_summary.get(name))
            )
        for sample_name in TILE_PROBE_SAMPLE_NAMES:
            sample = tile_probe_samples.get(sample_name) or {}
            tile = sample.get("tile") or {}
            block = sample.get("block") or {}
            derived_block = derived_tile_block_features(block)
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
            for name in TILE_PROBE_BLOCK_DERIVED_NAMES:
                row[f"{sample_prefix}_block_{name}"] = num(derived_block.get(name))
        tile_grid_info = tile_probe.get("grid") or {}
        tile_grid = by_grid_cell((tile_grid_info.get("cells")) or [])
        sparse_grid = str(tile_grid_info.get("encoding") or "") == "sparse_non_empty"
        omitted_grid_found = num(tile_grid_info.get("omittedCellFound"), 1 if sparse_grid else 0)
        omitted_grid_status = num(tile_grid_info.get("omittedCellStatus"), 0)
        for grid_row in range(TILE_GRID_HEIGHT):
            for grid_col in range(TILE_GRID_WIDTH):
                cell = tile_grid.get((grid_row, grid_col))
                omitted_cell = cell is None and sparse_grid
                if cell is None:
                    cell = {}
                tile = cell.get("tile") or {}
                block = cell.get("block") or {}
                derived_block = derived_tile_block_features(block)
                cell_prefix = f"{prefix}_tile_probe_grid_r{grid_row}_c{grid_col}"
                row[f"{cell_prefix}_found"] = omitted_grid_found if omitted_cell else num(cell.get("found"))
                row[f"{cell_prefix}_status"] = omitted_grid_status if omitted_cell else num(cell.get("status"))
                row[f"{cell_prefix}_rel_tile_x"] = num(cell.get("relTileX"), TILE_GRID_MIN_REL_X + grid_col)
                row[f"{cell_prefix}_rel_tile_y"] = num(cell.get("relTileY"), TILE_GRID_MIN_REL_Y + grid_row)
                row[f"{cell_prefix}_tile_x"] = num(cell.get("tileX"))
                row[f"{cell_prefix}_tile_y"] = num(cell.get("tileY"))
                row[f"{cell_prefix}_tile_id"] = num(cell.get("tileId"))
                row[f"{cell_prefix}_behavior"] = num(cell.get("behavior"))
                row[f"{cell_prefix}_solidish"] = num(cell.get("solidish"))
                for name in TILE_GRID_TILE_NAMES:
                    row[f"{cell_prefix}_{name}"] = num(tile.get(name))
                for name in TILE_GRID_BLOCK_NAMES:
                    row[f"{cell_prefix}_block_{name}"] = num(
                        block.get(name, derived_block.get(name))
                    )

    for name, bit in BUTTON_BITS.items():
        row[f"label_{name}"] = 1 if (held & (1 << bit)) else 0

    for category in NEAREST_CATEGORIES:
        found, dx, dy, dist2 = nearest_object(objects, category, self_pos)
        summary = nearest_summary.get(category) or {}
        if summary:
            found = num(summary.get("found"))
            dx = wrapped_dx(num(summary.get("dx")))
            dy = num(summary.get("dy"))
            dist2 = num(summary.get("dist2"))
        prefix = f"nearest_{category}"
        row[f"{prefix}_found"] = found
        row[f"{prefix}_dx"] = dx
        row[f"{prefix}_dy"] = dy
        row[f"{prefix}_dist"] = int(math.isqrt(dist2))

    return row


def iter_records(path: Path) -> Any:
    opener = gzip.open if path.name.lower().endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8-sig") as f:
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
        previous_self_coins: int | None = None
        coin_reward_frame: int | None = None
        for record in iter_records(playlog_path):
            if num(record.get("frame")) < args.min_frame:
                continue
            players = record.get("players") or []
            player_record = players[args.player] if len(players) > args.player else {}
            frame = num(record.get("frame"))
            self_coins = num(player_record.get("coins"), -1)
            if previous_self_coins == 7 and self_coins == 0:
                coin_reward_frame = frame
            reward_age = frame - coin_reward_frame if coin_reward_frame is not None else -1
            reward_recent = (
                coin_reward_frame is not None
                and 0 <= reward_age <= COIN_REWARD_ITEM_WINDOW_FRAMES
            )
            record["_self_prev_coins"] = previous_self_coins if previous_self_coins is not None else -1
            record["_self_coin_reward_recent"] = int(reward_recent)
            record["_self_coin_reward_age"] = reward_age if reward_recent else -1
            previous_self_coins = self_coins

            if args.require_player_found and not player_record.get("found"):
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
