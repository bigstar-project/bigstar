#!/usr/bin/env python3
"""Extract NSMB Net packets embedded in melonDS LocalMP payload traces.

The LocalMP command/reply payloads contain the smaller NSMB packet that the game
code reads through `Net::getConsoleKeys()` / `Net::getPacketByte()`. This helper
cuts those packets out into a compact CSV for replay/bridge experiments.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


DEFAULT_LAYOUTS = {
    # packet start offsets, not payload offsets. The 44-byte free-byte payload
    # starts at start+8.
    (1, 302): [46, 108],
    (65538, 106): [38],
}


def u16(data: bytes, off: int) -> int:
    return data[off] | (data[off + 1] << 8)


def parse_layout(text: str) -> dict[tuple[int, int], list[int]]:
    if not text:
        return DEFAULT_LAYOUTS
    layouts: dict[tuple[int, int], list[int]] = {}
    for item in text.split(";"):
        if not item.strip():
            continue
        head, offsets = item.split(":", 1)
        packet_type, length = (int(part, 0) for part in head.split(",", 1))
        layouts[(packet_type, length)] = [int(part, 0) for part in offsets.split(",") if part.strip()]
    return layouts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--packet-size", type=int, default=52)
    parser.add_argument(
        "--layouts",
        default="",
        help="Override layouts as 'type,len:offset,offset;type,len:offset'. Offsets are full packet starts.",
    )
    args = parser.parse_args()

    layouts = parse_layout(args.layouts)
    output = args.out.open("w", newline="", encoding="utf-8") if args.out else sys.stdout
    close_output = args.out is not None
    try:
        writer = csv.writer(output)
        writer.writerow(
            [
                "seq",
                "event",
                "inst",
                "localmp_type",
                "localmp_len",
                "packet_slot",
                "packet_off",
                "payload_off",
                "tick",
                "keys",
                "action",
                "action_hi",
                "packet_hex",
            ]
        )
        with args.trace.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                if not row.get("dataHex"):
                    continue
                packet_type = int(row["type"])
                length = int(row["len"])
                offsets = layouts.get((packet_type, length))
                if not offsets:
                    continue
                data = bytes.fromhex(row["dataHex"])
                for slot, off in enumerate(offsets):
                    end = off + args.packet_size
                    if off < 0 or end > len(data):
                        continue
                    packet = data[off:end]
                    writer.writerow(
                        [
                            row["seq"],
                            row["event"],
                            row["inst"],
                            packet_type,
                            length,
                            slot,
                            off,
                            off + 8,
                            f"0x{u16(packet, 0):04X}",
                            f"0x{u16(packet, 2):04X}",
                            f"0x{packet[4]:02X}",
                            f"0x{packet[5]:02X}",
                            packet.hex().upper(),
                        ]
                    )
    finally:
        if close_output:
            output.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
