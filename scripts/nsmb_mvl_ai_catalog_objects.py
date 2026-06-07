#!/usr/bin/env python3
"""Catalog active objects observed in NSMB MvL AI play logs."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
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


def iter_records(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlog", type=Path)
    parser.add_argument("--limit", type=int, default=80)
    args = parser.parse_args()

    catalog: dict[tuple[str, str], dict[str, Any]] = defaultdict(
        lambda: {
            "category": "",
            "count": 0,
            "first": 0,
            "last": 0,
            "in_view_x": 0,
            "bases": set(),
            "guids": set(),
            "min_p0dx": None,
            "max_p0dx": None,
            "min_p1dx": None,
            "max_p1dx": None,
        }
    )

    for record in iter_records(args.playlog):
        frame = num(record.get("frame"))
        for obj in record.get("objects") or []:
            key = (obj.get("objectId", "0x000"), obj.get("settings", "0x00000000"))
            entry = catalog[key]
            entry["category"] = obj.get("category", "object")
            entry["count"] += 1
            entry["first"] = frame if entry["count"] == 1 else min(entry["first"], frame)
            entry["last"] = max(entry["last"], frame)
            entry["bases"].add(obj.get("base", "0x00000000"))
            entry["guids"].add(obj.get("guid", "0x00000000"))
            screen = ((obj.get("screen") or {}).get("camera0") or {})
            if screen.get("inViewX"):
                entry["in_view_x"] += 1
            rel = obj.get("relative") or {}
            for name in ["p0dx", "p1dx"]:
                value = num(rel.get(name))
                min_name = f"min_{name}"
                max_name = f"max_{name}"
                entry[min_name] = value if entry[min_name] is None else min(entry[min_name], value)
                entry[max_name] = value if entry[max_name] is None else max(entry[max_name], value)

    rows = sorted(
        catalog.items(),
        key=lambda item: (-item[1]["count"], item[0][0], item[0][1]),
    )
    print("objectId settings category count first last inViewX bases guids p0dxRange p1dxRange")
    for (object_id, settings), entry in rows[: args.limit]:
        print(
            f"{object_id:>7s} {settings:>10s} {entry['category']:<20s} "
            f"{entry['count']:5d} {entry['first']:5d} {entry['last']:5d} "
            f"{entry['in_view_x']:7d} {len(entry['bases']):5d} {len(entry['guids']):5d} "
            f"{entry['min_p0dx']//4096 if entry['min_p0dx'] is not None else 0:5d}.."
            f"{entry['max_p0dx']//4096 if entry['max_p0dx'] is not None else 0:<5d} "
            f"{entry['min_p1dx']//4096 if entry['min_p1dx'] is not None else 0:5d}.."
            f"{entry['max_p1dx']//4096 if entry['max_p1dx'] is not None else 0:<5d}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
