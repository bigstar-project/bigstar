#!/usr/bin/env python3
"""Probe NSMB Mario vs Luigi RAM dumps for random-event candidates.

The current PoC writes raw MainRAM snapshots from melonDS. This helper keeps the
first pass deliberately heuristic: it looks for the known MvsL Big Star actor ID
(210 / 0x00d2) and for compact object-like records that differ between runs.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


MAIN_RAM_BASE = 0x02000000
BIG_STAR_ACTOR_ID = 210
US_GAMECODE = "A2DE"
A2DJ_GAMECODE = "A2DJ"

US_SYMBOLS = {
    "Game::random.value": 0x02085A70,
    "Game::randomCallCount": 0x02085A54,
    "Net::random.value": 0x02088A68,
    "Net::randomCallCount": 0x02088A48,
    "Net::randomShareStep": 0x02088A50,
    "Net::randomBranchAddress": 0x0208885C,
    "Game::stageGroup": 0x02085A18,
    "Game::stageID": 0x02085A14,
    "Game::localPlayerID": 0x02085A7C,
    "Game::vsMode": 0x02085A84,
}

A2DJ_SYMBOLS = {
    # Verified from inst0/inst1 MvsL RAM dumps. The Game/Stage globals are US
    # symbols shifted by -0x9c0 in A2DJ.
    "Game::stageID": 0x02085054,
    "Game::stageGroup / Stage::stageGroup": 0x02085058,
    "Game::randomCallCount": 0x02085094,
    "Game::random.value": 0x020850B0,
    "Game::localPlayerID": 0x020850BC,
    "Game::vsMode": 0x020850C4,
    # Verified from the Net global block layout and JP game group id 0x42. The
    # Net globals are US symbols shifted by -0x9e0 in A2DJ.
    "Net::ggid": 0x02087E78,
    "Net::randomBranchAddress": 0x02087E7C,
    "Net::sendPacket": 0x02087F00,
    "Net::randomCallCount": 0x02088068,
    "Net::marker": 0x0208806C,
    "Net::randomShareStep": 0x02088070,
    "Net::random.value": 0x02088088,
}


def u16(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 2], "little")


def u32(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 4], "little")


def hexdump16(data: bytes, off: int, size: int = 0x20) -> str:
    words = [f"{u16(data, off + i):04x}" for i in range(0, size, 2)]
    return " ".join(words)


@dataclass(frozen=True)
class Record:
    offset: int
    w0: int
    w1: int
    marker: int
    state: int
    tail: int
    mirror_marker: int

    @property
    def addr(self) -> int:
        return MAIN_RAM_BASE + self.offset


def find_big_star_actor_id(data: bytes) -> list[int]:
    hits: list[int] = []
    for off in range(0, len(data) - 2, 2):
        if u16(data, off) == BIG_STAR_ACTOR_ID:
            hits.append(off)
    return hits


def find_object_like_records(data: bytes, start: int, end: int) -> list[Record]:
    records: list[Record] = []
    end = min(end, len(data) - 0x42)
    for off in range(start, end, 2):
        marker = u16(data, off + 4)
        state = u16(data, off + 8)
        tail = u16(data, off + 10)
        mirror_marker = u16(data, off + 0x42)
        if 0x0E00 <= marker <= 0x0EFF and state == 3 and tail == 0xFFFF:
            records.append(
                Record(
                    offset=off,
                    w0=u16(data, off),
                    w1=u16(data, off + 2),
                    marker=marker,
                    state=state,
                    tail=tail,
                    mirror_marker=mirror_marker,
                )
            )
    return records


def print_dump_summary(label: str, data: bytes, scan_start: int, scan_end: int) -> None:
    print(f"== {label} ==")
    hits = find_big_star_actor_id(data)
    print(f"big_star_actor_id_0x00d2_hits={len(hits)}")
    for off in hits[:8]:
        print(
            f"  off=0x{off:06x} addr=0x{MAIN_RAM_BASE + off:08x} "
            f"context={hexdump16(data, max(0, off - 0x10), 0x30)}"
        )
    if len(hits) > 8:
        print(f"  ... {len(hits) - 8} more")

    records = find_object_like_records(data, scan_start, scan_end)
    print(f"object_like_records={len(records)} scan=0x{scan_start:06x}-0x{scan_end:06x}")
    for rec in records:
        print(
            f"  off=0x{rec.offset:06x} addr=0x{rec.addr:08x} "
            f"w0=0x{rec.w0:04x} w1=0x{rec.w1:04x} "
            f"marker=0x{rec.marker:04x} mirror=0x{rec.mirror_marker:04x} "
            f"head={hexdump16(data, rec.offset, 0x20)}"
        )


def print_diff(label_a: str, data_a: bytes, label_b: str, data_b: bytes, scan_start: int, scan_end: int) -> None:
    print(f"== diff {label_a} vs {label_b} ==")
    recs_a = {rec.offset: rec for rec in find_object_like_records(data_a, scan_start, scan_end)}
    recs_b = {rec.offset: rec for rec in find_object_like_records(data_b, scan_start, scan_end)}
    for off in sorted(set(recs_a) | set(recs_b)):
        rec_a = recs_a.get(off)
        rec_b = recs_b.get(off)
        raw_a = data_a[off : off + 0x20]
        raw_b = data_b[off : off + 0x20]
        if rec_a == rec_b and raw_a == raw_b:
            continue
        print(f"  off=0x{off:06x} addr=0x{MAIN_RAM_BASE + off:08x}")
        if rec_a:
            print(f"    {label_a}: marker=0x{rec_a.marker:04x} w0=0x{rec_a.w0:04x} w1=0x{rec_a.w1:04x}")
        else:
            print(f"    {label_a}: <no record>")
        if rec_b:
            print(f"    {label_b}: marker=0x{rec_b.marker:04x} w0=0x{rec_b.w0:04x} w1=0x{rec_b.w1:04x}")
        else:
            print(f"    {label_b}: <no record>")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("dumps", nargs="+", type=Path)
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--us-symbols", action="store_true")
    parser.add_argument("--a2dj-symbols", action="store_true")
    parser.add_argument("--scan-start", default="0x080000")
    parser.add_argument("--scan-end", default="0x090000")
    return parser.parse_args()


def print_rom_info(rom: Path | None) -> None:
    if not rom:
        return
    data = rom.read_bytes()
    gamecode = data[12:16].decode("ascii", errors="replace")
    title = data[:12].decode("ascii", errors="replace").rstrip("\0")
    print(f"== ROM ==")
    print(f"title={title} gamecode={gamecode}")
    if gamecode != US_GAMECODE:
        print(
            f"warning=NSMB-Code-Reference symbols are for US {US_GAMECODE}; "
            f"this ROM is {gamecode}, so fixed symbol addresses need porting."
        )


def print_symbols(label: str, data: bytes, symbols: dict[str, int], symbol_label: str) -> None:
    print(f"== {symbol_label} symbol probe {label} ==")
    for name, addr in symbols.items():
        off = addr - MAIN_RAM_BASE
        if off < 0 or off + 4 > len(data):
            print(f"  {name} addr=0x{addr:08x} out_of_dump")
            continue
        print(
            f"  {name} addr=0x{addr:08x} "
            f"u32=0x{u32(data, off):08x} u8=0x{data[off]:02x}"
        )


def main() -> int:
    args = parse_args()
    scan_start = int(args.scan_start, 0)
    scan_end = int(args.scan_end, 0)
    print_rom_info(args.rom)

    loaded: list[tuple[str, bytes]] = []
    for dump in args.dumps:
        data = dump.read_bytes()
        loaded.append((dump.as_posix(), data))
        print_dump_summary(dump.as_posix(), data, scan_start, scan_end)
        if args.us_symbols:
            print_symbols(dump.as_posix(), data, US_SYMBOLS, "US")
        if args.a2dj_symbols:
            print_symbols(dump.as_posix(), data, A2DJ_SYMBOLS, "A2DJ")

    if len(loaded) == 2:
        print_diff(loaded[0][0], loaded[0][1], loaded[1][0], loaded[1][1], scan_start, scan_end)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
