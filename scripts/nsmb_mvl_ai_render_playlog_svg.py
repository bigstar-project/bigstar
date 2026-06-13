#!/usr/bin/env python3
"""Render one NSMB MvL AI play-log frame as a player-centered SVG snapshot."""

from __future__ import annotations

import argparse
import gzip
import html
import json
from pathlib import Path
from typing import Any


FIXED = 4096
WIDTH = 900
HEIGHT = 520
CENTER_X = WIDTH // 2
CENTER_Y = HEIGHT // 2
STAGE_WRAP_WIDTH_PX = 1024
TILE_GRID_SIZE_PX = 16

HIDDEN_SCENE_OBJECT_CATEGORIES = {
    "big_star_marker",
    "big_star_related",
    "camera",
    "course_select",
    "mvl_object267",
    "stage_actor_manager",
    "stage_controller",
    "stage_fx",
    "stage_layout",
    "stage_scene",
}

TILE_LABELS = {"question", "hidden", "coin", "harmful", "water", "partial"}

CATEGORY_STYLE = {
    "player": ("#2563eb", "P"),
    "big_star_actor": ("#f59e0b", "S"),
    "big_star_related": ("#facc15", "R"),
    "big_star_marker": ("#64748b", "m"),
    "world_item": ("#10b981", "I"),
    "neutral_item": ("#34d399", "i"),
    "coin_item": ("#eab308", "C"),
    "dropped_star_item": ("#facc15", "S"),
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

DROPPED_STAR_ACTOR_SETTINGS_NORMALIZED = {
    0x00001002,
    0x00001012,
    0x00001102,
    0x00001112,
}


def parse_int(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def object_category(obj: dict[str, Any]) -> str:
    category = str(obj.get("category") or "object")
    object_id = parse_int(obj.get("objectId"))
    settings = parse_int(obj.get("settings"))
    if object_id == 0x001F and settings == 0x00090002:
        return "coin_item"
    if object_id == 0x0022 and (settings & 0x7FFFFFFF) in DROPPED_STAR_ACTOR_SETTINGS_NORMALIZED:
        return "dropped_star_item"
    if object_id == 0x010C and settings == 0x00001120:
        return "big_star_marker"
    return category

TILE_KIND_STYLE = {
    "hidden": ("#a855f7", "H", "hidden/invisible block"),
    "question": ("#facc15", "?", "question/item block"),
    "breakable": ("#f97316", "B", "breakable block"),
    "brick": ("#dc2626", "R", "brick block"),
    "coin": ("#eab308", "C", "coin tile"),
    "harmful": ("#ef4444", "!", "harmful tile"),
    "water": ("#38bdf8", "W", "water"),
    "partial": ("#14b8a6", "P", "partial solid"),
    "solid": ("#22c55e", "", "solid terrain"),
    "unknown": ("#64748b", "", "status/unknown tile"),
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


def fireball_kind_name(slot: dict[str, Any]) -> str:
    explicit = slot.get("kindName")
    if explicit:
        return str(explicit)
    kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
    return {0: "player0", 1: "player1", 2: "piranha_plant", 3: "fire_bro"}.get(kind, "unknown")


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
    visual_kind = corrected_visual_powerup_kind(player)
    if visual_kind:
        parts.append(f"visualPwr={visual_kind}")
    if visual_kind == 3:
        parts.append("miniVisual")
    if num(visual_state.get("canShootFireVisualCandidate")):
        parts.append("fireVisual")
    if num(visual_state.get("invincibleKnown")):
        parts.append(
            "invincible="
            f"{num(visual_state.get('invincibleCandidate'))}"
            f"/guard={num(visual_state.get('damageGuardTimer'))}"
            f"/dmg={num(visual_state.get('damageCooldown'))}"
        )
    else:
        parts.append("invincible=?")
    shell_state = num(visual_state.get("shellState"))
    if shell_state:
        parts.append(f"shell={shell_state}")
    return " ".join(str(part) for part in parts)


def corrected_visual_powerup_kind(player: dict[str, Any]) -> int:
    visual_state = player.get("visualState") or {}
    powerup = visual_state.get("powerup") or {}
    raw_powerup = num(powerup.get("raw"), num(player.get("powerup")))
    raw_inventory = num(player.get("inventoryPowerup"))
    actor_state = num(visual_state.get("actorPowerupState"))
    actor_form = num(visual_state.get("actorPowerupFormState"))
    shell_state = num(visual_state.get("shellState"))
    if raw_powerup == 2 or actor_state == 2 or actor_form == 2:
        return 2
    if raw_powerup == 4 or shell_state:
        return 4
    if actor_state == 4 or actor_form == 4:
        return 3
    if raw_powerup == 1 or actor_state == 1 or actor_form == 1:
        return 1
    if raw_powerup == 5:
        return 5
    if raw_powerup:
        return raw_powerup
    return 0


def pos(entity: dict[str, Any]) -> dict[str, int]:
    value = entity.get("pos") or {}
    return {"x": num(value.get("x")), "y": num(value.get("y"))}


def world_delta(a: dict[str, int], b: dict[str, int]) -> tuple[float, float]:
    return ((a["x"] - b["x"]) / FIXED, (a["y"] - b["y"]) / FIXED)


def svg_point(dx_px: float, dy_px: float) -> tuple[float, float]:
    return (CENTER_X + dx_px, CENTER_Y + dy_px)


def wrapped_delta_px(a_px: float, b_px: float) -> float:
    dx = a_px - b_px
    half = STAGE_WRAP_WIDTH_PX / 2
    while dx > half:
        dx -= STAGE_WRAP_WIDTH_PX
    while dx < -half:
        dx += STAGE_WRAP_WIDTH_PX
    return dx


def tile_cell_delta_px(cell: dict[str, Any], self_pos: dict[str, int]) -> tuple[float, float]:
    if cell.get("pixelX") is not None and cell.get("pixelY") is not None:
        return (
            wrapped_delta_px(num(cell.get("pixelX")), self_pos["x"] / FIXED),
            num(cell.get("pixelY")) + self_pos["y"] / FIXED,
        )
    return (
        num(cell.get("relTileX")) * TILE_GRID_SIZE_PX,
        num(cell.get("relTileY")) * TILE_GRID_SIZE_PX,
    )


def tile_cell_kind(cell: dict[str, Any]) -> tuple[str, str, str, str]:
    tile = cell.get("tile") or {}
    block = cell.get("block") or {}
    if num(block.get("hiddenOrRescueCandidate")) or num(block.get("invisible")) or num(tile.get("invisibleBlock")):
        return ("hidden", *TILE_KIND_STYLE["hidden"])
    if num(block.get("question")) or num(tile.get("questionBlock")):
        return ("question", *TILE_KIND_STYLE["question"])
    if num(block.get("breakable")) or num(tile.get("breakableBlock")):
        return ("breakable", *TILE_KIND_STYLE["breakable"])
    if num(block.get("brick")) or num(tile.get("brickBlock")):
        return ("brick", *TILE_KIND_STYLE["brick"])
    if num(tile.get("coin")):
        return ("coin", *TILE_KIND_STYLE["coin"])
    if num(tile.get("harmful")):
        return ("harmful", *TILE_KIND_STYLE["harmful"])
    if num(tile.get("water")):
        return ("water", *TILE_KIND_STYLE["water"])
    if num(tile.get("partialSolid")):
        return ("partial", *TILE_KIND_STYLE["partial"])
    if num(cell.get("solidish")):
        return ("solid", *TILE_KIND_STYLE["solid"])
    if num(cell.get("status")):
        return ("unresolved", "", "", "unresolved tile")
    return ("", "", "", "")


def object_delta_px(obj: dict[str, Any], self_player: dict[str, Any], player: int) -> tuple[float, float]:
    rel = obj.get("relative") or {}
    dx_key = f"p{player}dx"
    dy_key = f"p{player}dy"
    if dx_key in rel or dy_key in rel:
        return (num(rel.get(dx_key)) / FIXED, -num(rel.get(dy_key)) / FIXED)
    obj_pos = pos(obj)
    self_pos = pos(self_player)
    return (wrapped_delta_px(obj_pos["x"] / FIXED, self_pos["x"] / FIXED), -(obj_pos["y"] - self_pos["y"]) / FIXED)


def state_tags(player: dict[str, Any]) -> str:
    tags = []
    if num(player.get("dead")):
        tags.append("dead")
    if num(player.get("transitioning")):
        tags.append("transition")
    contact = player.get("contact") or {}
    for key, label in [
        ("ground", "ground"),
        ("wallLeft", "wallL"),
        ("wallRight", "wallR"),
        ("ceiling", "ceiling"),
        ("submerged", "water"),
    ]:
        if num(contact.get(key)):
            tags.append(label)
    return ",".join(tags) if tags else "-"


def iter_records(path: Path) -> Any:
    opener = gzip.open if path.name.lower().endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8-sig") as f:
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
    opponent_applied = ((record.get("inputs") or {}).get(f"appliedPlayer{player ^ 1}") or {})
    opponent_held = num(opponent_applied.get("held"))
    grid = ((self_player.get("tileProbe") or {}).get("grid") or {})
    grid_cells = grid.get("cells") or []
    grid_counts: dict[str, int] = {}
    for cell in grid_cells:
        kind, _color, label, _description = tile_cell_kind(cell)
        if kind:
            grid_counts[kind] = grid_counts.get(kind, 0) + 1
        elif num(cell.get("solidish")):
            grid_counts["solid"] = grid_counts.get("solid", 0) + 1
    grid_text = " ".join(f"{key}:{value}" for key, value in sorted(grid_counts.items())) or "-"

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="#0f172a"/>',
        '<defs><filter id="labelShadow"><feDropShadow dx="1" dy="1" stdDeviation="0.8" flood-color="#020617" flood-opacity="0.9"/></filter></defs>',
        f'<line x1="{CENTER_X}" y1="0" x2="{CENTER_X}" y2="{HEIGHT}" stroke="#334155" stroke-width="1"/>',
        f'<line x1="0" y1="{CENTER_Y}" x2="{WIDTH}" y2="{CENTER_Y}" stroke="#334155" stroke-width="1"/>',
        '<rect x="8" y="8" width="884" height="108" rx="6" fill="#111827" stroke="#334155"/>',
        f'<text x="20" y="30" fill="#e5e7eb" font-family="monospace" font-size="16">frame {num(record.get("frame"))} selected=P{player} input {html.escape(buttons_text(held))} contact {html.escape(contact_text(self_player))}</text>',
        f'<text x="20" y="52" fill="#9ca3af" font-family="monospace" font-size="13">P{player} {html.escape(visual_state_text(self_player))} tags={html.escape(state_tags(self_player))} tileProbe={html.escape(tile_probe_summary_text(self_player))}</text>',
        f'<text x="20" y="72" fill="#9ca3af" font-family="monospace" font-size="13">P{player ^ 1} input {html.escape(buttons_text(opponent_held))} {html.escape(visual_state_text(opponent))} tags={html.escape(state_tags(opponent))}</text>',
        f'<text x="20" y="94" fill="#cbd5e1" font-family="monospace" font-size="13">grid {num(grid.get("loggedCells"), len(grid_cells))}/{num(grid.get("totalCells")) or "?"} cells {html.escape(grid_text)}</text>',
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

    # Draw wide tile grid first so objects and players stay readable.
    for cell in grid_cells:
        kind, color, label, description = tile_cell_kind(cell)
        if not color:
            continue
        dx, dy = tile_cell_delta_px(cell, self_pos)
        x, y = svg_point(dx, dy)
        if x < -20 or x > WIDTH + 20 or y < -20 or y > HEIGHT + 20:
            continue
        tile = cell.get("tile") or {}
        block = cell.get("block") or {}
        tile_id = num(cell.get("tileId"))
        opacity = "0.68" if label else "0.24"
        parts.append(
            f'<rect x="{x - 6.5:.1f}" y="{y - 6.5:.1f}" width="13" height="13" fill="{color}" '
            f'opacity="{opacity}" stroke="#e2e8f0" stroke-width="0.5">'
            f'<title>grid {html.escape(description)} row={cell.get("row")} col={cell.get("col")} rel=({cell.get("relTileX")},{cell.get("relTileY")}) pixel=({cell.get("pixelX")},{cell.get("pixelY")}) tile=0x{tile_id:03X} behavior={html.escape(str(cell.get("behavior")))} solidish={num(cell.get("solidish"))} question={num(block.get("question")) or num(tile.get("questionBlock"))} breakable={num(block.get("breakable")) or num(tile.get("breakableBlock"))} brick={num(block.get("brick")) or num(tile.get("brickBlock"))} hidden={num(block.get("hiddenOrRescueCandidate")) or num(block.get("invisible")) or num(tile.get("invisibleBlock"))} itemBox={num(block.get("itemBox"))} storage={num(block.get("storageContents"))} dx={dx:.0f} dy={dy:.0f}</title></rect>'
        )
        if kind in TILE_LABELS:
            parts.append(
                f'<text x="{x:.1f}" y="{y + 4:.1f}" text-anchor="middle" fill="#f8fafc" '
                f'font-family="monospace" font-size="9" font-weight="800" filter="url(#labelShadow)">{html.escape(label)}</text>'
            )

    draw_marker(CENTER_X, CENTER_Y, "#38bdf8", f"P{player}", "selected player", 12)
    if opponent.get("found"):
        dx = wrapped_delta_px(opponent_pos["x"] / FIXED, self_pos["x"] / FIXED)
        dy = -(opponent_pos["y"] - self_pos["y"]) / FIXED
        x, y = svg_point(dx, dy)
        draw_marker(x, y, "#818cf8", f"P{player ^ 1}", f"opponent dx={dx:.0f} dy={dy:.0f}", 11)

    for sample in ((self_player.get("tileProbe") or {}).get("samples")) or []:
        if not num(sample.get("found")):
            continue
        sample_pos = {"x": num(sample.get("worldX")), "y": num(sample.get("worldY"))}
        dx = wrapped_delta_px(sample_pos["x"] / FIXED, self_pos["x"] / FIXED)
        dy = -(sample_pos["y"] - self_pos["y"]) / FIXED
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
            block_invisible = num(block.get("invisible"))
            block_breakable = num(block.get("breakable"))
            block_visible = num(
                block.get(
                    "visibleSolidCandidate",
                    int(num(block.get("question")) or num(block.get("brick")) or (block_breakable and not block_invisible)),
                )
            )
            block_hidden = num(block.get("hiddenOrRescueCandidate", block_invisible))
            block_visible_storage = num(
                block.get(
                    "visibleStorageBreakableCandidate",
                    int(block_breakable and num(block.get("storageContents")) != 0 and not block_invisible),
                )
            )
            block_text = (
                f" block visible={block_visible}"
                f" hidden={block_hidden}"
                f" visibleStorageBreakable={block_visible_storage}"
                f" legacyItemBox={num(block.get('itemBox'))}"
                f" rawLowContents={num(block.get('storageContents'))}"
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
        category = object_category(obj)
        if category == "player" or category in HIDDEN_SCENE_OBJECT_CATEGORIES:
            continue
        dx, dy = object_delta_px(obj, self_player, player)
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
        dx, dy = object_delta_px(slot, self_player, player)
        x, y = svg_point(dx, dy)
        if x < -40 or x > WIDTH + 40 or y < -40 or y > HEIGHT + 40:
            continue
        owner, confidence, owner_verified = fireball_owner_info(slot)
        owner_tracked = num(slot.get("ownerTracked"))
        state_bytes = ",".join(str(num(value)) for value in (slot.get("stateBytes") or [])[:8])
        owner_source = slot.get("ownerSource") or ("slotKind" if owner_verified else "positionVelocityHeuristic")
        color = "#fb923c" if owner == player else "#f43f5e"
        label = "FBv" if owner == player and owner_verified else (
            "FBt" if owner == player and owner_tracked else ("FB" if owner == player else "fb")
        )
        draw_marker(
            x,
            y,
            color,
            label,
            (
                f"fireball slot={slot.get('index')} ownerCandidate={owner}"
                f" confidence={confidence} verified={owner_verified}"
                f" source={owner_source} tracked={owner_tracked}"
                f" statelessOwner={slot.get('statelessOwnerCandidate')}"
                f" statelessConfidence={slot.get('statelessOwnerConfidence')}"
                f" kind={slot.get('kind')} kindName={fireball_kind_name(slot)} state={slot.get('state')}"
                f" facing={slot.get('facing')} stateBytes={state_bytes} dx={dx:.0f} dy={dy:.0f}"
            ),
            6,
        )

    legend_x = 20
    legend_y = HEIGHT - 24
    for key in ["question", "breakable", "brick", "hidden", "coin", "harmful", "water", "partial", "solid"]:
        color, label, description = TILE_KIND_STYLE[key]
        legend_label = label or "S"
        parts.append(f'<rect x="{legend_x - 6}" y="{legend_y - 6}" width="12" height="12" fill="{color}" opacity="0.72" stroke="#e2e8f0" stroke-width="0.5"/>')
        parts.append(
            f'<text x="{legend_x + 10}" y="{legend_y + 4}" fill="#cbd5e1" '
            f'font-family="monospace" font-size="11">{html.escape(legend_label)}={html.escape(description)}</text>'
        )
        legend_x += 150
        if legend_x > WIDTH - 140:
            legend_x = 20
            legend_y -= 18
    legend_y -= 22
    legend_x = 20
    for category, (color, label) in CATEGORY_STYLE.items():
        if category in {"object", "player"} or category in HIDDEN_SCENE_OBJECT_CATEGORIES:
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
