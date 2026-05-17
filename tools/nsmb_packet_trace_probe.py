#!/usr/bin/env python3
"""Reconstruct NSMB Net packet bytes from melonDS call traces.

The A2DJ `Net::setPacketByte(u32,u8)` hook logs one byte write at a time.
This helper groups those writes by NDS instance pointer and frame, then emits
packet-like byte arrays so they can be compared with LocalMP payload traces.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


SET_PACKET_BYTE_PC = "0200E9AC"


@dataclass
class PacketWrite:
    nds: str
    frame: int
    data: bytearray
    writes: int = 0

    def hex(self, length: int) -> str:
        return bytes(self.data[:length]).hex().upper()


def parse_call_trace(path: Path, packet_size: int) -> list[PacketWrite]:
    packets: dict[tuple[str, int], PacketWrite] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["pc"].upper() != SET_PACKET_BYTE_PC:
                continue
            frame = int(row["frame"])
            key = (row["nds"], frame)
            packet = packets.setdefault(key, PacketWrite(row["nds"], frame, bytearray(packet_size)))
            off = int(row["r0"], 16)
            val = int(row["r1"], 16) & 0xFF
            if 0 <= off < packet_size:
                packet.data[off] = val
            packet.writes += 1
    return [packets[key] for key in sorted(packets, key=lambda item: (item[1], item[0]))]


def parse_localmp_payloads(path: Path, packet_type: int, length: int) -> list[tuple[int, str, int, bytes]]:
    rows: list[tuple[int, str, int, bytes]] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if not row.get("dataHex"):
                continue
            if int(row["type"]) != packet_type or int(row["len"]) != length:
                continue
            rows.append((int(row["seq"]), row["event"], int(row["inst"]), bytes.fromhex(row["dataHex"])))
    return rows


def common_bytes(a: bytes, b: bytes) -> int:
    return sum(1 for x, y in zip(a, b) if x == y)


def find_windows(payload: bytes, data: bytes) -> list[int]:
    if not payload or len(data) < len(payload):
        return []
    return [off for off in range(0, len(data) - len(payload) + 1) if data[off : off + len(payload)] == payload]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("call_trace", type=Path)
    parser.add_argument("--packet-size", type=int, default=44)
    parser.add_argument("--limit", type=int, default=12)
    parser.add_argument("--localmp", type=Path)
    parser.add_argument("--localmp-type", type=int, default=3)
    parser.add_argument("--localmp-len", type=int, default=44)
    args = parser.parse_args()

    packets = parse_call_trace(args.call_trace, args.packet_size)
    print(f"reconstructed_packets={len(packets)} packet_size={args.packet_size}")
    for packet in packets[: args.limit]:
        print(
            f"frame={packet.frame:06d} nds={packet.nds} writes={packet.writes:03d} "
            f"bytes={packet.hex(args.packet_size)}"
        )

    if args.localmp:
        localmp = parse_localmp_payloads(args.localmp, args.localmp_type, args.localmp_len)
        print(f"localmp_candidates={len(localmp)} type={args.localmp_type} len={args.localmp_len}")
        for packet in packets[: args.limit]:
            payload = bytes(packet.data[: args.packet_size])
            exact = [row for row in localmp if row[3] == payload]
            if exact:
                shown = " ".join(f"seq={seq}:{event}:inst{inst}" for seq, event, inst, _ in exact[:4])
                print(f"exact frame={packet.frame:06d} nds={packet.nds} {shown}")
                continue
            windows: list[tuple[int, int, str, int, int]] = []
            for seq, event, inst, data in localmp:
                for off in find_windows(payload, data):
                    windows.append((seq, event, inst, off, len(data)))
            if windows:
                shown = " ".join(
                    f"seq={seq}:{event}:inst{inst}:off{off}:len{length}"
                    for seq, event, inst, off, length in windows[:8]
                )
                print(f"window frame={packet.frame:06d} nds={packet.nds} {shown}")
                continue
            best = sorted(
                (
                    (common_bytes(payload, data[off : off + len(payload)]), seq, event, inst, off, data)
                    for seq, event, inst, data in localmp
                    for off in range(0, max(1, len(data) - len(payload) + 1))
                ),
                reverse=True,
            )[:3]
            if best:
                formatted = " ".join(
                    f"{score}/{args.packet_size}@seq={seq}:{event}:inst{inst}:off{off}"
                    for score, seq, event, inst, off, _ in best
                )
                print(f"best frame={packet.frame:06d} nds={packet.nds} {formatted}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
