#!/usr/bin/env python3
"""Build a compact packet replay CSV from extracted NSMB packet rows.

Input is produced by `nsmb_localmp_packet_extract.py`.
Output columns are:

    tick,player,packet_hex

For a normal host/client command stream, `type=1` rows already contain both
players as packet slots 0 and 1, so those rows are preferred.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("packets", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--event", default="", help="Optional event filter, e.g. send or recv.")
    parser.add_argument("--action", default="", help="Optional action filter, e.g. 0x03.")
    parser.add_argument(
        "--prefer-command",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Use only type=1 command rows. They contain both player slots.",
    )
    args = parser.parse_args()

    rows: dict[tuple[int, int], str] = {}
    with args.packets.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if args.event and row["event"] != args.event:
                continue
            if args.action and row["action"] != args.action:
                continue
            if args.prefer_command and row["localmp_type"] != "1":
                continue

            tick = int(row["tick"], 0)
            player = int(row["packet_slot"], 0)
            if player < 0 or player > 1:
                continue
            rows[(tick, player)] = row["packet_hex"]

    output = args.out.open("w", newline="", encoding="utf-8") if args.out else sys.stdout
    close_output = args.out is not None
    try:
        writer = csv.writer(output)
        writer.writerow(["tick", "player", "packet_hex"])
        for (tick, player), packet_hex in sorted(rows.items()):
            writer.writerow([f"0x{tick:04X}", player, packet_hex])
    finally:
        if close_output:
            output.close()

    print(f"replay_rows={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
