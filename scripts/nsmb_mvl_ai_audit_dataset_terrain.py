#!/usr/bin/env python3
"""Audit terrain features in an NSMB MvL AI dataset CSV."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


SUMMARY_NAMES = [
    "groundBelowSolid",
    "aheadBodySolid",
    "aheadFeetSolid",
    "aheadBelowSolid",
    "ahead2BelowSolid",
    "wallAhead",
    "holeAhead",
    "wallLeft",
    "holeLeft",
    "wallRight",
    "holeRight",
    "contactGround",
    "effectiveGroundBelowSolid",
    "holeSuppressedByContact",
    "effectiveHoleAhead",
    "effectiveHoleLeft",
    "effectiveHoleRight",
]

PROBE_SAMPLES = [
    "center",
    "feet",
    "below",
    "aheadBody",
    "aheadFeet",
    "aheadBelow",
    "ahead2Feet",
    "ahead2Below",
    "above",
    "leftBody",
    "leftFeet",
    "leftBelow",
    "left2Below",
    "rightBody",
    "rightFeet",
    "rightBelow",
    "right2Below",
]


def num(value: Any, default: int = 0) -> int:
    if value is None or value == "":
        return default
    try:
        return int(str(value), 0)
    except ValueError:
        return default


def has_column(row: dict[str, str], name: str) -> bool:
    return name in row


def append_example(target: list[dict[str, Any]], limit: int, row: dict[str, str], extra: dict[str, Any]) -> None:
    if len(target) >= limit:
        return
    sample = {
        "frame": num(row.get("frame")),
        "recordingIndex": num(row.get("recording_index")),
        "recordingFrameIndex": num(row.get("recording_frame_index")),
    }
    sample.update(extra)
    target.append(sample)


def audit_prefix(row: dict[str, str], prefix: str, report: dict[str, Any], sample_limit: int) -> None:
    p = report["players"][prefix]
    left_wall = num(row.get(f"{prefix}_tile_probe_wallLeft"))
    right_wall = num(row.get(f"{prefix}_tile_probe_wallRight"))
    left_body = num(row.get(f"{prefix}_tile_probe_leftBody_solidish"))
    right_body = num(row.get(f"{prefix}_tile_probe_rightBody_solidish"))
    left_feet = num(row.get(f"{prefix}_tile_probe_leftFeet_solidish"))
    right_feet = num(row.get(f"{prefix}_tile_probe_rightFeet_solidish"))
    contact_left = num(row.get(f"{prefix}_contact_wallLeft"))
    contact_right = num(row.get(f"{prefix}_contact_wallRight"))

    if left_wall and not left_body and not contact_left and left_feet:
        p["sideWallFeetOnlyEvents"] += 1
        append_example(
            p["examples"]["sideWallFeetOnly"],
            sample_limit,
            row,
            {
                "side": "left",
                "wall": left_wall,
                "bodySolidish": left_body,
                "feetSolidish": left_feet,
                "contactWall": contact_left,
                "feetTileId": num(row.get(f"{prefix}_tile_probe_leftFeet_tile_id")),
                "feetBehavior": num(row.get(f"{prefix}_tile_probe_leftFeet_behavior")),
            },
        )
    if right_wall and not right_body and not contact_right and right_feet:
        p["sideWallFeetOnlyEvents"] += 1
        append_example(
            p["examples"]["sideWallFeetOnly"],
            sample_limit,
            row,
            {
                "side": "right",
                "wall": right_wall,
                "bodySolidish": right_body,
                "feetSolidish": right_feet,
                "contactWall": contact_right,
                "feetTileId": num(row.get(f"{prefix}_tile_probe_rightFeet_tile_id")),
                "feetBehavior": num(row.get(f"{prefix}_tile_probe_rightFeet_behavior")),
            },
        )

    for side in ["Left", "Right", "Ahead"]:
        raw_col = f"{prefix}_tile_probe_raw_wall{side}"
        fixed_col = f"{prefix}_tile_probe_wall{side}"
        if has_column(row, raw_col) and num(row.get(raw_col)) != num(row.get(fixed_col)):
            p["rawWallChangedEvents"] += 1
            if num(row.get(raw_col)) and not num(row.get(fixed_col)):
                p["rawWallClearedEvents"] += 1
            if not num(row.get(raw_col)) and num(row.get(fixed_col)):
                p["rawWallAddedEvents"] += 1
            append_example(
                p["examples"]["rawWallChanged"],
                sample_limit,
                row,
                {
                    "field": f"wall{side}",
                    "raw": num(row.get(raw_col)),
                    "normalized": num(row.get(fixed_col)),
                },
            )

    if any(num(row.get(f"{prefix}_tile_probe_{name}_recomputed_diff")) for name in SUMMARY_NAMES):
        p["summaryRecomputedDiffRows"] += 1

    row_has_hidden_or_rescue = False
    row_has_storage_breakable = False
    row_has_visible_storage_breakable = False
    for sample_name in PROBE_SAMPLES:
        base = f"{prefix}_tile_probe_{sample_name}_block"
        hidden_or_rescue = num(row.get(f"{base}_hiddenOrRescueCandidate"))
        storage_breakable = num(row.get(f"{base}_storageBreakableCandidate"))
        visible_storage_breakable = num(row.get(f"{base}_visibleStorageBreakableCandidate"))
        row_has_hidden_or_rescue = row_has_hidden_or_rescue or bool(hidden_or_rescue)
        row_has_storage_breakable = row_has_storage_breakable or bool(storage_breakable)
        row_has_visible_storage_breakable = row_has_visible_storage_breakable or bool(visible_storage_breakable)
        if hidden_or_rescue:
            append_example(
                p["examples"]["hiddenOrRescueCandidate"],
                sample_limit,
                row,
                {
                    "sample": sample_name,
                    "tileId": num(row.get(f"{prefix}_tile_probe_{sample_name}_tile_id")),
                    "behavior": num(row.get(f"{prefix}_tile_probe_{sample_name}_behavior")),
                    "storageContents": num(row.get(f"{base}_storageContents")),
                    "currentTileId": num(row.get(f"{base}_currentTileId")),
                    "currentBehavior": num(row.get(f"{base}_currentBehavior")),
                },
            )

    if row_has_hidden_or_rescue:
        p["hiddenOrRescueCandidateRows"] += 1
    if row_has_storage_breakable:
        p["storageBreakableCandidateRows"] += 1
    if row_has_visible_storage_breakable:
        p["visibleStorageBreakableCandidateRows"] += 1


def empty_player_report() -> dict[str, Any]:
    return {
        "sideWallFeetOnlyEvents": 0,
        "rawWallChangedEvents": 0,
        "rawWallClearedEvents": 0,
        "rawWallAddedEvents": 0,
        "summaryRecomputedDiffRows": 0,
        "hiddenOrRescueCandidateRows": 0,
        "storageBreakableCandidateRows": 0,
        "visibleStorageBreakableCandidateRows": 0,
        "examples": {
            "sideWallFeetOnly": [],
            "rawWallChanged": [],
            "hiddenOrRescueCandidate": [],
        },
    }


def audit(path: Path, sample_limit: int) -> dict[str, Any]:
    report: dict[str, Any] = {
        "input": str(path),
        "rows": 0,
        "players": {
            "self": empty_player_report(),
            "opponent": empty_player_report(),
        },
    }
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            report["rows"] += 1
            audit_prefix(row, "self", report, sample_limit)
            audit_prefix(row, "opponent", report, sample_limit)
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sample-limit", type=int, default=20)
    args = parser.parse_args()

    report = audit(args.dataset, args.sample_limit)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
