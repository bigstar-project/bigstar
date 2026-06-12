#!/usr/bin/env python3
"""Audit RuleAI play logs for obvious closed-loop control failures."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


LEFT_BIT = 5
RIGHT_BIT = 4
A_BIT = 0
Y_BIT = 11


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


def iter_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as f:
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


def held(record: dict[str, Any], index: int) -> int | None:
    inputs = record.get("inputs") or {}
    applied = inputs.get(f"appliedPlayer{index}") or {}
    if applied.get("valid"):
        return num(applied.get("held"))
    direct = inputs.get(f"player{index}") or {}
    if "held" in direct:
        return num(direct.get("held"))
    return None


def pressed(mask: int | None, bit: int) -> bool:
    return mask is not None and (mask & (1 << bit)) != 0


def tile_summary(p: dict[str, Any]) -> dict[str, Any]:
    return ((p.get("tileProbe") or {}).get("summary")) or {}


def append_sample(samples: list[dict[str, Any]], limit: int, sample: dict[str, Any]) -> None:
    if len(samples) < limit:
        samples.append(sample)


def audit(path: Path, player_index: int, sample_limit: int, stuck_records: int) -> dict[str, Any]:
    rows = 0
    gameplay_rows = 0
    found_rows = 0
    input_rows = 0
    nonzero_rows = 0
    held_counts: Counter[str] = Counter()
    death_transitions = 0
    left_right_flips = 0
    blocked_input_rows = 0
    hole_input_rows = 0
    stuck_windows = 0

    samples: dict[str, list[dict[str, Any]]] = {
        "deathTransitions": [],
        "blockedInputs": [],
        "holeInputs": [],
        "leftRightFlips": [],
        "stuckWindows": [],
        "stateContradictions": [],
    }

    prev_record: dict[str, Any] | None = None
    prev_mask: int | None = None
    prev_dir = 0
    still_run: list[dict[str, Any]] = []

    for record in iter_jsonl(path):
        rows += 1
        frame = num(record.get("frame"))
        if record.get("inGameplay"):
            gameplay_rows += 1
        p = player(record, player_index)
        if p.get("found"):
            found_rows += 1

        mask = held(record, player_index)
        if mask is not None:
            input_rows += 1
            if mask:
                nonzero_rows += 1
            held_counts[f"0x{mask:03X}"] += 1

        left = pressed(mask, LEFT_BIT)
        right = pressed(mask, RIGHT_BIT)
        direction = -1 if left and not right else 1 if right and not left else 0
        summary = tile_summary(p)
        contact = p.get("contact") or {}
        wall_left = bool(num(summary.get("wallLeft")) or num(contact.get("wallLeft")))
        wall_right = bool(num(summary.get("wallRight")) or num(contact.get("wallRight")))
        hole_left = bool(num(summary.get("effectiveHoleLeft"), summary.get("holeLeft")))
        hole_right = bool(num(summary.get("effectiveHoleRight"), summary.get("holeRight")))
        hole_ahead = bool(num(summary.get("effectiveHoleAhead"), summary.get("holeAhead")))

        if left and wall_left or right and wall_right:
            blocked_input_rows += 1
            append_sample(
                samples["blockedInputs"],
                sample_limit,
                {"frame": frame, "held": f"0x{mask or 0:03X}", "x": pos_x(p), "y": pos_y(p),
                 "wallLeft": int(wall_left), "wallRight": int(wall_right)},
            )
        if left and hole_left or right and hole_right or (direction != 0 and hole_ahead):
            hole_input_rows += 1
            append_sample(
                samples["holeInputs"],
                sample_limit,
                {"frame": frame, "held": f"0x{mask or 0:03X}", "x": pos_x(p), "y": pos_y(p),
                 "holeAhead": int(hole_ahead), "holeLeft": int(hole_left), "holeRight": int(hole_right)},
            )
        if direction != 0 and prev_dir != 0 and direction != prev_dir:
            left_right_flips += 1
            append_sample(
                samples["leftRightFlips"],
                sample_limit,
                {"frame": frame, "prevHeld": f"0x{prev_mask or 0:03X}", "held": f"0x{mask or 0:03X}",
                 "x": pos_x(p), "y": pos_y(p)},
            )
        if direction != 0:
            prev_dir = direction
        prev_mask = mask

        if prev_record is not None:
            prev_p = player(prev_record, player_index)
            if p.get("dead") and not prev_p.get("dead"):
                death_transitions += 1
                append_sample(samples["deathTransitions"], sample_limit, {"frame": frame, "x": pos_x(p), "y": pos_y(p)})

        if p.get("found") and direction != 0:
            if still_run:
                last = still_run[-1]
                if abs(pos_x(p) - last["x"]) <= 0x300 and abs(pos_y(p) - last["y"]) <= 0x800:
                    still_run.append({"frame": frame, "x": pos_x(p), "y": pos_y(p), "held": f"0x{mask or 0:03X}"})
                else:
                    still_run = [{"frame": frame, "x": pos_x(p), "y": pos_y(p), "held": f"0x{mask or 0:03X}"}]
            else:
                still_run = [{"frame": frame, "x": pos_x(p), "y": pos_y(p), "held": f"0x{mask or 0:03X}"}]
            if len(still_run) == stuck_records:
                stuck_windows += 1
                append_sample(
                    samples["stuckWindows"],
                    sample_limit,
                    {"startFrame": still_run[0]["frame"], "endFrame": still_run[-1]["frame"],
                     "x": still_run[-1]["x"], "y": still_run[-1]["y"], "held": still_run[-1]["held"]},
                )
        else:
            still_run = []

        if num(contact.get("ground")) and not num(summary.get("effectiveGroundBelowSolid")):
            append_sample(
                samples["stateContradictions"],
                sample_limit,
                {"frame": frame, "kind": "ground_without_effective_ground", "x": pos_x(p), "y": pos_y(p)},
            )

        prev_record = record

    return {
        "schema": "nsmb_mvl_ai_ruleai_audit_v1",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "input": str(path),
        "player": player_index,
        "rows": rows,
        "gameplayRows": gameplay_rows,
        "playerFoundRows": found_rows,
        "inputRows": input_rows,
        "nonzeroInputRows": nonzero_rows,
        "heldTop": [{"value": value, "count": count} for value, count in held_counts.most_common(10)],
        "deathTransitions": death_transitions,
        "blockedInputRows": blocked_input_rows,
        "holeInputRows": hole_input_rows,
        "leftRightFlips": left_right_flips,
        "stuckWindows": stuck_windows,
        "samples": samples,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sample-limit", type=int, default=12)
    parser.add_argument("--stuck-records", type=int, default=6)
    args = parser.parse_args()

    report = audit(args.input, args.player, args.sample_limit, args.stuck_records)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
