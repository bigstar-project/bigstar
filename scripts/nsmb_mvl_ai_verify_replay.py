#!/usr/bin/env python3
"""Compare an expected NSMB MvL recording with a replay run play log."""

from __future__ import annotations

import argparse
import csv
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


def playlog_from_input(path: Path) -> Path:
    if path.suffix.lower() not in {".json", ".manifest"}:
        return path
    with path.open("r", encoding="utf-8") as f:
        manifest = json.load(f)
    playlog = (
        manifest.get("playLog")
        or manifest.get("playLogPath")
        or manifest.get("aiPlayLog")
        or manifest.get("aiPlayLogPath")
    )
    if not playlog:
        raise ValueError(f"{path}: manifest does not contain playLog/playLogPath")
    result = Path(str(playlog))
    if not result.is_absolute():
        result = path.parent / result
    return result


def last_record(path: Path) -> dict[str, Any]:
    last: dict[str, Any] | None = None
    for record in iter_records(path):
        last = record
    if last is None:
        raise ValueError(f"{path}: no records")
    return last


def records_by_frame(path: Path) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for record in iter_records(path):
        result[num(record.get("frame"))] = record
    if not result:
        raise ValueError(f"{path}: no records")
    return result


def player(record: dict[str, Any], index: int) -> dict[str, Any]:
    players = record.get("players") or []
    if index >= len(players):
        return {}
    return players[index]


def pos(player_record: dict[str, Any]) -> dict[str, int]:
    value = player_record.get("pos") or {}
    return {"x": num(value.get("x")), "y": num(value.get("y")), "z": num(value.get("z"))}


def fail(message: str) -> None:
    raise SystemExit(f"replay mismatch: {message}")


def format_value(value: Any) -> str:
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False, sort_keys=True)
    return str(value)


def format_mismatch(mismatch: dict[str, Any]) -> str:
    field = mismatch.get("field", "")
    expected = format_value(mismatch.get("expected"))
    actual = format_value(mismatch.get("actual"))
    if mismatch.get("kind") == "close":
        return (
            f"{field}: expected={expected} actual={actual} "
            f"diff={mismatch.get('diff')} tolerance={mismatch.get('tolerance')}"
        )
    if mismatch.get("kind") == "missing_frame":
        return f"{field}: expected={expected} actual={actual}"
    return f"{field}: expected={expected} actual={actual}"


def assert_equal(label: str, expected: Any, actual: Any) -> None:
    if expected != actual:
        fail(f"{label}: expected={expected} actual={actual}")


def assert_close(label: str, expected: int, actual: int, tolerance: int) -> None:
    diff = abs(expected - actual)
    if diff > tolerance:
        fail(f"{label}: expected={expected} actual={actual} diff={diff} tolerance={tolerance}")


def category_counts(record: dict[str, Any]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for obj in record.get("objects") or []:
        category = str(obj.get("category") or "object")
        counts[category] = counts.get(category, 0) + 1
    return counts


def add_equal_mismatch(mismatches: list[dict[str, Any]], field: str, expected: Any, actual: Any) -> None:
    if expected != actual:
        mismatches.append({"kind": "equal", "field": field, "expected": expected, "actual": actual})


def add_close_mismatch(
    mismatches: list[dict[str, Any]],
    field: str,
    expected: int,
    actual: int,
    tolerance: int,
) -> None:
    diff = abs(expected - actual)
    if diff > tolerance:
        mismatches.append(
            {
                "kind": "close",
                "field": field,
                "expected": expected,
                "actual": actual,
                "diff": diff,
                "tolerance": tolerance,
            }
        )


def compare_record_mismatches(
    expected: dict[str, Any],
    actual: dict[str, Any],
    *,
    label: str,
    position_tolerance: int,
    ignore_hash: bool,
    ignore_object_counts: bool,
    ignore_category_counts: bool,
) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    add_equal_mismatch(mismatches, f"{label}.frame", num(expected.get("frame")), num(actual.get("frame")))
    if not ignore_hash:
        add_equal_mismatch(mismatches, f"{label}.hash", expected.get("hash"), actual.get("hash"))
    for index in [0, 1]:
        expected_player = player(expected, index)
        actual_player = player(actual, index)
        add_equal_mismatch(
            mismatches,
            f"{label}.player{index}.found",
            bool(expected_player.get("found")),
            bool(actual_player.get("found")),
        )
        expected_pos = pos(expected_player)
        actual_pos = pos(actual_player)
        for axis in ["x", "y", "z"]:
            add_close_mismatch(
                mismatches,
                f"{label}.player{index}.pos.{axis}",
                expected_pos[axis],
                actual_pos[axis],
                position_tolerance,
            )
        for field in ["powerup", "dead", "battleStars", "coins"]:
            add_equal_mismatch(
                mismatches,
                f"{label}.player{index}.{field}",
                num(expected_player.get(field)),
                num(actual_player.get(field)),
            )
    if not ignore_object_counts:
        expected_objects = expected.get("objectSummary") or {}
        actual_objects = actual.get("objectSummary") or {}
        for field in ["total", "active", "dead", "notCreated", "skipUpdate", "skipRender"]:
            add_equal_mismatch(
                mismatches,
                f"{label}.objectSummary.{field}",
                num(expected_objects.get(field)),
                num(actual_objects.get(field)),
            )
    if not ignore_category_counts:
        add_equal_mismatch(mismatches, f"{label}.categoryCounts", category_counts(expected), category_counts(actual))
    return mismatches


def compare_record(
    expected: dict[str, Any],
    actual: dict[str, Any],
    *,
    label: str,
    position_tolerance: int,
    ignore_hash: bool,
    ignore_object_counts: bool,
    ignore_category_counts: bool,
) -> None:
    mismatches = compare_record_mismatches(
        expected,
        actual,
        label=label,
        position_tolerance=position_tolerance,
        ignore_hash=ignore_hash,
        ignore_object_counts=ignore_object_counts,
        ignore_category_counts=ignore_category_counts,
    )
    if mismatches:
        fail(format_mismatch(mismatches[0]))


def summarize_record(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    players = []
    for index in [0, 1]:
        player_record = player(record, index)
        players.append(
            {
                "found": bool(player_record.get("found")),
                "pos": pos(player_record),
                "powerup": num(player_record.get("powerup")),
                "dead": num(player_record.get("dead")),
                "battleStars": num(player_record.get("battleStars")),
                "coins": num(player_record.get("coins")),
            }
        )
    return {
        "frame": num(record.get("frame")),
        "hash": record.get("hash"),
        "players": players,
        "objectSummary": record.get("objectSummary") or {},
        "categoryCounts": category_counts(record),
    }


def scan_first_mismatch(
    expected_log: Path,
    actual_log: Path,
    *,
    position_tolerance: int,
    ignore_hash: bool,
    ignore_object_counts: bool,
    ignore_category_counts: bool,
    max_mismatch_frames: int,
) -> dict[str, Any]:
    expected_by_frame = records_by_frame(expected_log)
    actual_by_frame = records_by_frame(actual_log)
    mismatched_frames: list[dict[str, Any]] = []
    compared_frames = 0
    first_mismatch: dict[str, Any] | None = None
    for frame in sorted(set(expected_by_frame) | set(actual_by_frame)):
        expected = expected_by_frame.get(frame)
        actual = actual_by_frame.get(frame)
        if expected is None or actual is None:
            mismatches = [
                {
                    "kind": "missing_frame",
                    "field": f"frame[{frame}]",
                    "expected": "present" if expected is not None else "missing",
                    "actual": "present" if actual is not None else "missing",
                }
            ]
        else:
            compared_frames += 1
            mismatches = compare_record_mismatches(
                expected,
                actual,
                label=f"frame[{frame}]",
                position_tolerance=position_tolerance,
                ignore_hash=ignore_hash,
                ignore_object_counts=ignore_object_counts,
                ignore_category_counts=ignore_category_counts,
            )
        if mismatches:
            entry = {
                "frame": frame,
                "mismatches": mismatches,
                "expected": summarize_record(expected),
                "actual": summarize_record(actual),
            }
            if first_mismatch is None:
                first_mismatch = entry
            if max_mismatch_frames <= 0 or len(mismatched_frames) < max_mismatch_frames:
                mismatched_frames.append(entry)
    return {
        "schema": "nsmb_mvl_ai_replay_diff_v1",
        "expectedPlayLog": str(expected_log),
        "actualPlayLog": str(actual_log),
        "expectedFrames": len(expected_by_frame),
        "actualFrames": len(actual_by_frame),
        "comparedFrames": compared_frames,
        "firstMismatch": first_mismatch,
        "mismatchedFrames": mismatched_frames,
    }


def write_report_json(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
        f.write("\n")


def write_report_csv(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["frame", "field", "kind", "expected", "actual", "diff", "tolerance", "message"],
        )
        writer.writeheader()
        for entry in report.get("mismatchedFrames") or []:
            frame = entry.get("frame")
            for mismatch in entry.get("mismatches") or []:
                writer.writerow(
                    {
                        "frame": frame,
                        "field": mismatch.get("field", ""),
                        "kind": mismatch.get("kind", ""),
                        "expected": format_value(mismatch.get("expected")),
                        "actual": format_value(mismatch.get("actual")),
                        "diff": mismatch.get("diff", ""),
                        "tolerance": mismatch.get("tolerance", ""),
                        "message": format_mismatch(mismatch),
                    }
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected", type=Path, help="expected playlog JSONL or recording manifest JSON")
    parser.add_argument("actual", type=Path, help="replay run playlog JSONL or recording manifest JSON")
    parser.add_argument("--position-tolerance", type=int, default=0)
    parser.add_argument("--ignore-hash", action="store_true")
    parser.add_argument("--ignore-object-counts", action="store_true")
    parser.add_argument("--check-category-counts", action="store_true")
    parser.add_argument("--ignore-category-counts", action="store_true")
    parser.add_argument("--checkpoint-interval", type=int, default=0)
    parser.add_argument("--checkpoint-start-frame", type=int, default=0)
    parser.add_argument("--max-checkpoints", type=int, default=0)
    parser.add_argument("--scan-frames", action="store_true", help="compare every recorded frame and report the first mismatch")
    parser.add_argument("--mismatch-report-json", type=Path)
    parser.add_argument("--mismatch-report-csv", type=Path)
    parser.add_argument("--max-mismatch-frames", type=int, default=20)
    args = parser.parse_args()

    expected_log = playlog_from_input(args.expected)
    actual_log = playlog_from_input(args.actual)
    expected = last_record(expected_log)
    actual = last_record(actual_log)
    ignore_category_counts = not args.check_category_counts or args.ignore_category_counts
    scan_frames = args.scan_frames or args.mismatch_report_json is not None or args.mismatch_report_csv is not None

    if scan_frames:
        report = scan_first_mismatch(
            expected_log,
            actual_log,
            position_tolerance=args.position_tolerance,
            ignore_hash=args.ignore_hash,
            ignore_object_counts=args.ignore_object_counts,
            ignore_category_counts=ignore_category_counts,
            max_mismatch_frames=args.max_mismatch_frames,
        )
        if args.mismatch_report_json is not None:
            write_report_json(args.mismatch_report_json, report)
        if args.mismatch_report_csv is not None:
            write_report_csv(args.mismatch_report_csv, report)
        first_mismatch = report.get("firstMismatch")
        if first_mismatch is not None:
            mismatch = (first_mismatch.get("mismatches") or [{}])[0]
            fail(f"first mismatch frame={first_mismatch.get('frame')} {format_mismatch(mismatch)}")

    compared_checkpoints = 0
    if args.checkpoint_interval > 0:
        expected_by_frame = records_by_frame(expected_log)
        actual_by_frame = records_by_frame(actual_log)
        for frame in sorted(expected_by_frame):
            if frame < args.checkpoint_start_frame:
                continue
            if (frame - args.checkpoint_start_frame) % args.checkpoint_interval != 0:
                continue
            if frame not in actual_by_frame:
                fail(f"checkpoint frame missing from actual: frame={frame}")
            compare_record(
                expected_by_frame[frame],
                actual_by_frame[frame],
                label=f"checkpoint[{frame}]",
                position_tolerance=args.position_tolerance,
                ignore_hash=args.ignore_hash,
                ignore_object_counts=args.ignore_object_counts,
                ignore_category_counts=ignore_category_counts,
            )
            compared_checkpoints += 1
            if args.max_checkpoints > 0 and compared_checkpoints >= args.max_checkpoints:
                break

    compare_record(
        expected,
        actual,
        label="final",
        position_tolerance=args.position_tolerance,
        ignore_hash=args.ignore_hash,
        ignore_object_counts=args.ignore_object_counts,
        ignore_category_counts=ignore_category_counts,
    )

    print(
        "replay verified "
        f"expected={expected_log} actual={actual_log} frame={num(actual.get('frame'))} "
        f"checkpoints={compared_checkpoints}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
