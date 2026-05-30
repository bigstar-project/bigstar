#!/usr/bin/env python3
"""Report ARM literal references to Game::localPlayerID in NSMB US.

This is intentionally static and narrow: it finds PC-relative ARM LDR
instructions whose literal value is the Game::localPlayerID address, then maps
each instruction to the nearest symbol in symbols9.x. It is useful for deciding
which MvL display/UI paths can be patched without changing the global
Game::localPlayerID gameplay state.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
except Exception as exc:  # pragma: no cover - runtime dependency check
    raise SystemExit(f"capstone is required: {exc}") from exc

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from ndspy import codeCompression  # noqa: E402
from nsmb_us_rom_tool import load_symbols, parse_segments, u32  # noqa: E402


LOCAL_PLAYER_SYMBOL = "_ZN4Game13localPlayerIDE"
DEFAULT_LOCAL_PLAYER_ADDR = 0x02085A7C


@dataclass(frozen=True)
class SymbolRange:
    name: str
    addr: int
    end: int


def build_symbol_ranges(symbols: dict[str, int]) -> list[SymbolRange]:
    ordered = sorted((addr, name) for name, addr in symbols.items())
    ranges: list[SymbolRange] = []
    for i, (addr, name) in enumerate(ordered):
        end = ordered[i + 1][0] if i + 1 < len(ordered) else addr + 4
        if end <= addr:
            end = addr + 4
        ranges.append(SymbolRange(name=name, addr=addr, end=end))
    return ranges


def nearest_symbol(ranges: list[SymbolRange], addr: int) -> SymbolRange | None:
    lo = 0
    hi = len(ranges)
    while lo < hi:
        mid = (lo + hi) // 2
        if ranges[mid].addr <= addr:
            lo = mid + 1
        else:
            hi = mid
    idx = lo - 1
    if idx < 0:
        return None
    return ranges[idx]


def demangle_hint(name: str) -> str:
    # Keep this dependency-free. The full mangled name remains in the output.
    replacements = {
        "_ZN11StageCamera": "StageCamera::",
        "_ZN7StageFX": "StageFX::",
        "_ZN4Game": "Game::",
        "_ZN6Player": "Player::",
        "_ZN10PlayerBase": "PlayerBase::",
        "_ZN5Stage": "Stage::",
        "_ZN10StageScene": "StageScene::",
    }
    for prefix, label in replacements.items():
        if name.startswith(prefix):
            return label + name[len(prefix):]
    return name


def literal_target(insn_addr: int, op_str: str) -> int | None:
    # Examples: "r0, [pc, #0x28]" or "r1, [pc, #-0x10]".
    m = re.search(r"\[pc(?:,\s*#(?P<sign>-?)(?P<imm>0x[0-9a-fA-F]+|\d+))?\]", op_str)
    if not m:
        return None
    sign = m.group("sign") or ""
    imm_s = m.group("imm")
    imm = 0 if imm_s is None else int(imm_s, 0)
    if sign == "-":
        imm = -imm
    return (insn_addr + 8 + imm) & 0xFFFFFFFF


def scan_segment(
    rom: bytes,
    seg,
    symbol_ranges: list[SymbolRange],
    local_player_addr: int,
) -> list[dict[str, object]]:
    raw_end = seg.rom + (seg.raw_size or seg.size)
    data = codeCompression.decompress(rom[seg.rom:raw_end])
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    results: list[dict[str, object]] = []
    # Overlays contain literal pools and data between functions. A normal
    # linear disassembly can stop at the first invalid word, so decode one word
    # at a time and accept that data words may decode as harmless false code.
    for off in range(0, len(data) - 3, 4):
        decoded = list(md.disasm(data[off:off + 4], seg.ram + off))
        if not decoded:
            continue
        insn = decoded[0]
        if insn.mnemonic != "ldr" or "pc" not in insn.op_str:
            continue
        target = literal_target(insn.address, insn.op_str)
        if target is None or not seg.contains(target):
            continue
        off = target - seg.ram
        if off < 0 or off + 4 > len(data):
            continue
        if u32(data, off) != local_player_addr:
            continue
        sym = nearest_symbol(symbol_ranges, insn.address)
        results.append({
            "segment": seg.name,
            "overlay": seg.overlay_id,
            "insn": insn.address,
            "literal": target,
            "asm": f"{insn.mnemonic} {insn.op_str}",
            "symbol": sym.name if sym else "<none>",
            "symbol_addr": sym.addr if sym else 0,
            "symbol_delta": insn.address - sym.addr if sym else 0,
            "symbol_hint": demangle_hint(sym.name) if sym else "<none>",
        })
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="roms/nsmb-us.nds")
    ap.add_argument("--symbols", default="external/NSMB-Code-Reference/symbols9.x")
    ap.add_argument("--overlay-id", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--all-segments", action="store_true")
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=None)
    args = ap.parse_args()

    rom = Path(args.rom).read_bytes()
    symbols = load_symbols(Path(args.symbols))
    local_player_addr = args.addr or symbols.get(LOCAL_PLAYER_SYMBOL, DEFAULT_LOCAL_PLAYER_ADDR)
    symbol_ranges = build_symbol_ranges(symbols)
    segments = parse_segments(rom)

    if args.all_segments:
        targets = [seg for seg in segments if seg.name == "arm9" or seg.overlay_id is not None]
    elif args.overlay_id is not None:
        targets = [seg for seg in segments if seg.overlay_id == args.overlay_id]
    else:
        targets = [seg for seg in segments if seg.overlay_id == 10]

    all_results: list[dict[str, object]] = []
    for seg in targets:
        all_results.extend(scan_segment(rom, seg, symbol_ranges, local_player_addr))

    print(f"localPlayerID=0x{local_player_addr:08X}")
    print("segment,overlay,insn,literal,symbol_addr,symbol_delta,symbol_hint,symbol,asm")
    for row in sorted(all_results, key=lambda r: (str(r["segment"]), int(r["insn"]))):
        overlay = "" if row["overlay"] is None else str(row["overlay"])
        print(
            f"{row['segment']},{overlay},0x{int(row['insn']):08X},0x{int(row['literal']):08X},"
            f"0x{int(row['symbol_addr']):08X},0x{int(row['symbol_delta']):X},"
            f"{row['symbol_hint']},{row['symbol']},{row['asm']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
