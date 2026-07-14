#!/usr/bin/env python3
"""Catalog StageLayout tileProbe samples from NSMB MvL AI play logs."""

from __future__ import annotations

import argparse
import json
from collections import Counter
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


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def tile_key(sample: dict[str, Any]) -> tuple[str, int, int, int, int, int, int, int]:
    tile = sample.get("tile") or {}
    block = sample.get("block") or {}
    return (
        str(sample.get("name", "?")),
        num(sample.get("status")),
        num(sample.get("tileId")),
        num(sample.get("behavior")),
        num(tile.get("lowType")),
        num(sample.get("solidish")),
        num(block.get("any")),
        num(block.get("storageContents")),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlog", type=Path)
    parser.add_argument("--player", type=int, default=-1, help="-1 means all players")
    parser.add_argument("--limit", type=int, default=30)
    args = parser.parse_args()

    counter: Counter[tuple[str, int, int, int, int, int, int, int, int, int, int]] = Counter()
    for record in load_records(args.playlog):
        for index, player in enumerate(record.get("players") or []):
            if args.player >= 0 and index != args.player:
                continue
            contact = player.get("contact") or {}
            summary = ((player.get("tileProbe") or {}).get("summary")) or {}
            contact_ground = num(summary.get("contactGround"), num(contact.get("ground")))
            effective_ground = num(summary.get("effectiveGroundBelowSolid"))
            suppressed = num(summary.get("holeSuppressedByContact"))
            for sample in ((player.get("tileProbe") or {}).get("samples")) or []:
                key = tile_key(sample) + (contact_ground, effective_ground, suppressed)
                counter[key] += 1

    print(
        "count sample status tileId behavior lowType solid block contents "
        "contactGround effectiveGround suppressed"
    )
    for key, count in counter.most_common(args.limit):
        (
            sample_name,
            status,
            tile_id,
            behavior,
            low_type,
            solidish,
            block_any,
            contents,
            contact_ground,
            effective_ground,
            suppressed,
        ) = key
        print(
            f"{count:5d} {sample_name:12s} {status:2d} "
            f"0x{tile_id:03X} 0x{behavior:08X} 0x{low_type:02X} "
            f"{solidish:d} {block_any:d} {contents:4d} "
            f"{contact_ground:d} {effective_ground:d} {suppressed:d}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
