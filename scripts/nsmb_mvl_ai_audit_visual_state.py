#!/usr/bin/env python3
"""Audit whether an NSMB MvL AI play log exposes screen-visible state for learning."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


KNOWN_CATEGORIES = {
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
}


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def iter_records(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield line_no, json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def add_sample(samples: dict[str, list[dict[str, Any]]], key: str, sample: dict[str, Any], limit: int) -> None:
    if limit <= 0 or len(samples[key]) >= limit:
        return
    samples[key].append(sample)


def audit_playlog(path: Path, max_samples: int, fireball_owner_min_confidence: int) -> dict[str, Any]:
    rows = 0
    gameplay_rows = 0
    visual_state_missing = [0, 0]
    invincibility_unknown = [0, 0]
    powerup_values: list[Counter[int]] = [Counter(), Counter()]
    inventory_powerup_values: list[Counter[int]] = [Counter(), Counter()]
    category_counts: Counter[str] = Counter()
    visible_unknown_objects: Counter[str] = Counter()
    fireball_slots = 0
    fireball_owner_low_confidence = 0
    projectile_visible_frames = 0
    samples: dict[str, list[dict[str, Any]]] = defaultdict(list)

    for line_no, record in iter_records(path):
        rows += 1
        frame = num(record.get("frame"))
        if record.get("inGameplay"):
            gameplay_rows += 1

        players = record.get("players") or []
        for player_index in range(min(2, len(players))):
            player = players[player_index]
            powerup_values[player_index][num(player.get("powerup"), -1)] += 1
            inventory_powerup_values[player_index][num(player.get("inventoryPowerup"), -1)] += 1
            visual_state = player.get("visualState") or {}
            if not visual_state:
                visual_state_missing[player_index] += 1
                add_sample(
                    samples,
                    "visualStateMissing",
                    {"frame": frame, "line": line_no, "player": player_index},
                    max_samples,
                )
            elif not num(visual_state.get("invincibleKnown")):
                invincibility_unknown[player_index] += 1

        projectile_visible = False
        for obj in record.get("objects") or []:
            category = str(obj.get("category", "object"))
            category_counts[category] += 1
            if category in {"projectile", "player_fireball", "enemy_fireball"}:
                projectile_visible = True
            screen = obj.get("screen") or {}
            in_view = any(num((screen.get(camera) or {}).get("inView")) for camera in ("camera0", "camera1"))
            if in_view and category not in KNOWN_CATEGORIES:
                key = f"{obj.get('objectId')}:{obj.get('settings')}:{obj.get('vtable')}"
                visible_unknown_objects[key] += 1
                add_sample(
                    samples,
                    "visibleUnknownObject",
                    {
                        "frame": frame,
                        "line": line_no,
                        "category": category,
                        "objectId": obj.get("objectId"),
                        "settings": obj.get("settings"),
                        "vtable": obj.get("vtable"),
                    },
                    max_samples,
                )
        if projectile_visible:
            projectile_visible_frames += 1

        special_objects = record.get("specialObjects") or {}
        fireballs = special_objects.get("fireballs") or {}
        for slot in fireballs.get("slots") or []:
            fireball_slots += 1
            owner = num(slot.get("ownerCandidate"), -1)
            confidence = num(slot.get("ownerConfidence"))
            if owner < 0 or confidence < fireball_owner_min_confidence:
                fireball_owner_low_confidence += 1
                add_sample(
                    samples,
                    "fireballOwnerLowConfidence",
                    {
                        "frame": frame,
                        "line": line_no,
                        "slot": slot.get("index"),
                        "ownerCandidate": owner,
                        "ownerConfidence": confidence,
                        "kind": slot.get("kind"),
                        "state": slot.get("state"),
                        "facing": slot.get("facing"),
                        "relative": slot.get("relative"),
                    },
                    max_samples,
                )

    return {
        "playlog": str(path),
        "rows": rows,
        "gameplayRows": gameplay_rows,
        "visualStateMissing": visual_state_missing,
        "invincibilityUnknown": invincibility_unknown,
        "powerupValues": [{str(k): v for k, v in counter.items()} for counter in powerup_values],
        "inventoryPowerupValues": [{str(k): v for k, v in counter.items()} for counter in inventory_powerup_values],
        "categoryCounts": dict(category_counts),
        "visibleUnknownObjects": dict(visible_unknown_objects),
        "fireballSlots": fireball_slots,
        "fireballOwnerLowConfidence": fireball_owner_low_confidence,
        "projectileVisibleFrames": projectile_visible_frames,
        "samples": dict(samples),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlogs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-samples", type=int, default=8)
    parser.add_argument("--fireball-owner-min-confidence", type=int, default=60)
    args = parser.parse_args()

    reports = [
        audit_playlog(path, args.max_samples, args.fireball_owner_min_confidence)
        for path in args.playlogs
    ]
    result: dict[str, Any] = {"reports": reports}
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
