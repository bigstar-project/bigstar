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


def contact_text(player: dict[str, Any]) -> str:
    contact = player.get("contact") or {}
    names = []
    if num(contact.get("ground")):
        names.append("G")
    if num(contact.get("predictGround")) and not num(contact.get("ground")):
        names.append("g?")
    if num(contact.get("wallLeft")):
        names.append("WL")
    if num(contact.get("wallRight")):
        names.append("WR")
    if num(contact.get("ceiling")):
        names.append("C")
    if num(contact.get("water")) or num(contact.get("liquid")) or num(contact.get("submerged")):
        names.append("W")
    if num(contact.get("quicksand")) or num(contact.get("quicksandTop")):
        names.append("Q")
    if num(contact.get("rope")) or num(contact.get("tightrope")) or num(contact.get("pole")):
        names.append("R")
    if num(contact.get("spikesLeft")) or num(contact.get("spikesRight")):
        names.append("S")
    return "+".join(names) if names else "-"


def fall_text(player: dict[str, Any]) -> str:
    fall = player.get("fallRisk") or {}
    tags = []
    if num(fall.get("belowCamera0")):
        tags.append("below")
    if num(fall.get("nearCameraBottom0")):
        tags.append("low")
    if num(fall.get("velYPositive")):
        tags.append("vy+")
    if num(fall.get("velYNegative")):
        tags.append("vy-")
    prefix = "+".join(tags) if tags else "-"
    bottom = num(fall.get("cameraBottomDistance0")) // 4096
    screen_y = num(fall.get("screenY0")) // 4096
    return f"{prefix}:{screen_y}/{bottom}"


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
        "frame st p input contact y/bot self(x,y) opp(x,y) star(dx,dy) hazard(dx,dy) "
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
            for name in [
                "player",
                "big_star_actor",
                "big_star_related",
                "world_item",
                "coin",
                "moving_hazard",
                "hazard",
                "enemy_goomba",
                "enemy_koopa",
                "platform",
                "item_spawn_effect",
            ]
            if num(counts.get(name)) > 0
        )
        if not counts_text:
            counts_text = "-"
        print(
            f"{num(record.get('frame')):5d} "
            f"{num((record.get('stage') or {}).get('id')):2d} "
            f"{player} "
            f"{buttons_text(held):5s} "
            f"{contact_text(players[player]):7s} "
            f"{fall_text(players[player]):>11s} "
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
