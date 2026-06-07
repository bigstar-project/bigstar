#!/usr/bin/env python3
"""Print a compact human-readable summary of an NSMB MvL AI play log."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


BUTTONS = [
    ("A", 0),
    ("B", 1),
    ("R", 4),
    ("L", 5),
    ("U", 6),
    ("D", 7),
    ("Y", 11),
]


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def pos_text(entity: dict[str, Any]) -> str:
    pos = entity.get("pos") or {}
    return f"{num(pos.get('x'))//4096:5d},{num(pos.get('y'))//4096:5d}"


def buttons_text(held: int) -> str:
    names = [name for name, bit in BUTTONS if held & (1 << bit)]
    return "".join(names) if names else "-"


def nearest_text(record: dict[str, Any], player: int, category: str) -> str:
    visual = record.get("visualSummary") or {}
    for entry in visual.get("nearest") or []:
        if num(entry.get("player"), -1) != player:
            continue
        item = (entry.get("categories") or {}).get(category) or {}
        if not item.get("found"):
            return "-"
        return f"{num(item.get('dx'))//4096:5d},{num(item.get('dy'))//4096:5d}"
    return "-"


def iter_records(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlog", type=Path)
    parser.add_argument("--limit", type=int, default=40)
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    args = parser.parse_args()

    print(
        "frame st p input self(x,y) opp(x,y) star(dx,dy) hazard(dx,dy) "
        "visX obj active counts"
    )
    printed = 0
    for record in iter_records(args.playlog):
        players = record.get("players") or [{}, {}]
        player = args.player
        opponent = player ^ 1
        applied = ((record.get("inputs") or {}).get(f"appliedPlayer{player}") or {})
        held = num(applied.get("held"))
        visual = record.get("visualSummary") or {}
        counts = visual.get("categoryCounts") or {}
        counts_text = ",".join(
            f"{name}:{num(counts.get(name))}"
            for name in ["player", "big_star_actor", "world_item", "moving_hazard", "enemy_koopa"]
            if num(counts.get(name)) > 0
        )
        if not counts_text:
            counts_text = "-"
        print(
            f"{num(record.get('frame')):5d} "
            f"{num((record.get('stage') or {}).get('id')):2d} "
            f"{player} "
            f"{buttons_text(held):5s} "
            f"{pos_text(players[player]):>11s} "
            f"{pos_text(players[opponent]):>11s} "
            f"{nearest_text(record, player, 'big_star_actor'):>11s} "
            f"{nearest_text(record, player, 'moving_hazard'):>13s} "
            f"{num(visual.get('visibleCamera0X')):4d}/{num(visual.get('visibleCamera1X')):<4d} "
            f"{num((record.get('objectSummary') or {}).get('active')):3d} "
            f"{counts_text}"
        )
        printed += 1
        if printed >= args.limit:
            break
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
