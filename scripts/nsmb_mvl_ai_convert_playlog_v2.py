#!/usr/bin/env python3
"""Convert NSMB MvL AI play log v1 records into compact observation v2 JSONL."""

from __future__ import annotations

import argparse
import gzip
import json
import math
from pathlib import Path
from typing import Any, TextIO

import nsmb_mvl_ai_build_dataset as legacy


TERRAIN_CHANNELS = [
    "solid",
    "coin",
    "question",
    "breakable",
    "brick",
    "slope",
    "scanSolid",
    "entrance",
    "water",
    "partialSolid",
    "harmful",
    "invisible",
    "itemBox",
    "hiddenOrRescue",
    "visibleStorageBreakable",
    "visibleSolid",
]

ENTITY_CATEGORY_IDS = {
    name: index
    for index, name in enumerate(
        [
            "unknown",
            "player",
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
    )
}

ALLOWED_BUTTON_BITS = {
    "up": legacy.BUTTON_BITS["up"],
    "down": legacy.BUTTON_BITS["down"],
    "left": legacy.BUTTON_BITS["left"],
    "right": legacy.BUTTON_BITS["right"],
    "y": legacy.BUTTON_BITS["y"],
    "b": legacy.BUTTON_BITS["b"],
}

ALLOWED_HELD_MASK = sum(1 << bit for bit in ALLOWED_BUTTON_BITS.values())

ACTION_HEADS = ["horizontal", "vertical", "jump", "run", "fire"]

ACTION_CLASSES = {
    "horizontal": ["neutral", "left", "right"],
    "vertical": ["neutral", "up", "down"],
    "jump": ["none", "press", "hold"],
    "run": ["off", "on"],
    "fire": ["off", "press"],
}

COMPACT_SCALAR_TILE_KEYS = [
    "groundBelowSolid",
    "blockedAhead",
    "blockedLeft",
    "blockedRight",
    "effectiveHoleAhead",
    "effectiveHoleLeft",
    "effectiveHoleRight",
]


def open_text(path: Path, mode: str) -> TextIO:
    if path.name.lower().endswith(".gz"):
        return gzip.open(path, mode + "t", encoding="utf-8")
    return path.open(mode, encoding="utf-8")


def button_labels(held: int) -> dict[str, int]:
    return {
        name: 1 if (held & (1 << bit)) else 0
        for name, bit in ALLOWED_BUTTON_BITS.items()
    }


def label_pressed(record: dict[str, Any], player: int, source: str) -> int:
    inputs = record.get("inputs") or {}
    applied = inputs.get(f"appliedPlayer{player}") or {}
    player_input = inputs.get(f"player{player}") or {}
    console_input = inputs.get(f"console{player}") or {}
    fallback_pressed = legacy.num(
        player_input.get("pressed"),
        legacy.num(console_input.get("pressed")),
    )
    if source == "applied":
        if applied.get("valid") and "pressed" in applied:
            return legacy.num(applied.get("pressed"))
        return fallback_pressed if applied.get("valid") else 0
    if source == "player":
        return legacy.num(player_input.get("pressed"))
    if source == "console":
        return legacy.num(console_input.get("pressed"))
    if applied.get("valid") and "pressed" in applied:
        return legacy.num(applied.get("pressed"))
    return fallback_pressed


def fire_capable(record: dict[str, Any], player: int) -> bool:
    players = record.get("players") or []
    player_record = players[player] if len(players) > player else {}
    visual = player_record.get("visualState") or {}
    return bool(
        legacy.num(visual.get("canShootFireVisualCandidate"))
        or legacy.num(visual.get("isFireVisualCandidate"))
        or legacy.num(visual.get("visualPowerupKindCandidate")) == 2
    )


def action_labels(held: int, pressed: int, *, can_fire: bool) -> dict[str, Any]:
    left = bool(held & (1 << legacy.BUTTON_BITS["left"]))
    right = bool(held & (1 << legacy.BUTTON_BITS["right"]))
    up = bool(held & (1 << legacy.BUTTON_BITS["up"]))
    down = bool(held & (1 << legacy.BUTTON_BITS["down"]))
    y_held = bool(held & (1 << legacy.BUTTON_BITS["y"]))
    b_held = bool(held & (1 << legacy.BUTTON_BITS["b"]))
    y_pressed = bool(pressed & (1 << legacy.BUTTON_BITS["y"]))
    b_pressed = bool(pressed & (1 << legacy.BUTTON_BITS["b"]))

    horizontal_id = 1 if left and not right else 2 if right and not left else 0
    vertical_id = 1 if up and not down else 2 if down and not up else 0
    jump_id = 1 if b_pressed else 2 if b_held else 0
    fire_id = 1 if can_fire and y_pressed else 0
    run_id = 1 if y_held else 0

    return {
        "horizontal": ACTION_CLASSES["horizontal"][horizontal_id],
        "horizontalId": horizontal_id,
        "vertical": ACTION_CLASSES["vertical"][vertical_id],
        "verticalId": vertical_id,
        "jump": ACTION_CLASSES["jump"][jump_id],
        "jumpId": jump_id,
        "run": ACTION_CLASSES["run"][run_id],
        "runId": run_id,
        "fire": ACTION_CLASSES["fire"][fire_id],
        "fireId": fire_id,
    }


def player_label(record: dict[str, Any], player: int, source: str) -> dict[str, Any]:
    held = legacy.label_held(record, player, source)
    if held is None:
        return {
            "valid": 0,
            "held": 0,
            "pressed": 0,
            "allowedHeld": 0,
            "buttons": button_labels(0),
            "actions": action_labels(0, 0, can_fire=False),
            "source": source,
        }
    pressed = label_pressed(record, player, source)
    return {
        "valid": 1,
        "held": held,
        "pressed": pressed,
        "allowedHeld": held & ALLOWED_HELD_MASK,
        "buttons": button_labels(held),
        "actions": action_labels(held, pressed, can_fire=fire_capable(record, player)),
        "source": source,
    }



def terrain_mask(cell: dict[str, Any]) -> int:
    tile = cell.get("tile") or {}
    block = cell.get("block") or {}
    derived = legacy.derived_tile_block_features(block)
    values = {
        "solid": legacy.num(cell.get("solidish")) or legacy.num(tile.get("solid")),
        "coin": legacy.num(tile.get("coin")),
        "question": legacy.num(tile.get("questionBlock")),
        "breakable": legacy.num(tile.get("breakableBlock")),
        "brick": legacy.num(tile.get("brickBlock")),
        "slope": legacy.num(tile.get("slope")),
        "scanSolid": legacy.num(tile.get("scanSolid")),
        "entrance": legacy.num(tile.get("entrance")),
        "water": legacy.num(tile.get("water")),
        "partialSolid": legacy.num(tile.get("partialSolid")),
        "harmful": legacy.num(tile.get("harmful")),
        "invisible": legacy.num(tile.get("invisibleBlock")) or legacy.num(block.get("invisible")),
        "itemBox": legacy.num(block.get("itemBox")),
        "hiddenOrRescue": legacy.num(block.get("hiddenOrRescueCandidate"), derived["hiddenOrRescueCandidate"]),
        "visibleStorageBreakable": legacy.num(
            block.get("visibleStorageBreakableCandidate"),
            derived["visibleStorageBreakableCandidate"],
        ),
        "visibleSolid": legacy.num(block.get("visibleSolidCandidate"), derived["visibleSolidCandidate"]),
    }
    mask = 0
    for index, name in enumerate(TERRAIN_CHANNELS):
        if values[name]:
            mask |= 1 << index
    return mask


def compact_terrain(player: dict[str, Any]) -> dict[str, Any]:
    grid = ((player.get("tileProbe") or {}).get("grid")) or {}
    sparse = str(grid.get("encoding") or "") == "sparse_non_empty"
    cells = []
    for raw in grid.get("cells") or []:
        if not isinstance(raw, dict):
            continue
        mask = terrain_mask(raw)
        status = legacy.num(raw.get("status"))
        found = legacy.num(raw.get("found"), 1 if sparse else 0)
        if mask == 0 and found and status == 0:
            continue
        cells.append(
            {
                "r": legacy.num(raw.get("row")),
                "c": legacy.num(raw.get("col")),
                "rx": legacy.num(raw.get("relTileX")),
                "ry": legacy.num(raw.get("relTileY")),
                "found": found,
                "status": status,
                "mask": mask,
                "tileId": legacy.num(raw.get("tileId")),
                "behavior": legacy.num(raw.get("behavior")),
            }
        )
    return {
        "encoding": "sparse_channel_mask_v2",
        "width": legacy.num(grid.get("width"), legacy.TILE_GRID_WIDTH),
        "height": legacy.num(grid.get("height"), legacy.TILE_GRID_HEIGHT),
        "minRelTileX": legacy.num(grid.get("minRelTileX"), legacy.TILE_GRID_MIN_REL_X),
        "minRelTileY": legacy.num(grid.get("minRelTileY"), legacy.TILE_GRID_MIN_REL_Y),
        "channels": TERRAIN_CHANNELS,
        "omittedCellFound": legacy.num(grid.get("omittedCellFound"), 1 if sparse else 0),
        "omittedCellStatus": legacy.num(grid.get("omittedCellStatus"), 0),
        "cells": cells,
    }


def compact_player(player: dict[str, Any]) -> dict[str, Any]:
    contact = player.get("contact") or {}
    tile_probe = player.get("tileProbe") or {}
    samples = legacy.by_name(tile_probe.get("samples") or [])
    visual = player.get("visualState") or {}
    terrain = compact_terrain(player)
    return {
        "found": legacy.num(player.get("found")),
        "pos": legacy.pos(player),
        "vel": legacy.vel(player),
        "screen": {
            "camera0": legacy.screen(player, "camera0"),
            "camera1": legacy.screen(player, "camera1"),
        },
        "contact": {name: legacy.num(contact.get(name)) for name in legacy.CONTACT_NAMES},
        "visual": {
            "powerupKind": legacy.visual_powerup_kind_candidate(player),
            "sourceMask": legacy.visual_powerup_source_mask_candidate(player),
            "fire": legacy.num(visual.get("isFireVisualCandidate")),
            "mini": int(legacy.visual_powerup_kind_candidate(player) == legacy.ITEM_KIND_MINI_MUSHROOM),
            "shell": legacy.num((visual.get("powerup") or {}).get("isShellCandidate")),
            "mega": int(legacy.visual_powerup_kind_candidate(player) == legacy.ITEM_KIND_MEGA_MUSHROOM),
            "starInvincible": legacy.star_invincible_candidate(player),
            "invincible": legacy.invincible_candidate(player),
            "actorPowerupState": legacy.num(visual.get("actorPowerupState")),
            "actorPowerupFormState": legacy.num(visual.get("actorPowerupFormState")),
            "shellState": legacy.num(visual.get("shellState")),
        },
        "battleStars": legacy.num(player.get("battleStars")),
        "coins": legacy.num(player.get("coins")),
        "dead": legacy.num(player.get("dead")),
        "tileSummary": legacy.recompute_tile_probe_summary(tile_probe, contact, samples, terrain),
        "terrain": terrain,
    }


def screen_mask(screen: dict[str, Any]) -> int:
    mask = 0
    if legacy.num(((screen.get("camera0") or {}).get("inView"))):
        mask |= 1
    if legacy.num(((screen.get("camera1") or {}).get("inView"))):
        mask |= 2
    return mask


def compact_object_entity(obj: dict[str, Any], players: list[dict[str, Any]]) -> dict[str, Any]:
    category = legacy.object_category(obj)
    object_id = legacy.num(obj.get("objectId"))
    settings = legacy.num(obj.get("settings"))
    pos = legacy.pos(obj)
    vel = legacy.vel(obj)
    kind_by_player = []
    for player in players:
        kind, confidence = legacy.item_kind_candidate(
            object_id,
            settings,
            category,
            legacy.visual_powerup_kind_candidate(player),
        )
        kind_by_player.append({"kind": kind, "confidence": confidence})
    relative = {}
    for index, player in enumerate(players[:2]):
        player_pos = legacy.pos(player)
        relative[f"player{index}"] = {
            "dx": legacy.delta_x(pos["x"], player_pos["x"]),
            "dy": pos["y"] - player_pos["y"],
        }
    return {
        "source": "object",
        "category": category,
        "categoryId": ENTITY_CATEGORY_IDS.get(category, 0),
        "objectId": object_id,
        "settings": settings,
        "kindByPlayer": kind_by_player,
        "pos": pos,
        "vel": vel,
        "relative": relative,
        "screenMask": screen_mask(obj.get("screen") or {}),
        "state": legacy.num(obj.get("stateType")),
        "flags": legacy.num(obj.get("flags")),
    }


def compact_fireball_entity(slot: dict[str, Any], players: list[dict[str, Any]]) -> dict[str, Any]:
    owner = legacy.num(slot.get("ownerCandidate"), -1)
    owner_verified = legacy.num(slot.get("ownerVerified"))
    category = "player_fireball" if owner in {0, 1} else "enemy_fireball"
    pos = legacy.pos(slot)
    vel = legacy.vel(slot)
    relative = {}
    for index, player in enumerate(players[:2]):
        player_pos = legacy.pos(player)
        relative[f"player{index}"] = {
            "dx": legacy.delta_x(pos["x"], player_pos["x"]),
            "dy": pos["y"] - player_pos["y"],
        }
    return {
        "source": "fireball",
        "category": category,
        "categoryId": ENTITY_CATEGORY_IDS.get(category, 0),
        "objectId": 0,
        "settings": 0,
        "kind": legacy.num(slot.get("kind")),
        "owner": owner,
        "ownerConfidence": legacy.num(slot.get("ownerConfidence")),
        "ownerVerified": owner_verified,
        "pos": pos,
        "vel": vel,
        "relative": relative,
        "screenMask": 0,
        "state": legacy.num(slot.get("state")),
        "flags": 0,
    }


def compact_entities(record: dict[str, Any]) -> list[dict[str, Any]]:
    players = record.get("players") or []
    entities = [
        compact_object_entity(obj, players)
        for obj in record.get("objects") or []
        if isinstance(obj, dict)
    ]
    fireballs = (((record.get("specialObjects") or {}).get("fireballs")) or {}).get("slots") or []
    entities.extend(compact_fireball_entity(slot, players) for slot in fireballs if isinstance(slot, dict))
    return entities


def scalar_features_for_player(record: dict[str, Any], player: int) -> dict[str, int]:
    players = record.get("players") or []
    if len(players) <= player:
        return {}
    self_player = players[player]
    opponent = players[player ^ 1] if len(players) > (player ^ 1) else {}
    self_pos = legacy.pos(self_player)
    opponent_pos = legacy.pos(opponent)
    target = ((record.get("targets") or {}).get("bigStarActor")) or {}
    target_pos = legacy.pos(target)
    tile_probe = self_player.get("tileProbe") or {}
    terrain = compact_terrain(self_player)
    tile_summary = legacy.recompute_tile_probe_summary(
        tile_probe,
        self_player.get("contact") or {},
        legacy.by_name(tile_probe.get("samples") or []),
        terrain,
    )
    objects = record.get("objects") or []
    runtime_hazard = legacy.runtime_hazard_threat(objects, self_pos, legacy.vel(self_player))
    nearest_item = legacy.nearest_item_details(
        objects,
        self_pos,
        current_powerup_kind=legacy.visual_powerup_kind_candidate(self_player),
    )
    return {
        "stage_id": legacy.num((record.get("stage") or {}).get("id")),
        "stage_group": legacy.num((record.get("stage") or {}).get("group")),
        "self_x": self_pos["x"],
        "self_y": self_pos["y"],
        "self_vx": legacy.vel(self_player)["x"],
        "self_vy": legacy.vel(self_player)["y"],
        "self_powerup_kind": legacy.visual_powerup_kind_candidate(self_player),
        "self_invincible": legacy.invincible_candidate(self_player),
        "self_star_invincible": legacy.star_invincible_candidate(self_player),
        "self_battle_stars": legacy.num(self_player.get("battleStars")),
        "self_coins": legacy.num(self_player.get("coins")),
        "opponent_dx": legacy.delta_x(opponent_pos["x"], self_pos["x"]),
        "opponent_dy": opponent_pos["y"] - self_pos["y"],
        "opponent_powerup_kind": legacy.visual_powerup_kind_candidate(opponent),
        "opponent_battle_stars": legacy.num(opponent.get("battleStars")),
        "target_found": legacy.num(target.get("found")),
        "target_dx": legacy.delta_x(target_pos["x"], self_pos["x"]),
        "target_dy": target_pos["y"] - self_pos["y"],
        "nearest_item_found": nearest_item["found"],
        "nearest_item_dx": nearest_item["dx"],
        "nearest_item_dy": nearest_item["dy"],
        "nearest_item_kind": nearest_item["kind_candidate"],
        "nearest_item_avoid": nearest_item["avoid_candidate"],
        "runtime_hazard_found": runtime_hazard["found"],
        "runtime_hazard_dx": runtime_hazard["dx"],
        "runtime_hazard_dy": runtime_hazard["dy"],
        "runtime_hazard_closing": runtime_hazard["closing"],
        "runtime_hazard_category": runtime_hazard["category"],
        **{f"tile_{name}": legacy.num(tile_summary.get(name)) for name in COMPACT_SCALAR_TILE_KEYS},
    }


def set_coin_reward_meta(
    record: dict[str, Any],
    player: int,
    previous_coins: int | None,
    reward_frame: int | None,
) -> tuple[int, int | None]:
    players = record.get("players") or []
    player_record = players[player] if len(players) > player else {}
    frame = legacy.num(record.get("frame"))
    coins = legacy.num(player_record.get("coins"), -1)
    if previous_coins == 7 and coins == 0:
        reward_frame = frame
    reward_age = frame - reward_frame if reward_frame is not None else -1
    reward_recent = reward_frame is not None and 0 <= reward_age <= legacy.COIN_REWARD_ITEM_WINDOW_FRAMES
    record["_self_prev_coins"] = previous_coins if previous_coins is not None else -1
    record["_self_coin_reward_recent"] = int(reward_recent)
    record["_self_coin_reward_age"] = reward_age if reward_recent else -1
    return coins, reward_frame


def compact_record(
    record: dict[str, Any],
    *,
    recording_index: int,
    recording_frame_index: int,
    label_source: str,
    include_legacy_features: bool,
    legacy_coin_state: dict[int, tuple[int | None, int | None]],
) -> dict[str, Any]:
    record["_recording_index"] = recording_index
    record["_recording_frame_index"] = recording_frame_index
    players = record.get("players") or []
    labels = {f"player{player}": player_label(record, player, label_source) for player in (0, 1)}
    compact = {
        "schema": "nsmb_mvl_compact_observation_v2",
        "sourceSchema": record.get("schema"),
        "recordingIndex": recording_index,
        "recordingFrameIndex": recording_frame_index,
        "frame": legacy.num(record.get("frame")),
        "instance": legacy.num(record.get("instance")),
        "role": record.get("role"),
        "localPlayer": legacy.num(record.get("localPlayer"), -1),
        "stage": record.get("stage") or {},
        "labels": labels,
        "players": [compact_player(player) for player in players[:2]],
        "scalarFeaturesByPlayer": {
            f"player{player}": scalar_features_for_player(record, player) for player in (0, 1)
        },
        "targets": {
            "bigStarActor": ((record.get("targets") or {}).get("bigStarActor")) or {},
        },
        "camera": record.get("camera") or {},
        "objectSummary": record.get("objectSummary") or {},
        "entities": compact_entities(record),
    }
    if include_legacy_features:
        legacy_rows = {}
        for player in (0, 1):
            prev, reward = legacy_coin_state[player]
            next_prev, next_reward = set_coin_reward_meta(record, player, prev, reward)
            legacy_coin_state[player] = (next_prev, next_reward)
            try:
                if labels[f"player{player}"]["valid"]:
                    legacy_rows[f"player{player}"] = legacy.build_row(record, player, label_source)
            except (KeyError, ValueError):
                pass
        compact["legacyFeatureRows"] = legacy_rows
    return compact


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="v1 ai-playlog JSONL(.gz), recording manifest, or recordings-index")
    parser.add_argument("output", type=Path, help="compact observation v2 JSONL(.gz)")
    parser.add_argument("--label-source", choices=["auto", "applied", "player", "console"], default="auto")
    parser.add_argument("--min-frame", type=int, default=0)
    parser.add_argument("--include-legacy-features", action="store_true")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with open_text(args.output, "w") as out:
        for recording_index, playlog_path in enumerate(legacy.input_playlog_paths(args.input)):
            frame_index = 0
            legacy_coin_state: dict[int, tuple[int | None, int | None]] = {
                0: (None, None),
                1: (None, None),
            }
            for record in legacy.iter_records(playlog_path):
                if legacy.num(record.get("frame")) < args.min_frame:
                    continue
                compact = compact_record(
                    record,
                    recording_index=recording_index,
                    recording_frame_index=frame_index,
                    label_source=args.label_source,
                    include_legacy_features=args.include_legacy_features,
                    legacy_coin_state=legacy_coin_state,
                )
                out.write(json.dumps(compact, separators=(",", ":")) + "\n")
                frame_index += 1
                count += 1
    print(f"records={count} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
