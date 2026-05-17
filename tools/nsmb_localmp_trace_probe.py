#!/usr/bin/env python3
"""Inspect melonDS Local MP payload traces for NSMB MvL packet candidates."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Row:
    seq: int
    event: str
    inst: int
    packet_type: int
    length: int
    timestamp: int
    data_hash: str
    data: bytes


def parse_rows(path: Path) -> list[Row]:
    rows: list[Row] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for raw in csv.DictReader(handle):
            hex_data = raw.get("dataHex", "")
            rows.append(
                Row(
                    seq=int(raw["seq"]),
                    event=raw["event"],
                    inst=int(raw["inst"]),
                    packet_type=int(raw["type"]),
                    length=int(raw["len"]),
                    timestamp=int(raw["timestamp"]),
                    data_hash=raw["dataHash"],
                    data=bytes.fromhex(hex_data) if hex_data else b"",
                )
            )
    return rows


def le16(data: bytes, off: int) -> int | None:
    if off + 2 > len(data):
        return None
    return data[off] | (data[off + 1] << 8)


def le32(data: bytes, off: int) -> int | None:
    if off + 4 > len(data):
        return None
    return data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24)


def format_offsets(offsets: list[int], limit: int) -> str:
    if len(offsets) <= limit:
        return " ".join(f"{off:02X}" for off in offsets)
    head = " ".join(f"{off:02X}" for off in offsets[:limit])
    return f"{head} ... +{len(offsets) - limit}"


def analyze_group(rows: list[Row], offset_limit: int, sample_count: int) -> None:
    if not rows:
        return

    min_len = min(len(row.data) for row in rows)
    variable_offsets: list[int] = []
    for off in range(min_len):
        values = {row.data[off] for row in rows}
        if len(values) > 1:
            variable_offsets.append(off)

    first = rows[0].data
    print(
        f"group event={rows[0].event} inst={rows[0].inst} type={rows[0].packet_type} "
        f"len={rows[0].length} rows={len(rows)} dump_len={min_len}"
    )
    print(f"  variable_offsets={format_offsets(variable_offsets, offset_limit)}")

    # Print likely counters: offsets where little-endian 16-bit values often step by 1 or 2.
    counter_candidates: list[tuple[int, int, int]] = []
    for off in range(0, max(0, min_len - 1)):
        values = [le16(row.data, off) for row in rows[: min(len(rows), 64)]]
        if any(value is None for value in values):
            continue
        diffs = [b - a for a, b in zip(values, values[1:])]
        common = max(set(diffs), key=diffs.count) if diffs else 0
        if common in (1, 2, 4, 8, 0x10, 0x20) and diffs.count(common) >= max(3, len(diffs) // 2):
            counter_candidates.append((off, values[0] or 0, common))
    if counter_candidates:
        shown = ", ".join(
            f"off=0x{off:02X} first=0x{first_value:04X} step={step}"
            for off, first_value, step in counter_candidates[:12]
        )
        print(f"  counter_candidates={shown}")

    print("  samples:")
    for row in rows[:sample_count]:
        words = []
        for off in range(0, min(min_len, 64), 4):
            value = le32(row.data, off)
            if value is not None:
                words.append(f"{off:02X}:{value:08X}")
        print(
            f"    seq={row.seq} ts={row.timestamp} hash={row.data_hash} "
            f"bytes={row.data[:64].hex().upper()} words={' '.join(words)}"
        )
    print()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("--event", default="send")
    parser.add_argument("--type", type=int, dest="packet_type")
    parser.add_argument("--len", type=int, dest="length")
    parser.add_argument("--inst", type=int)
    parser.add_argument("--offset-limit", type=int, default=96)
    parser.add_argument("--samples", type=int, default=4)
    args = parser.parse_args()

    rows = [row for row in parse_rows(args.trace) if row.data]
    if args.event:
        rows = [row for row in rows if row.event == args.event]
    if args.packet_type is not None:
        rows = [row for row in rows if row.packet_type == args.packet_type]
    if args.length is not None:
        rows = [row for row in rows if row.length == args.length]
    if args.inst is not None:
        rows = [row for row in rows if row.inst == args.inst]

    groups: dict[tuple[str, int, int, int], list[Row]] = defaultdict(list)
    for row in rows:
        groups[(row.event, row.inst, row.packet_type, row.length)].append(row)

    for key in sorted(groups):
        analyze_group(groups[key], args.offset_limit, args.samples)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
