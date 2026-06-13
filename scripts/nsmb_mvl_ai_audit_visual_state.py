#!/usr/bin/env python3
"""Audit whether an NSMB MvL AI play log exposes screen-visible state for learning."""

from __future__ import annotations

import argparse
import gzip
import json
import sys
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


def fireball_owner_info(slot: dict[str, Any]) -> tuple[int, int, int]:
    kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
    if kind in (0, 1):
        return kind, 100, 1
    if kind in (2, 3):
        return -1, 100, 1
    return num(slot.get("ownerCandidate"), -1), num(slot.get("ownerConfidence")), num(slot.get("ownerVerified"))


def iter_records(path: Path):
    opener = gzip.open if path.name.lower().endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8-sig") as f:
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
    invincibility_candidate_frames = [0, 0]
    damage_guard_timer_frames = [0, 0]
    damage_cooldown_frames = [0, 0]
    shell_state_frames = [0, 0]
    powerup_values: list[Counter[int]] = [Counter(), Counter()]
    inventory_powerup_values: list[Counter[int]] = [Counter(), Counter()]
    visual_powerup_values: list[Counter[int]] = [Counter(), Counter()]
    visual_fire_candidate_frames = [0, 0]
    category_counts: Counter[str] = Counter()
    visible_unknown_objects: Counter[str] = Counter()
    fireball_slots = 0
    fireball_owner_low_confidence = 0
    fireball_owner_tracked = 0
    fireball_stateless_owner_low_confidence = 0
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
            visual_powerup_values[player_index][num(visual_state.get("visualPowerupKindCandidate"), -1)] += 1
            if num(visual_state.get("isFireVisualCandidate")) or num(visual_state.get("canShootFireVisualCandidate")):
                visual_fire_candidate_frames[player_index] += 1
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
            else:
                if num(visual_state.get("invincibleCandidate")):
                    invincibility_candidate_frames[player_index] += 1
                if num(visual_state.get("damageGuardTimer")):
                    damage_guard_timer_frames[player_index] += 1
                if num(visual_state.get("damageCooldown")):
                    damage_cooldown_frames[player_index] += 1
                if num(visual_state.get("shellState")):
                    shell_state_frames[player_index] += 1

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
            owner, confidence, owner_verified = fireball_owner_info(slot)
            if num(slot.get("ownerTracked")):
                fireball_owner_tracked += 1
            stateless_owner = num(slot.get("statelessOwnerCandidate"), -1)
            stateless_confidence = num(slot.get("statelessOwnerConfidence"))
            if not owner_verified and (stateless_owner < 0 or stateless_confidence < fireball_owner_min_confidence):
                fireball_stateless_owner_low_confidence += 1
            if not owner_verified and (owner < 0 or confidence < fireball_owner_min_confidence):
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
                        "ownerVerified": owner_verified,
                        "ownerSource": slot.get("ownerSource"),
                        "ownerTracked": num(slot.get("ownerTracked")),
                        "statelessOwnerCandidate": stateless_owner,
                        "statelessOwnerConfidence": stateless_confidence,
                        "kind": slot.get("kind"),
                        "kindName": slot.get("kindName"),
                        "state": slot.get("state"),
                        "facing": slot.get("facing"),
                        "stateBytesOffset": slot.get("stateBytesOffset"),
                        "stateBytes": slot.get("stateBytes"),
                        "debugWordsOffset": slot.get("debugWordsOffset"),
                        "debugWords": (slot.get("debugWords") or [])[:4],
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
        "invincibilityCandidateFrames": invincibility_candidate_frames,
        "damageGuardTimerFrames": damage_guard_timer_frames,
        "damageCooldownFrames": damage_cooldown_frames,
        "shellStateFrames": shell_state_frames,
        "powerupValues": [{str(k): v for k, v in counter.items()} for counter in powerup_values],
        "inventoryPowerupValues": [{str(k): v for k, v in counter.items()} for counter in inventory_powerup_values],
        "visualPowerupValues": [{str(k): v for k, v in counter.items()} for counter in visual_powerup_values],
        "visualFireCandidateFrames": visual_fire_candidate_frames,
        "categoryCounts": dict(category_counts),
        "visibleUnknownObjects": dict(visible_unknown_objects),
        "fireballSlots": fireball_slots,
        "fireballOwnerLowConfidence": fireball_owner_low_confidence,
        "fireballOwnerTracked": fireball_owner_tracked,
        "fireballStatelessOwnerLowConfidence": fireball_stateless_owner_low_confidence,
        "projectileVisibleFrames": projectile_visible_frames,
        "samples": dict(samples),
    }


def strict_failures(report: dict[str, Any], args: argparse.Namespace) -> list[str]:
    failures: list[str] = []
    if args.fail_on_visual_state_missing and any(num(value) for value in report.get("visualStateMissing") or []):
        failures.append(f"{report['playlog']}: visualStateMissing={report.get('visualStateMissing')}")
    if args.fail_on_invincibility_unknown and any(num(value) for value in report.get("invincibilityUnknown") or []):
        failures.append(f"{report['playlog']}: invincibilityUnknown={report.get('invincibilityUnknown')}")
    if args.fail_on_visible_unknown_object and report.get("visibleUnknownObjects"):
        failures.append(f"{report['playlog']}: visibleUnknownObjects={report.get('visibleUnknownObjects')}")
    if args.fail_on_fireball_low_confidence and num(report.get("fireballOwnerLowConfidence")):
        failures.append(
            f"{report['playlog']}: fireballOwnerLowConfidence={report.get('fireballOwnerLowConfidence')}"
        )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlogs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-samples", type=int, default=8)
    parser.add_argument("--fireball-owner-min-confidence", type=int, default=60)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail on visualState missing, invincibility unknown, visible unknown objects, or low-confidence fireball owner.",
    )
    parser.add_argument("--fail-on-visual-state-missing", action="store_true")
    parser.add_argument("--fail-on-invincibility-unknown", action="store_true")
    parser.add_argument("--fail-on-visible-unknown-object", action="store_true")
    parser.add_argument("--fail-on-fireball-low-confidence", action="store_true")
    args = parser.parse_args()
    if args.strict:
        args.fail_on_visual_state_missing = True
        args.fail_on_invincibility_unknown = True
        args.fail_on_visible_unknown_object = True
        args.fail_on_fireball_low_confidence = True

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
    failures = [failure for report in reports for failure in strict_failures(report, args)]
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
