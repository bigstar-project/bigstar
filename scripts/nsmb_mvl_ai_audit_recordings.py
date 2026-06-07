#!/usr/bin/env python3
"""Audit NSMB MvL AI recording manifests before imitation training."""

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


def ratio(numerator: int, denominator: int) -> float:
    return float(numerator) / float(denominator) if denominator > 0 else 0.0


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def resolve_manifest_paths(path: Path) -> list[Path]:
    data = load_json(path)
    if data.get("schema") == "nsmb_mvl_ai_recording_manifest_v1":
        return [path]
    if data.get("schema") != "nsmb_mvl_ai_recordings_index_v1":
        raise ValueError(f"{path}: unsupported schema {data.get('schema')!r}")
    result: list[Path] = []
    for item in data.get("recordings") or []:
        manifest = Path(str(item.get("manifest") or ""))
        if not manifest.is_absolute():
            manifest = path.parent / manifest
        result.append(manifest)
    return result


def parse_required_event(value: str) -> tuple[str, int]:
    if ":" not in value:
        return value, 1
    name, count = value.split(":", 1)
    return name, int(count, 0)


def path_present(value: Any, base: Path) -> bool:
    if value is None or str(value) == "":
        return False
    path = Path(str(value))
    if not path.is_absolute():
        path = base / path
    return path.exists()


def audit_manifest(
    path: Path,
    *,
    min_rows: int,
    min_gameplay_rows: int,
    min_player_found_ratio: float,
    min_label_ratio: float,
    min_nonzero_label_rows: int,
    required_stage: int | None,
    require_packet_replay: bool,
    required_events: list[tuple[str, int]],
) -> dict[str, Any]:
    data = load_json(path)
    summary = data.get("summary") or {}
    quality = data.get("quality") or {}
    replay = data.get("replay") or {}
    base = path.parent

    rows = num(summary.get("rows"))
    gameplay_rows = num(summary.get("gameplayRows"))
    player_found_rows = num(summary.get("playerFoundRows"))
    label_rows = num(summary.get("labelRows"))
    nonzero_label_rows = num(summary.get("nonzeroLabelRows"))
    event_counts = summary.get("eventCounts") or {}

    errors: list[str] = []
    warnings: list[str] = []

    if rows < min_rows:
        errors.append(f"rows {rows} < {min_rows}")
    if gameplay_rows < min_gameplay_rows:
        errors.append(f"gameplayRows {gameplay_rows} < {min_gameplay_rows}")
    if ratio(player_found_rows, rows) < min_player_found_ratio:
        errors.append(
            f"playerFoundRatio {ratio(player_found_rows, rows):.3f} < {min_player_found_ratio:.3f}"
        )
    if ratio(label_rows, rows) < min_label_ratio:
        errors.append(f"labelRatio {ratio(label_rows, rows):.3f} < {min_label_ratio:.3f}")
    if nonzero_label_rows < min_nonzero_label_rows:
        errors.append(f"nonzeroLabelRows {nonzero_label_rows} < {min_nonzero_label_rows}")
    if required_stage is not None and num(data.get("stageScope"), -1) != required_stage:
        errors.append(f"stageScope {data.get('stageScope')} != {required_stage}")
    if require_packet_replay:
        has_common = path_present(data.get("packetReplayFile") or replay.get("packetReplayFile"), base)
        has_pair = path_present(data.get("hostPacketReplayFile") or replay.get("hostPacketReplayFile"), base) and path_present(
            data.get("clientPacketReplayFile") or replay.get("clientPacketReplayFile"),
            base,
        )
        if not has_common and not has_pair:
            errors.append("packet replay file is required but missing")
    for event_name, min_count in required_events:
        count = num(event_counts.get(event_name))
        if count < min_count:
            errors.append(f"event {event_name} count {count} < {min_count}")

    quality_status = str(quality.get("status") or "unreviewed")
    if quality_status != "accepted":
        warnings.append(f"quality status is {quality_status}")
    if rows > 0 and player_found_rows < rows:
        warnings.append(f"player missing rows: {rows - player_found_rows}")

    return {
        "manifest": str(path),
        "status": "fail" if errors else "pass",
        "kind": data.get("kind"),
        "player": data.get("player"),
        "labelSource": data.get("labelSource"),
        "stageScope": data.get("stageScope"),
        "quality": quality_status,
        "metrics": {
            "rows": rows,
            "gameplayRows": gameplay_rows,
            "playerFoundRows": player_found_rows,
            "playerFoundRatio": ratio(player_found_rows, rows),
            "labelRows": label_rows,
            "labelRatio": ratio(label_rows, rows),
            "nonzeroLabelRows": nonzero_label_rows,
            "eventCounts": event_counts,
            "specialObjectFrames": summary.get("specialObjectFrames") or {},
        },
        "errors": errors,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="recording manifest JSON or recordings-index JSON")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--min-rows", type=int, default=1)
    parser.add_argument("--min-gameplay-rows", type=int, default=1)
    parser.add_argument("--min-player-found-ratio", type=float, default=0.0)
    parser.add_argument("--min-label-ratio", type=float, default=0.0)
    parser.add_argument("--min-nonzero-label-rows", type=int, default=0)
    parser.add_argument("--stage", type=int)
    parser.add_argument("--require-packet-replay", action="store_true")
    parser.add_argument("--require-event", action="append", default=[])
    args = parser.parse_args()

    required_events = [parse_required_event(value) for value in args.require_event]
    manifests = resolve_manifest_paths(args.input)
    results = [
        audit_manifest(
            manifest,
            min_rows=args.min_rows,
            min_gameplay_rows=args.min_gameplay_rows,
            min_player_found_ratio=args.min_player_found_ratio,
            min_label_ratio=args.min_label_ratio,
            min_nonzero_label_rows=args.min_nonzero_label_rows,
            required_stage=args.stage,
            require_packet_replay=args.require_packet_replay,
            required_events=required_events,
        )
        for manifest in manifests
    ]
    failures = sum(1 for result in results if result["errors"])
    report = {
        "schema": "nsmb_mvl_ai_recording_audit_v1",
        "input": str(args.input),
        "status": "fail" if failures else "pass",
        "recordingCount": len(results),
        "failureCount": failures,
        "results": results,
    }

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
