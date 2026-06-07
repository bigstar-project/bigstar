#!/usr/bin/env python3
"""Export compact JSON for the NSMB MvL AI replay viewer."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


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
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def compact_player(player: dict[str, Any]) -> dict[str, Any]:
    return {
        "found": bool(player.get("found")),
        "pos": player.get("pos"),
        "vel": player.get("vel"),
        "powerup": num(player.get("powerup")),
        "dead": num(player.get("dead")),
        "battleStars": num(player.get("battleStars")),
        "coins": num(player.get("coins")),
        "contact": player.get("contact"),
        "screen": player.get("screen"),
        "fallRisk": player.get("fallRisk"),
        "tileProbe": player.get("tileProbe"),
    }


def compact_object(obj: dict[str, Any]) -> dict[str, Any]:
    return {
        "category": obj.get("category"),
        "objectId": obj.get("objectId"),
        "settings": obj.get("settings"),
        "guid": obj.get("guid"),
        "pos": obj.get("pos"),
        "vel": obj.get("vel"),
        "relative": obj.get("relative"),
        "screen": obj.get("screen"),
    }


def compact_record(record: dict[str, Any], max_objects: int) -> dict[str, Any]:
    objects = record.get("objects") or []
    return {
        "frame": num(record.get("frame")),
        "instance": num(record.get("instance")),
        "role": record.get("role"),
        "inGameplay": bool(record.get("inGameplay")),
        "hash": record.get("hash"),
        "stage": record.get("stage"),
        "inputs": record.get("inputs"),
        "players": [compact_player(player) for player in (record.get("players") or [])[:2]],
        "targets": record.get("targets"),
        "camera": record.get("camera"),
        "objectSummary": record.get("objectSummary"),
        "visualSummary": record.get("visualSummary"),
        "objects": [compact_object(obj) for obj in objects[:max_objects]],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="AI/human play log JSONL")
    parser.add_argument("output", type=Path, help="viewer JSON")
    parser.add_argument("--max-frames", type=int, default=2000)
    parser.add_argument("--max-objects", type=int, default=64)
    parser.add_argument("--min-frame", type=int, default=0)
    args = parser.parse_args()

    frames = []
    for record in iter_records(args.input):
        if num(record.get("frame")) < args.min_frame:
            continue
        frames.append(compact_record(record, args.max_objects))
        if len(frames) >= args.max_frames:
            break

    data = {
        "schema": "nsmb_mvl_ai_viewer_data_v1",
        "source": str(args.input),
        "frameCount": len(frames),
        "frames": frames,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"frames={len(frames)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
