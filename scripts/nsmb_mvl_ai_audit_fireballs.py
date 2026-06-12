#!/usr/bin/env python3
"""Audit Fireball handler state captured in NSMB MvL AI play logs."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


HORIZONTAL_WRAP_WIDTH = 0x400000


def num(value: Any, default: int = 0) -> int:
    if value is None:
        return default
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def wrapped_dx(x: int, origin: int, wrap_width: int = HORIZONTAL_WRAP_WIDTH) -> int:
    if wrap_width <= 0:
        return x - origin
    half = wrap_width // 2
    return ((x - origin + half) % wrap_width) - half


def iter_records(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def slot_pos(slot: dict[str, Any]) -> tuple[int, int, int]:
    pos = slot.get("pos") or {}
    return num(pos.get("x")), num(pos.get("y")), num(pos.get("z"))


def slot_vel(slot: dict[str, Any]) -> tuple[int, int, int]:
    vel = slot.get("vel") or {}
    return num(vel.get("x")), num(vel.get("y")), num(vel.get("z"))


def owner_info(slot: dict[str, Any]) -> tuple[int, int, int]:
    source_kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
    if source_kind in (0, 1):
        return source_kind, 100, 1
    if source_kind in (2, 3):
        return -1, 100, 1
    return (
        num(slot.get("ownerCandidate"), -1),
        num(slot.get("ownerConfidence")),
        num(slot.get("ownerVerified")),
    )


def source_kind_name(slot: dict[str, Any]) -> str:
    explicit = str(slot.get("kindName") or "")
    if explicit:
        return explicit
    source_kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
    if source_kind == 0:
        return "player0"
    if source_kind == 1:
        return "player1"
    if source_kind == 2:
        return "piranha_plant"
    if source_kind == 3:
        return "fire_bro"
    return "unknown"


class FireballEvent:
    def __init__(self, input_index: int, path: Path, slot_index: int, frame: int, slot: dict[str, Any]) -> None:
        self.input_index = input_index
        self.path = path
        self.slot_index = slot_index
        self.start_frame = frame
        self.end_frame = frame
        self.sample_count = 0
        self.owner_verified_samples = 0
        self.owner_low_confidence_samples = 0
        self.source_kinds: Counter[int] = Counter()
        self.kind_names: Counter[str] = Counter()
        self.owner_candidates: Counter[int] = Counter()
        self.states: Counter[int] = Counter()
        self.facings: Counter[int] = Counter()
        self.first_pos = slot_pos(slot)
        self.last_pos = self.first_pos
        self.first_vel = slot_vel(slot)
        self.last_vel = self.first_vel
        self.motion_samples = 0
        self.prev_frame: int | None = None
        self.prev_pos: tuple[int, int, int] | None = None
        self.add_sample(frame, slot)

    def add_sample(self, frame: int, slot: dict[str, Any]) -> None:
        pos = slot_pos(slot)
        vel = slot_vel(slot)
        if self.prev_frame is not None and self.prev_pos is not None and frame > self.prev_frame:
            dx = wrapped_dx(pos[0], self.prev_pos[0])
            dy = pos[1] - self.prev_pos[1]
            if dx != 0 or dy != 0:
                self.motion_samples += 1
        self.end_frame = frame
        self.sample_count += 1
        source_kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
        owner_candidate, owner_confidence, owner_verified = owner_info(slot)
        self.source_kinds[source_kind] += 1
        self.kind_names[source_kind_name(slot)] += 1
        self.owner_candidates[owner_candidate] += 1
        self.states[num(slot.get("state"), -1)] += 1
        self.facings[num(slot.get("facing"), -1)] += 1
        if owner_verified:
            self.owner_verified_samples += 1
        if owner_confidence < 80:
            self.owner_low_confidence_samples += 1
        self.last_pos = pos
        self.last_vel = vel
        self.prev_frame = frame
        self.prev_pos = pos

    def to_dict(self) -> dict[str, Any]:
        return {
            "inputIndex": self.input_index,
            "path": str(self.path),
            "slotIndex": self.slot_index,
            "startFrame": self.start_frame,
            "endFrame": self.end_frame,
            "durationFramesObserved": self.end_frame - self.start_frame,
            "sampleCount": self.sample_count,
            "ownerVerifiedSamples": self.owner_verified_samples,
            "ownerLowConfidenceSamples": self.owner_low_confidence_samples,
            "sourceKinds": dict(sorted(self.source_kinds.items())),
            "kindNames": dict(sorted(self.kind_names.items())),
            "ownerCandidates": dict(sorted(self.owner_candidates.items())),
            "states": dict(sorted(self.states.items())),
            "facings": dict(sorted(self.facings.items())),
            "firstPos": {"x": self.first_pos[0], "y": self.first_pos[1], "z": self.first_pos[2]},
            "lastPos": {"x": self.last_pos[0], "y": self.last_pos[1], "z": self.last_pos[2]},
            "firstVel": {"x": self.first_vel[0], "y": self.first_vel[1], "z": self.first_vel[2]},
            "lastVel": {"x": self.last_vel[0], "y": self.last_vel[1], "z": self.last_vel[2]},
            "motionSamples": self.motion_samples,
        }


def audit_paths(paths: list[Path], max_events: int) -> dict[str, Any]:
    active: dict[tuple[int, int], FireballEvent] = {}
    events: list[FireballEvent] = []
    summary = {
        "inputCount": len(paths),
        "rows": 0,
        "rowsWithFireballs": 0,
        "activeSlotSamples": 0,
        "activeSlotMaxPerRow": 0,
        "ownerVerifiedSamples": 0,
        "ownerLowConfidenceSamples": 0,
        "sourceKinds": Counter(),
        "kindNames": Counter(),
        "ownerCandidates": Counter(),
        "states": Counter(),
        "facings": Counter(),
    }

    for input_index, path in enumerate(paths):
        for record in iter_records(path):
            summary["rows"] += 1
            frame = num(record.get("frame"))
            slots = ((record.get("specialObjects") or {}).get("fireballs") or {}).get("slots") or []
            seen: set[tuple[int, int]] = set()
            if slots:
                summary["rowsWithFireballs"] += 1
            summary["activeSlotSamples"] += len(slots)
            summary["activeSlotMaxPerRow"] = max(summary["activeSlotMaxPerRow"], len(slots))
            for slot in slots:
                slot_index = num(slot.get("index"), -1)
                if slot_index < 0:
                    continue
                key = (input_index, slot_index)
                seen.add(key)
                if key not in active:
                    active[key] = FireballEvent(input_index, path, slot_index, frame, slot)
                else:
                    active[key].add_sample(frame, slot)

                source_kind = num(slot.get("sourceKind"), num(slot.get("kind"), -1))
                owner_candidate, owner_confidence, owner_verified = owner_info(slot)
                summary["sourceKinds"][source_kind] += 1
                summary["kindNames"][source_kind_name(slot)] += 1
                summary["ownerCandidates"][owner_candidate] += 1
                summary["states"][num(slot.get("state"), -1)] += 1
                summary["facings"][num(slot.get("facing"), -1)] += 1
                if owner_verified:
                    summary["ownerVerifiedSamples"] += 1
                if owner_confidence < 80:
                    summary["ownerLowConfidenceSamples"] += 1

            ended = [key for key in active if key[0] == input_index and key not in seen]
            for key in ended:
                events.append(active.pop(key))

        ended = [key for key in active if key[0] == input_index]
        for key in ended:
            events.append(active.pop(key))

    event_dicts = [event.to_dict() for event in events]
    event_dicts.sort(key=lambda item: (item["inputIndex"], item["startFrame"], item["slotIndex"]))
    return {
        "schema": "nsmb_mvl_fireball_audit_v1",
        "summary": {
            **{
                key: value
                for key, value in summary.items()
                if not isinstance(value, Counter)
            },
            "eventCount": len(events),
            "sourceKinds": dict(sorted(summary["sourceKinds"].items())),
            "kindNames": dict(sorted(summary["kindNames"].items())),
            "ownerCandidates": dict(sorted(summary["ownerCandidates"].items())),
            "states": dict(sorted(summary["states"].items())),
            "facings": dict(sorted(summary["facings"].items())),
        },
        "events": event_dicts[:max_events],
        "truncatedEvents": max(0, len(event_dicts) - max_events),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path, help="AI play log JSONL files")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-events", type=int, default=32)
    args = parser.parse_args()

    report = audit_paths(args.inputs, args.max_events)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
        f.write("\n")
    summary = report["summary"]
    print(
        "rows={rows} rowsWithFireballs={rowsWithFireballs} activeSlotSamples={activeSlotSamples} "
        "events={eventCount} ownerVerifiedSamples={ownerVerifiedSamples} "
        "ownerLowConfidenceSamples={ownerLowConfidenceSamples} output={output}".format(
            output=args.output,
            **summary,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
