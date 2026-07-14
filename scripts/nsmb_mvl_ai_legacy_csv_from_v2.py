#!/usr/bin/env python3
"""Restore legacy imitation CSV rows embedded in compact observation v2 records."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
from pathlib import Path
from typing import Any, Iterable, TextIO


def open_text(path: Path) -> TextIO:
    if path.name.lower().endswith(".gz"):
        return gzip.open(path, "rt", encoding="utf-8")
    return path.open("r", encoding="utf-8")


def iter_rows(path: Path, player: int, require_player_found: bool) -> Iterable[dict[str, Any]]:
    player_key = f"player{player}"
    with open_text(path) as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            record = json.loads(line)
            if record.get("schema") != "nsmb_mvl_compact_observation_v2":
                raise ValueError(f"{path}:{line_no}: expected compact observation v2")
            row = ((record.get("legacyFeatureRows") or {}).get(player_key)) or None
            if row is not None:
                if require_player_found and not int(row.get("self_found", 0) or 0):
                    continue
                yield row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="compact observation v2 JSONL(.gz) with --include-legacy-features")
    parser.add_argument("output", type=Path, help="legacy CSV output")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--require-player-found", action="store_true")
    args = parser.parse_args()

    rows = list(iter_rows(args.input, args.player, args.require_player_found))
    frame_indices_by_recording: dict[int, int] = {}
    for row in rows:
        recording_index = int(row.get("recording_index", 0) or 0)
        next_index = frame_indices_by_recording.get(recording_index, 0)
        row["recording_frame_index"] = next_index
        frame_indices_by_recording[recording_index] = next_index + 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        args.output.write_text("", encoding="utf-8")
        print("rows=0")
        return 0
    fieldnames = list(rows[0].keys())
    with args.output.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"rows={len(rows)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
