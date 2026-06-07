#!/usr/bin/env python3
"""Compare an expected NSMB MvL recording with a replay run play log."""

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


def assert_equal(label: str, expected: Any, actual: Any) -> None:
    if expected != actual:
        fail(f"{label}: expected={expected} actual={actual}")


def assert_close(label: str, expected: int, actual: int, tolerance: int) -> None:
    diff = abs(expected - actual)
    if diff > tolerance:
        fail(f"{label}: expected={expected} actual={actual} diff={diff} tolerance={tolerance}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected", type=Path, help="expected playlog JSONL or recording manifest JSON")
    parser.add_argument("actual", type=Path, help="replay run playlog JSONL or recording manifest JSON")
    parser.add_argument("--position-tolerance", type=int, default=0)
    parser.add_argument("--ignore-hash", action="store_true")
    parser.add_argument("--ignore-object-counts", action="store_true")
    args = parser.parse_args()

    expected_log = playlog_from_input(args.expected)
    actual_log = playlog_from_input(args.actual)
    expected = last_record(expected_log)
    actual = last_record(actual_log)

    assert_equal("final frame", num(expected.get("frame")), num(actual.get("frame")))
    if not args.ignore_hash:
        assert_equal("final hash", expected.get("hash"), actual.get("hash"))
    for index in [0, 1]:
        expected_player = player(expected, index)
        actual_player = player(actual, index)
        assert_equal(f"player{index}.found", bool(expected_player.get("found")), bool(actual_player.get("found")))
        expected_pos = pos(expected_player)
        actual_pos = pos(actual_player)
        for axis in ["x", "y", "z"]:
            assert_close(
                f"player{index}.pos.{axis}",
                expected_pos[axis],
                actual_pos[axis],
                args.position_tolerance,
            )
        for field in ["powerup", "dead", "battleStars", "coins"]:
            assert_equal(
                f"player{index}.{field}",
                num(expected_player.get(field)),
                num(actual_player.get(field)),
            )
    if not args.ignore_object_counts:
        expected_objects = expected.get("objectSummary") or {}
        actual_objects = actual.get("objectSummary") or {}
        for field in ["total", "active", "dead", "notCreated", "skipUpdate", "skipRender"]:
            assert_equal(f"objectSummary.{field}", num(expected_objects.get(field)), num(actual_objects.get(field)))

    print(
        "replay verified "
        f"expected={expected_log} actual={actual_log} frame={num(actual.get('frame'))}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
