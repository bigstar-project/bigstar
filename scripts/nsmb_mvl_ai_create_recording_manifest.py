#!/usr/bin/env python3
"""Create a metadata manifest for an NSMB MvL AI/human play recording."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


INTERESTING_CATEGORIES = [
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
    "enemy_goomba",
    "enemy_koopa",
    "platform",
    "item_spawn_effect",
    "projectile",
    "player_fireball",
    "enemy_fireball",
]


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def rel(path: Path | None, base: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path)


def iter_records(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def player_summary(player: dict[str, Any]) -> dict[str, Any]:
    pos = player.get("pos") or {}
    return {
        "found": bool(player.get("found")),
        "x": num(pos.get("x")),
        "y": num(pos.get("y")),
        "z": num(pos.get("z")),
        "powerup": num(player.get("powerup")),
        "dead": num(player.get("dead")),
        "battleStars": num(player.get("battleStars")),
        "coins": num(player.get("coins")),
    }


def label_value(record: dict[str, Any], player: int, source: str) -> int | None:
    inputs = record.get("inputs") or {}
    applied = inputs.get(f"appliedPlayer{player}") or {}
    if source == "applied":
        return num(applied.get("held")) if applied.get("valid") else None
    if source == "player":
        return num((inputs.get(f"player{player}") or {}).get("held"))
    if source == "console":
        return num((inputs.get(f"console{player}") or {}).get("held"))
    if applied.get("valid"):
        return num(applied.get("held"))
    return num((inputs.get(f"player{player}") or {}).get("held"))


def summarize(playlog: Path, player: int, label_source: str) -> dict[str, Any]:
    first: dict[str, Any] | None = None
    last: dict[str, Any] | None = None
    prev_players: list[dict[str, Any]] | None = None
    rows = 0
    gameplay_rows = 0
    player_found_rows = 0
    label_rows = 0
    nonzero_label_rows = 0
    category_frames = {name: 0 for name in INTERESTING_CATEGORIES}
    block_candidate_frames = 0
    event_counts = {
        "starPickup": 0,
        "coinChange": 0,
        "powerupChange": 0,
        "playerDeath": 0,
        "blockCandidateVisible": 0,
        "itemVisible": 0,
        "projectileVisible": 0,
    }

    for record in iter_records(playlog):
        rows += 1
        if first is None:
            first = record
        last = record
        if record.get("inGameplay"):
            gameplay_rows += 1
        players = record.get("players") or []
        if len(players) > player and players[player].get("found"):
            player_found_rows += 1
        held = label_value(record, player, label_source)
        if held is not None:
            label_rows += 1
            if held:
                nonzero_label_rows += 1

        categories = {str(obj.get("category")) for obj in record.get("objects") or []}
        for name in category_frames:
            if name in categories:
                category_frames[name] += 1
        if categories.intersection({"world_item", "neutral_item", "dropped_star_item", "item"}):
            event_counts["itemVisible"] += 1
        if categories.intersection({"projectile", "player_fireball", "enemy_fireball"}):
            event_counts["projectileVisible"] += 1

        block_visible = False
        for p in players:
            for sample in ((p.get("tileProbe") or {}).get("samples") or []):
                block = sample.get("block") or {}
                if block.get("any") or block.get("itemBox"):
                    block_visible = True
                    break
            if block_visible:
                break
        if block_visible:
            block_candidate_frames += 1
            event_counts["blockCandidateVisible"] += 1

        current_players = [player_summary(p) for p in players[:2]]
        if prev_players and len(prev_players) == len(current_players):
            for before, after in zip(prev_players, current_players):
                if after["battleStars"] > before["battleStars"]:
                    event_counts["starPickup"] += 1
                if after["coins"] != before["coins"]:
                    event_counts["coinChange"] += 1
                if after["powerup"] != before["powerup"]:
                    event_counts["powerupChange"] += 1
                if after["dead"] and not before["dead"]:
                    event_counts["playerDeath"] += 1
        prev_players = current_players

    if first is None or last is None:
        raise ValueError(f"{playlog}: no records")

    return {
        "rows": rows,
        "gameplayRows": gameplay_rows,
        "playerFoundRows": player_found_rows,
        "labelRows": label_rows,
        "nonzeroLabelRows": nonzero_label_rows,
        "frameStart": num(first.get("frame")),
        "frameEnd": num(last.get("frame")),
        "hashStart": first.get("hash"),
        "hashEnd": last.get("hash"),
        "stageStart": first.get("stage"),
        "stageEnd": last.get("stage"),
        "playersEnd": [player_summary(p) for p in (last.get("players") or [])[:2]],
        "objectSummaryEnd": last.get("objectSummary"),
        "categoryFrames": category_frames,
        "blockCandidateFrames": block_candidate_frames,
        "eventCounts": event_counts,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlog", type=Path, help="AI/human play log JSONL")
    parser.add_argument("output", type=Path, help="recording manifest JSON")
    parser.add_argument("--kind", choices=["human", "rule_ai", "replay", "self_play"], default="human")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--label-source", choices=["auto", "applied", "player", "console"], default="player")
    parser.add_argument("--stage", type=int, default=0)
    parser.add_argument("--host-input-script", type=Path)
    parser.add_argument("--client-input-script", type=Path)
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--stdout", type=Path)
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    base = args.output.parent
    base.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": "nsmb_mvl_ai_recording_manifest_v1",
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "kind": args.kind,
        "stageScope": args.stage,
        "player": args.player,
        "labelSource": args.label_source,
        "playLog": rel(args.playlog, base),
        "hostInputScript": rel(args.host_input_script, base),
        "clientInputScript": rel(args.client_input_script, base),
        "logDir": rel(args.log_dir, base),
        "stdout": rel(args.stdout, base),
        "notes": args.notes,
        "summary": summarize(args.playlog, args.player, args.label_source),
    }
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"manifest={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
