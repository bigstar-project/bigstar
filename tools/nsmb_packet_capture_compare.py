#!/usr/bin/env python3
"""Compare NSMB packet capture hook output with extracted LAN/LocalMP packets."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture_csv", type=Path)
    parser.add_argument("packets_csv", type=Path)
    parser.add_argument("--event", required=True, help="Packet event to compare, e.g. send.")
    parser.add_argument("--type", required=True, dest="localmp_type", help="localmp_type to compare, e.g. 1 or 65538.")
    parser.add_argument("--slot", required=True, help="packet_slot to compare.")
    parser.add_argument("--action", default="0x03")
    parser.add_argument("--max-errors", type=int, default=20)
    args = parser.parse_args()

    captured = {
        row["tick"]: row["packet_hex"]
        for row in csv.DictReader(args.capture_csv.open(newline="", encoding="utf-8"))
    }

    checked = 0
    errors: list[str] = []
    with args.packets_csv.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["event"] != args.event:
                continue
            if row["localmp_type"] != args.localmp_type:
                continue
            if row["packet_slot"] != args.slot:
                continue
            if row["action"] != args.action:
                continue

            got = captured.get(row["tick"])
            if got is None:
                continue
            checked += 1
            if got != row["packet_hex"]:
                errors.append(f"{row['tick']}: capture != extracted packet")

    print(f"checked_ticks={checked} action={args.action} errors={len(errors)}")
    for error in errors[: args.max_errors]:
        print(error)
    return 1 if errors or checked == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
