#!/usr/bin/env python3
"""Audit imitation-policy play logs for neutral/stuck failure states."""

from __future__ import annotations

import argparse
import gzip
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, TextIO


LEFT_BIT = 5
RIGHT_BIT = 4
A_BIT = 0
B_BIT = 1
Y_BIT = 11

HAZARD_CATEGORIES = ("moving_hazard", "hazard", "enemy_goomba", "enemy_koopa")
ITEM_CATEGORIES = ("world_item", "neutral_item", "coin_item", "dropped_star_item", "item", "coin")
STAR_CATEGORIES = ("big_star_actor",)


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str) and value != "":
        return int(value, 0)
    return default


def signed32(value: Any, default: int = 0) -> int:
    raw = num(value, default) & 0xFFFFFFFF
    if raw & 0x80000000:
        raw -= 0x100000000
    return raw


def ratio(numerator: int, denominator: int) -> float:
    return numerator / denominator if denominator else 0.0


def open_text(path: Path) -> TextIO:
    if path.suffix.lower() == ".gz":
        return gzip.open(path, "rt", encoding="utf-8")
    return path.open("r", encoding="utf-8")


def iter_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    with open_text(path) as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def player(record: dict[str, Any], index: int) -> dict[str, Any]:
    players = record.get("players") or []
    if 0 <= index < len(players):
        return players[index] or {}
    return {}


def pos_x(p: dict[str, Any]) -> int:
    return signed32((p.get("pos") or {}).get("x"))


def pos_y(p: dict[str, Any]) -> int:
    return signed32((p.get("pos") or {}).get("y"))


def vel_x(p: dict[str, Any]) -> int:
    return signed32((p.get("vel") or {}).get("x"))


def vel_y(p: dict[str, Any]) -> int:
    return signed32((p.get("vel") or {}).get("y"))


def held(record: dict[str, Any], index: int) -> int | None:
    inputs = record.get("inputs") or {}
    applied = inputs.get(f"appliedPlayer{index}") or {}
    if applied.get("valid"):
        return num(applied.get("held"))
    direct = inputs.get(f"player{index}") or {}
    if "held" in direct:
        return num(direct.get("held"))
    return None


def button(mask: int | None, bit: int) -> bool:
    return mask is not None and (mask & (1 << bit)) != 0


def tile_summary(p: dict[str, Any]) -> dict[str, Any]:
    return ((p.get("tileProbe") or {}).get("summary")) or {}


def contact(p: dict[str, Any]) -> dict[str, Any]:
    return p.get("contact") or {}


def nearest(record: dict[str, Any], index: int, categories: tuple[str, ...]) -> dict[str, Any] | None:
    entries = ((record.get("visualSummary") or {}).get("nearest")) or []
    selected: dict[str, Any] | None = None
    for entry in entries:
        if num(entry.get("player"), -1) != index:
            continue
        nearest_categories = entry.get("categories") or {}
        for category in categories:
            candidate = nearest_categories.get(category) or {}
            if not candidate.get("found"):
                continue
            dx = signed32(candidate.get("dx"))
            dy = signed32(candidate.get("dy"))
            dist2 = num(candidate.get("dist2"), dx * dx + dy * dy)
            current = {
                "category": category,
                "objectId": candidate.get("objectId"),
                "guid": candidate.get("guid"),
                "dx": dx,
                "dy": dy,
                "dist2": dist2,
            }
            if selected is None or dist2 < num(selected.get("dist2")):
                selected = current
    return selected


def append_sample(samples: list[dict[str, Any]], limit: int, sample: dict[str, Any]) -> None:
    if len(samples) < limit:
        samples.append(sample)


def context_for(record: dict[str, Any], index: int, mask: int | None) -> tuple[str, dict[str, Any]]:
    p = player(record, index)
    if not record.get("inGameplay"):
        return "not_gameplay", {}
    if not p.get("found"):
        return "player_missing", {}
    if p.get("dead"):
        return "dead", {}

    summary = tile_summary(p)
    c = contact(p)
    fall = p.get("fallRisk") or {}
    hazard = nearest(record, index, HAZARD_CATEGORIES)
    item = nearest(record, index, ITEM_CATEGORIES)
    star = nearest(record, index, STAR_CATEGORIES)
    opponent = nearest_player(record, index)

    hole_ahead = bool(num(summary.get("effectiveHoleAhead"), summary.get("holeAhead")))
    hole_left = bool(num(summary.get("effectiveHoleLeft"), summary.get("holeLeft")))
    hole_right = bool(num(summary.get("effectiveHoleRight"), summary.get("holeRight")))
    blocked_left = bool(num(summary.get("blockedLeft"), summary.get("wallLeft")) or num(c.get("wallLeft")))
    blocked_right = bool(num(summary.get("blockedRight"), summary.get("wallRight")) or num(c.get("wallRight")))
    grounded = bool(num(c.get("ground")) or num(summary.get("effectiveGroundBelowSolid")))
    near_hazard = hazard is not None and abs(hazard["dx"]) <= 0x40000 and abs(hazard["dy"]) <= 0x50000
    near_item = item is not None and abs(item["dx"]) <= 0x50000 and abs(item["dy"]) <= 0x60000
    near_star = star is not None and star["dist2"] <= 0x90000 * 0x90000
    near_opponent = opponent is not None and abs(opponent["dx"]) <= 0x50000 and abs(opponent["dy"]) <= 0x50000
    near_bottom = bool(num(fall.get("nearCameraBottom0")) or num(fall.get("nearCameraBottom1")))
    below_camera = bool(num(fall.get("belowCamera0")) or num(fall.get("belowCamera1")))

    details = {
        "x": pos_x(p),
        "y": pos_y(p),
        "vx": vel_x(p),
        "vy": vel_y(p),
        "held": f"0x{mask or 0:03X}",
        "grounded": int(grounded),
        "holeAhead": int(hole_ahead),
        "holeLeft": int(hole_left),
        "holeRight": int(hole_right),
        "blockedLeft": int(blocked_left),
        "blockedRight": int(blocked_right),
        "nearCameraBottom": int(near_bottom),
        "belowCamera": int(below_camera),
        "nearestHazard": hazard,
        "nearestItem": item,
        "nearestStar": star,
        "nearestOpponent": opponent,
        "buttons": buttons(mask),
    }

    if below_camera or near_bottom:
        return "fall_risk", details
    if hole_ahead or hole_left or hole_right:
        return "hole_or_edge", details
    if blocked_left or blocked_right:
        return "blocked_or_wall", details
    if near_hazard:
        return "hazard_near", details
    if near_opponent:
        return "opponent_near", details
    if near_item:
        return "item_near", details
    if near_star:
        return "star_near", details
    if not grounded:
        return "airborne", details
    if not mask:
        return "grounded_open_neutral", details
    return "grounded_open_nonzero", details


def nearest_player(record: dict[str, Any], index: int) -> dict[str, Any] | None:
    p = player(record, index)
    o = player(record, index ^ 1)
    if not p.get("found") or not o.get("found"):
        return None
    dx = pos_x(o) - pos_x(p)
    dy = pos_y(o) - pos_y(p)
    return {"category": "opponent", "dx": dx, "dy": dy, "dist2": dx * dx + dy * dy}


def buttons(mask: int | None) -> list[str]:
    if mask is None:
        return []
    result: list[str] = []
    if button(mask, LEFT_BIT):
        result.append("left")
    if button(mask, RIGHT_BIT):
        result.append("right")
    if button(mask, A_BIT):
        result.append("a")
    if button(mask, B_BIT):
        result.append("b")
    if button(mask, Y_BIT):
        result.append("y")
    return result


def summarize_run(run: list[dict[str, Any]], context: str, sample_limit: int) -> dict[str, Any]:
    first = run[0]
    last = run[-1]
    xs = [entry["x"] for entry in run if entry.get("found")]
    ys = [entry["y"] for entry in run if entry.get("found")]
    return {
        "context": context,
        "startFrame": first["frame"],
        "endFrame": last["frame"],
        "records": len(run),
        "frameSpan": last["frame"] - first["frame"],
        "minX": min(xs) if xs else None,
        "maxX": max(xs) if xs else None,
        "minY": min(ys) if ys else None,
        "maxY": max(ys) if ys else None,
        "first": first.get("details"),
        "last": last.get("details"),
        "frames": [entry["frame"] for entry in run[:sample_limit]],
    }


def audit(path: Path, player_index: int, sample_limit: int, neutral_records: int, stuck_records: int) -> dict[str, Any]:
    rows = 0
    gameplay_rows = 0
    found_rows = 0
    input_rows = 0
    neutral_rows = 0
    nonzero_rows = 0
    dead_rows = 0
    held_counts: Counter[str] = Counter()
    neutral_context_counts: Counter[str] = Counter()
    stuck_context_counts: Counter[str] = Counter()
    neutral_window_count = 0
    stuck_window_count = 0

    samples: dict[str, list[dict[str, Any]]] = {
        "neutralWindows": [],
        "stuckWindows": [],
        "neutralRowsByContext": [],
        "stuckRowsByContext": [],
    }

    neutral_run: list[dict[str, Any]] = []
    stuck_run: list[dict[str, Any]] = []

    for record in iter_jsonl(path):
        rows += 1
        frame = num(record.get("frame"))
        in_gameplay = bool(record.get("inGameplay"))
        if in_gameplay:
            gameplay_rows += 1
        p = player(record, player_index)
        found = bool(p.get("found"))
        if found:
            found_rows += 1
        if p.get("dead"):
            dead_rows += 1

        mask = held(record, player_index)
        if mask is not None:
            input_rows += 1
            held_counts[f"0x{mask:03X}"] += 1
            if mask:
                nonzero_rows += 1
            else:
                neutral_rows += 1

        context, details = context_for(record, player_index, mask)
        if in_gameplay and found and not p.get("dead") and mask == 0:
            neutral_context_counts[context] += 1
            append_sample(samples["neutralRowsByContext"], sample_limit, {"frame": frame, "context": context, **details})
            neutral_run.append({"frame": frame, "found": found, "x": pos_x(p), "y": pos_y(p), "details": details})
            if len(neutral_run) == neutral_records:
                neutral_window_count += 1
                append_sample(samples["neutralWindows"], sample_limit, summarize_run(neutral_run, context, sample_limit))
            elif len(neutral_run) > neutral_records and len(samples["neutralWindows"]) < sample_limit:
                samples["neutralWindows"][-1] = summarize_run(neutral_run, context, sample_limit)
        else:
            neutral_run = []

        if in_gameplay and found and not p.get("dead"):
            entry = {"frame": frame, "found": found, "x": pos_x(p), "y": pos_y(p), "details": details}
            stuck_run.append(entry)
            if len(stuck_run) > stuck_records:
                stuck_run.pop(0)
            if len(stuck_run) == stuck_records:
                xs = [e["x"] for e in stuck_run]
                ys = [e["y"] for e in stuck_run]
                if max(xs) - min(xs) <= 0x2800 and max(ys) - min(ys) <= 0x4000:
                    stuck_window_count += 1
                    stuck_context_counts[context] += 1
                    append_sample(samples["stuckRowsByContext"], sample_limit, {"frame": frame, "context": context, **details})
                    append_sample(samples["stuckWindows"], sample_limit, summarize_run(stuck_run, context, sample_limit))
                    stuck_run = []
        else:
            stuck_run = []

    return {
        "schema": "nsmb_mvl_ai_imitation_audit_v1",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "input": str(path),
        "player": player_index,
        "rows": rows,
        "gameplayRows": gameplay_rows,
        "playerFoundRows": found_rows,
        "deadRows": dead_rows,
        "inputRows": input_rows,
        "neutralInputRows": neutral_rows,
        "nonzeroInputRows": nonzero_rows,
        "neutralInputRatio": ratio(neutral_rows, input_rows),
        "nonzeroInputRatio": ratio(nonzero_rows, input_rows),
        "heldTop": [{"value": value, "count": count} for value, count in held_counts.most_common(10)],
        "neutralContextCounts": [{"context": key, "count": value} for key, value in neutral_context_counts.most_common()],
        "stuckContextCounts": [{"context": key, "count": value} for key, value in stuck_context_counts.most_common()],
        "neutralWindowCount": neutral_window_count,
        "stuckWindowCount": stuck_window_count,
        "samples": samples,
        "notes": [
            "neutral/stuck counts depend on AIPlayLogInterval; use interval 10 or smaller for detailed diagnosis",
            "contexts are heuristic labels for prioritizing new data collection and feature/model debugging",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sample-limit", type=int, default=12)
    parser.add_argument("--neutral-records", type=int, default=3)
    parser.add_argument("--stuck-records", type=int, default=6)
    args = parser.parse_args()

    report = audit(args.input, args.player, args.sample_limit, args.neutral_records, args.stuck_records)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
