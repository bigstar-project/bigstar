#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

from ndspy.rom import NintendoDSRom

from nsmb_us_rom_tool import load_symbols


def encode_mov_imm(rd: int, imm: int) -> int:
    imm &= 0xFFFFFFFF
    for rot in range(16):
        # ARM immediate encoding represents imm8 ROR (rot * 2).
        left = ((imm << (rot * 2)) | (imm >> (32 - rot * 2))) & 0xFFFFFFFF if rot else imm
        if left <= 0xFF:
            return 0xE3A00000 | (rd << 12) | (rot << 8) | left
    raise ValueError(f"immediate 0x{imm:X} is not encodable as ARM mov immediate")


def find_section(main_code, addr: int):
    for section in main_code.sections:
        start = section.ramAddress
        end = start + len(section.data)
        if start <= addr < end:
            return section, addr - start
    raise ValueError(f"address 0x{addr:08X} is not in an ARM9 data section")


def patch_arm9_words(main_code, addr: int, words: list[int]) -> bytes:
    section, off = find_section(main_code, addr)
    old = bytes(section.data[off:off + len(words) * 4])
    for i, word in enumerate(words):
        struct.pack_into("<I", section.data, off + i * 4, word)
    return old


def patch_rng_constant(rom: NintendoDSRom, symbols: dict[str, int], value: int) -> list[str]:
    arm9 = rom.loadArm9()
    words = [
        encode_mov_imm(0, value),
        0xE12FFF1E,  # bx lr
    ]
    patched: list[str] = []
    for symbol in ("_ZN3Net9getRandomEv", "_ZN4Game9getRandomEv"):
        addr = symbols[symbol]
        old = patch_arm9_words(arm9, addr, words)
        patched.append(f"{symbol} @ 0x{addr:08X}: {old.hex()} -> {''.join(struct.pack('<I', w).hex() for w in words)}")
    rom.arm9 = arm9.save(compress=True)
    return patched


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="roms/nsmb-us.nds")
    ap.add_argument("--symbols", default="external/NSMB-Code-Reference/symbols9.x")
    ap.add_argument("--out", required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_rng = sub.add_parser("rng-constant")
    p_rng.add_argument("--value", type=lambda x: int(x, 0), default=0x100)
    args = ap.parse_args()

    symbols = load_symbols(Path(args.symbols))
    rom = NintendoDSRom.fromFile(args.rom)

    if args.cmd == "rng-constant":
        changes = patch_rng_constant(rom, symbols, args.value)
    else:
        raise AssertionError(args.cmd)

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    rom.saveToFile(args.out)
    print(f"wrote {args.out}")
    for change in changes:
        print(change)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
