#!/usr/bin/env python3
"""Render one NSMB MvL AI play-log frame as a player-centered SVG snapshot."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
from typing import Any


FIXED = 4096
WIDTH = 900
HEIGHT = 520
CENTER_X = WIDTH // 2
CENTER_Y = HEIGHT // 2

CATEGORY_STYLE = {
    "player": ("#2563eb", "P"),
    "big_star_actor": ("#f59e0b", "S"),
    "big_star_related": ("#facc15", "R"),
    "big_star_candidate": ("#fbbf24", "s"),
    "world_item": ("#10b981", "I"),
    "neutral_item": ("#34d399", "i"),
    "dropped_star_item": ("#f97316", "D"),
    "item": ("#22c55e", "i"),
    "coin": ("#eab308", "C"),
    "moving_hazard": ("#ef4444", "H"),
    "hazard": ("#dc2626", "H"),
    "enemy_goomba": ("#92400e", "G"),
    "enemy_koopa": ("#15803d", "K"),
    "platform": ("#64748b", "F"),
    "warp_entrance": ("#7c3aed", "W"),
    "item_spawn_effect": ("#fb7185", "E"),
    "object": ("#94a3b8", "O"),
}


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def buttons_text(held: int) -> str:
    buttons = [("A", 0), ("B", 1), ("R", 4), ("L", 5), ("U", 6), ("D", 7), ("Y", 11)]
    names = [name for name, bit in buttons if held & (1 << bit)]
    return "".join(names) if names else "-"


def contact_text(player: dict[str, Any]) -> str:
    contact = player.get("contact") or {}
    names = []
    for key, label in [
        ("ground", "G"),
        ("wallLeft", "WL"),
        ("wallRight", "WR"),
        ("ceiling", "C"),
        ("submerged", "W"),
        ("quicksand", "Q"),
        ("rope", "R"),
        ("spikesLeft", "S"),
        ("spikesRight", "S"),
    ]:
        if num(contact.get(key)):
            names.append(label)
    return "+".join(dict.fromkeys(names)) if names else "-"


def tile_probe_summary_text(player: dict[str, Any]) -> str:
    summary = ((player.get("tileProbe") or {}).get("summary")) or {}
    tags = []
    pairs = [
        ("wallAhead", "wall"),
        ("effectiveHoleAhead", "hole"),
        ("effectiveGroundBelowSolid", "ground"),
        ("holeSuppressedByContact", "suppress"),
        ("aheadBodySolid", "aheadBody"),
        ("aheadBelowSolid", "aheadBelow"),
        ("wallLeft", "wallLeft"),
        ("effectiveHoleLeft", "holeLeft"),
        ("wallRight", "wallRight"),
        ("effectiveHoleRight", "holeRight"),
    ]
    for key, label in pairs:
        if num(summary.get(key)):
            tags.append(label)
    if not tags:
        for key, label in [
            ("holeAhead", "hole"),
            ("groundBelowSolid", "ground"),
            ("holeLeft", "holeLeft"),
            ("holeRight", "holeRight"),
        ]:
            if num(summary.get(key)):
                tags.append(label)
    return "+".join(tags) if tags else "-"


def visual_state_text(player: dict[str, Any]) -> str:
    visual_state = player.get("visualState") or {}
    powerup = visual_state.get("powerup") or {}
    inventory = visual_state.get("inventoryPowerup") or {}
    parts = [
        f"pwr={powerup.get('name', player.get('powerup', '?'))}",
        f"inv={inventory.get('name', player.get('inventoryPowerup', '?'))}",
    ]
    if num(visual_state.get("hasReserveItemCandidate")):
        parts.append("reserve")
    if num(visual_state.get("invincibleKnown")):
        parts.append(f"invincible={num(visual_state.get('invincibleCandidate'))}")
    else:
        parts.append("invincible=?")
    return " ".join(str(part) for part in parts)


def pos(entity: dict[str, Any]) -> dict[str, int]:
    value = entity.get("pos") or {}
    return {"x": num(value.get("x")), "y": num(value.get("y"))}


def world_delta(a: dict[str, int], b: dict[str, int]) -> tuple[float, float]:
    return ((a["x"] - b["x"]) / FIXED, (a["y"] - b["y"]) / FIXED)


def svg_point(dx_px: float, dy_px: float) -> tuple[float, float]:
    return (CENTER_X + dx_px, CENTER_Y + dy_px)


def iter_records(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


def choose_record(path: Path, frame: int | None, player: int) -> dict[str, Any]:
    best: dict[str, Any] | None = None
    best_delta: int | None = None
    for record in iter_records(path):
        players = record.get("players") or [{}, {}]
        if len(players) <= player or not players[player].get("found"):
            continue
        if frame is None:
            return record
        delta = abs(num(record.get("frame")) - frame)
        if best is None or best_delta is None or delta < best_delta:
            best = record
            best_delta = delta
    if best is None:
        raise ValueError("no record with the selected player found")
    return best


def render(record: dict[str, Any], player: int, max_objects: int) -> str:
    players = record.get("players") or [{}, {}]
    self_player = players[player]
    opponent = players[player ^ 1]
    self_pos = pos(self_player)
    opponent_pos = pos(opponent)
    applied = ((record.get("inputs") or {}).get(f"appliedPlayer{player}") or {})
    held = num(applied.get("held"))

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="#0f172a"/>',
        f'<line x1="{CENTER_X}" y1="0" x2="{CENTER_X}" y2="{HEIGHT}" stroke="#334155" stroke-width="1"/>',
        f'<line x1="0" y1="{CENTER_Y}" x2="{WIDTH}" y2="{CENTER_Y}" stroke="#334155" stroke-width="1"/>',
        '<rect x="8" y="8" width="884" height="76" rx="6" fill="#111827" stroke="#334155"/>',
        f'<text x="20" y="32" fill="#e5e7eb" font-family="monospace" font-size="16">frame {num(record.get("frame"))} player {player} input {html.escape(buttons_text(held))} contact {html.escape(contact_text(self_player))}</text>',
        f'<text x="20" y="56" fill="#9ca3af" font-family="monospace" font-size="13">state {html.escape(visual_state_text(self_player))}, tileProbe {html.escape(tile_probe_summary_text(self_player))}</text>',
    ]

    def draw_marker(x: float, y: float, color: str, label: str, title: str, radius: int = 8) -> None:
        safe_title = html.escape(title)
        safe_label = html.escape(label)
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius}" fill="{color}" stroke="#f8fafc" stroke-width="1">')
        parts.append(f"<title>{safe_title}</title></circle>")
        parts.append(
            f'<text x="{x:.1f}" y="{y + 4:.1f}" text-anchor="middle" fill="#0f172a" '
            f'font-family="monospace" font-size="10" font-weight="700">{safe_label}</text>'
        )

    draw_marker(CENTER_X, CENTER_Y, "#38bdf8", "ME", "selected player", 11)
    if opponent.get("found"):
        dx, dy = world_delta(opponent_pos, self_pos)
        x, y = svg_point(dx, dy)
        draw_marker(x, y, "#818cf8", "OP", f"opponent dx={dx:.0f} dy={dy:.0f}", 10)

    for sample in ((self_player.get("tileProbe") or {}).get("samples")) or []:
        if not num(sample.get("found")):
            continue
        sample_pos = {"x": num(sample.get("worldX")), "y": num(sample.get("worldY"))}
        dx, dy = world_delta(sample_pos, self_pos)
        x, y = svg_point(dx, dy)
        tile = sample.get("tile") or {}
        if num(tile.get("harmful")):
            color = "#ef4444"
        elif num(tile.get("coin")):
            color = "#eab308"
        elif num(tile.get("questionBlock")) or num(tile.get("brickBlock")) or num(tile.get("breakableBlock")):
            color = "#f97316"
        elif num(sample.get("solidish")):
            color = "#22c55e"
        else:
            color = "#475569"
        name = str(sample.get("name", "?"))
        tile_id = num(sample.get("tileId"))
        behavior = sample.get("behavior", "0")
        status = num(sample.get("status"))
        low_type = num(tile.get("lowType"))
        block = sample.get("block") or {}
        block_text = ""
        if num(block.get("any")):
            block_text = (
                f" block itemBox={num(block.get('itemBox'))}"
                f" contents={num(block.get('storageContents'))}"
            )
        parts.append(
            f'<rect x="{x - 5:.1f}" y="{y - 5:.1f}" width="10" height="10" fill="{color}" '
            f'stroke="#e2e8f0" stroke-width="1">'
            f'<title>tileProbe {html.escape(name)} status={status} tile=0x{tile_id:03X} behavior={html.escape(str(behavior))} low=0x{low_type:02X}{html.escape(block_text)} dx={dx:.0f} dy={dy:.0f}</title></rect>'
        )
        parts.append(
            f'<text x="{x:.1f}" y="{y - 8:.1f}" text-anchor="middle" fill="#cbd5e1" '
            f'font-family="monospace" font-size="8">{html.escape(name[:2])}</text>'
        )

    drawn = 0
    for obj in record.get("objects") or []:
        if drawn >= max_objects:
            break
        category = obj.get("category", "object")
        if category == "player":
            continue
        rel = obj.get("relative") or {}
        dx = num(rel.get(f"p{player}dx")) / FIXED
        dy = num(rel.get(f"p{player}dy")) / FIXED
        x, y = svg_point(dx, dy)
        if x < -40 or x > WIDTH + 40 or y < -40 or y > HEIGHT + 40:
            continue
        color, label = CATEGORY_STYLE.get(category, CATEGORY_STYLE["object"])
        draw_marker(
            x,
            y,
            color,
            label,
            f"{category} id={obj.get('objectId')} settings={obj.get('settings')} dx={dx:.0f} dy={dy:.0f}",
            7,
        )
        drawn += 1

    for slot in (((record.get("specialObjects") or {}).get("fireballs") or {}).get("slots")) or []:
        rel = slot.get("relative") or {}
        dx = num(rel.get(f"p{player}dx")) / FIXED
        dy = num(rel.get(f"p{player}dy")) / FIXED
        x, y = svg_point(dx, dy)
        if x < -40 or x > WIDTH + 40 or y < -40 or y > HEIGHT + 40:
            continue
        owner = num(slot.get("ownerCandidate"), -1)
        confidence = num(slot.get("ownerConfidence"))
        color = "#fb923c" if owner == player else "#f43f5e"
        label = "FB" if owner == player else "fb"
        draw_marker(
            x,
            y,
            color,
            label,
            (
                f"fireball slot={slot.get('index')} ownerCandidate={owner}"
                f" confidence={confidence} kind={slot.get('kind')} state={slot.get('state')}"
                f" facing={slot.get('facing')} dx={dx:.0f} dy={dy:.0f}"
            ),
            6,
        )

    legend_x = 20
    legend_y = HEIGHT - 24
    for category, (color, label) in CATEGORY_STYLE.items():
        if category in {"object", "player"}:
            continue
        parts.append(f'<circle cx="{legend_x}" cy="{legend_y}" r="6" fill="{color}"/>')
        parts.append(
            f'<text x="{legend_x + 10}" y="{legend_y + 4}" fill="#cbd5e1" '
            f'font-family="monospace" font-size="11">{html.escape(label)}={html.escape(category)}</text>'
        )
        legend_x += 128
        if legend_x > WIDTH - 120:
            legend_x = 20
            legend_y -= 18

    parts.append("</svg>")
    return "\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlog", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--frame", type=int)
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--max-objects", type=int, default=48)
    args = parser.parse_args()

    record = choose_record(args.playlog, args.frame, args.player)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(record, args.player, args.max_objects), encoding="utf-8")
    print(f"frame={num(record.get('frame'))} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
