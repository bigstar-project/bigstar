#!/usr/bin/env python3
import argparse
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from ndspy import codeCompression

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
except Exception:
    Cs = None


@dataclass
class Segment:
    name: str
    ram: int
    size: int
    rom: int
    file_id: int | None = None
    overlay_id: int | None = None
    raw_size: int | None = None
    compressed: bool = False

    @property
    def end(self) -> int:
        return self.ram + self.size

    def contains(self, addr: int) -> bool:
        return self.ram <= addr < self.end

    def file_offset(self, addr: int) -> int:
        return self.rom + (addr - self.ram)


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def load_symbols(path: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    rx = re.compile(r"^\s*([A-Za-z0-9_.$]+)\s*=\s*(0x[0-9A-Fa-f]+|[0-9A-Fa-f]+)\s*;")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = rx.match(line)
        if not m:
            continue
        value = m.group(2)
        symbols[m.group(1)] = int(value, 16)
    return symbols


def parse_segments(rom: bytes) -> list[Segment]:
    segments: list[Segment] = []
    arm9_rom = u32(rom, 0x20)
    arm9_ram = u32(rom, 0x28)
    arm9_size = u32(rom, 0x2C)
    arm9_raw = rom[arm9_rom:arm9_rom + arm9_size]
    arm9_decompressed = codeCompression.decompress(arm9_raw)
    segments.append(Segment(
        "arm9",
        arm9_ram,
        len(arm9_decompressed),
        arm9_rom,
        raw_size=arm9_size,
        compressed=len(arm9_decompressed) != len(arm9_raw)))

    fat_off = u32(rom, 0x48)
    ovt_off = u32(rom, 0x50)
    ovt_size = u32(rom, 0x54)
    for off in range(ovt_off, ovt_off + ovt_size, 0x20):
        overlay_id = u32(rom, off + 0x00)
        ram = u32(rom, off + 0x04)
        size = u32(rom, off + 0x08)
        file_id = u32(rom, off + 0x18)
        fat_entry = fat_off + file_id * 8
        file_start = u32(rom, fat_entry)
        file_end = u32(rom, fat_entry + 4)
        file_size = file_end - file_start
        raw = rom[file_start:file_end]
        decomp = codeCompression.decompress(raw)
        mapped_size = len(decomp)
        segments.append(Segment(
            f"overlay{overlay_id}",
            ram,
            mapped_size,
            file_start,
            file_id,
            overlay_id,
            raw_size=file_size,
            compressed=len(decomp) != len(raw)))
    return segments


def find_segment(segments: list[Segment], addr: int) -> Segment | None:
    matches = [s for s in segments if s.contains(addr)]
    if not matches:
        return None
    # Overlay symbols should win over the broad ARM9 range if ranges overlap.
    matches.sort(key=lambda s: (s.overlay_id is None, s.size))
    return matches[0]


def find_overlay_segment(segments: list[Segment], overlay_id: int, addr: int) -> Segment | None:
    for seg in segments:
        if seg.overlay_id == overlay_id and seg.contains(addr):
            return seg
    return None


def print_info(rom: bytes, segments: list[Segment]) -> None:
    print(f"title={rom[0:12]!r}")
    print(f"gamecode={rom[12:16].decode('ascii', errors='replace')}")
    print(f"arm9_rom=0x{u32(rom,0x20):08X} arm9_ram=0x{u32(rom,0x28):08X} arm9_size=0x{u32(rom,0x2C):X}")
    print(f"arm9_overlay_table=0x{u32(rom,0x50):08X} size=0x{u32(rom,0x54):X}")
    print("segments:")
    for s in segments:
        extra = "" if s.overlay_id is None else f" file_id={s.file_id}"
        comp = " compressed" if s.compressed else ""
        print(f"  {s.name:10} ram=0x{s.ram:08X}-0x{s.end:08X} rom=0x{s.rom:08X} size=0x{s.size:X} raw=0x{(s.raw_size or s.size):X}{comp}{extra}")


def resolve_addr(value: str, symbols: dict[str, int]) -> tuple[str, int]:
    if value in symbols:
        return value, symbols[value]
    return value, int(value, 0)


def disasm(rom: bytes, seg: Segment, addr: int, size: int) -> None:
    raw_end = seg.rom + (seg.raw_size or seg.size)
    seg_data = codeCompression.decompress(rom[seg.rom:raw_end])
    data = seg_data[addr - seg.ram:addr - seg.ram + size]
    print(f"{seg.name}: addr=0x{addr:08X} rom=0x{seg.file_offset(addr):08X} size=0x{len(data):X} compressed={int(seg.compressed)}")
    if Cs is None:
        print(data.hex())
        return
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for insn in md.disasm(data, addr):
        print(f"0x{insn.address:08X}: {insn.mnemonic:8} {insn.op_str}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="roms/nsmb-us.nds")
    ap.add_argument("--symbols", default="tools/bigstar-rom/resources/symbols9.x")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    p_map = sub.add_parser("map")
    p_map.add_argument("addr", nargs="+")
    p_dis = sub.add_parser("disasm")
    p_dis.add_argument("addr")
    p_dis.add_argument("--size", type=lambda x: int(x, 0), default=0x80)
    p_dis.add_argument("--overlay-id", type=lambda x: int(x, 0))
    args = ap.parse_args()

    rom = Path(args.rom).read_bytes()
    symbols = load_symbols(Path(args.symbols))
    segments = parse_segments(rom)

    if args.cmd == "info":
        print_info(rom, segments)
        return 0

    if args.cmd == "map":
        for item in args.addr:
            label, addr = resolve_addr(item, symbols)
            seg = find_segment(segments, addr)
            if not seg:
                print(f"{label}: addr=0x{addr:08X} segment=<none>")
                continue
            print(f"{label}: addr=0x{addr:08X} segment={seg.name} rom=0x{seg.file_offset(addr):08X}")
        return 0

    if args.cmd == "disasm":
        label, addr = resolve_addr(args.addr, symbols)
        seg = find_overlay_segment(segments, args.overlay_id, addr) if args.overlay_id is not None else find_segment(segments, addr)
        if not seg:
            suffix = "" if args.overlay_id is None else f" in overlay{args.overlay_id}"
            raise SystemExit(f"{label}: addr=0x{addr:08X} segment not found{suffix}")
        disasm(rom, seg, addr, args.size)
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
