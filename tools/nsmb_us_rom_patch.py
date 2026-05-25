#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

from ndspy.rom import NintendoDSRom

from nsmb_us_rom_tool import load_symbols


BX_LR = 0xE12FFF1E
POP_PC = 0xE8BD8000


def encode_mov_imm(rd: int, imm: int) -> int:
    imm &= 0xFFFFFFFF
    for rot in range(16):
        # ARM immediate encoding represents imm8 ROR (rot * 2).
        left = ((imm << (rot * 2)) | (imm >> (32 - rot * 2))) & 0xFFFFFFFF if rot else imm
        if left <= 0xFF:
            return 0xE3A00000 | (rd << 12) | (rot << 8) | left
    raise ValueError(f"immediate 0x{imm:X} is not encodable as ARM mov immediate")


def encode_add_sp_imm(imm: int) -> int:
    return 0xE28DD000 | encode_arm_imm12(imm)


def encode_sub_sp_imm(imm: int) -> int:
    return 0xE24DD000 | encode_arm_imm12(imm)


def encode_str_imm(rd: int, rn: int, off: int) -> int:
    if off < 0 or off > 0xFFF:
        raise ValueError(f"STR offset 0x{off:X} is out of range")
    return 0xE5800000 | (rn << 16) | (rd << 12) | off


def encode_ldr_imm(rd: int, rn: int, off: int) -> int:
    if off < 0 or off > 0xFFF:
        raise ValueError(f"LDR offset 0x{off:X} is out of range")
    return 0xE5900000 | (rn << 16) | (rd << 12) | off


def encode_cmp_imm(rn: int, imm: int) -> int:
    return 0xE3500000 | (rn << 16) | encode_arm_imm12(imm)


def encode_mov_reg(rd: int, rm: int) -> int:
    return 0xE1A00000 | (rd << 12) | rm


def with_cond(word: int, cond: int) -> int:
    return (word & 0x0FFFFFFF) | ((cond & 0xF) << 28)


def encode_push(regmask: int) -> int:
    return 0xE92D0000 | regmask


def encode_bl(src_addr: int, dst_addr: int) -> int:
    diff = dst_addr - (src_addr + 8)
    if diff % 4:
        raise ValueError(f"unaligned BL target 0x{dst_addr:08X} from 0x{src_addr:08X}")
    off = diff // 4
    if off < -(1 << 23) or off >= (1 << 23):
        raise ValueError(f"BL target 0x{dst_addr:08X} out of range from 0x{src_addr:08X}")
    return 0xEB000000 | (off & 0x00FFFFFF)


def encode_arm_imm12(imm: int) -> int:
    imm &= 0xFFFFFFFF
    for rot in range(16):
        left = ((imm << (rot * 2)) | (imm >> (32 - rot * 2))) & 0xFFFFFFFF if rot else imm
        if left <= 0xFF:
            return (rot << 8) | left
    raise ValueError(f"immediate 0x{imm:X} is not encodable as ARM immediate")


def find_section(main_code, addr: int):
    for section in main_code.sections:
        start = section.ramAddress
        end = start + len(section.data)
        if start <= addr < end:
            return section, addr - start
    raise ValueError(f"address 0x{addr:08X} is not in an ARM9 data section")


def find_overlay(overlays: dict[int, object], addr: int):
    matches = []
    for overlay_id, overlay in overlays.items():
        start = overlay.ramAddress
        end = start + len(overlay.data)
        if start <= addr < end:
            matches.append((overlay_id, overlay))
    if not matches:
        raise ValueError(f"address 0x{addr:08X} is not in an ARM9 overlay")
    matches.sort(key=lambda item: len(item[1].data))
    overlay_id, overlay = matches[0]
    return overlay_id, overlay, addr - overlay.ramAddress


def patch_arm9_words(main_code, addr: int, words: list[int]) -> bytes:
    section, off = find_section(main_code, addr)
    old = bytes(section.data[off:off + len(words) * 4])
    for i, word in enumerate(words):
        struct.pack_into("<I", section.data, off + i * 4, word)
    return old


def patch_overlay_words(overlays: dict[int, object], addr: int, words: list[int]) -> tuple[int, bytes]:
    overlay_id, overlay, off = find_overlay(overlays, addr)
    old = bytes(overlay.data[off:off + len(words) * 4])
    for i, word in enumerate(words):
        struct.pack_into("<I", overlay.data, off + i * 4, word)
    return overlay_id, old


def save_overlays(rom: NintendoDSRom, overlays: dict[int, object]) -> None:
    for overlay in overlays.values():
        rom.files[overlay.fileID] = overlay.save(compress=overlay.compressed)


def words_hex(words: list[int]) -> str:
    return "".join(struct.pack("<I", w).hex() for w in words)


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


def build_direct_loadlevel_stub(
    start_addr: int,
    load_level_addr: int,
    *,
    scene: int,
    stage: int,
    player_id: int,
    rng_seed: int,
) -> list[int]:
    stack_values = [
        0,          # act
        player_id,  # playerID
        3,          # playerMask
        0,          # character1: Mario
        1,          # character2: Luigi
        0,          # powerup
        0,          # entrance
        0,          # flag
        0,          # unused1
        0,          # controlOptions
        0,          # unused2
        0,          # challengeMode
        rng_seed,   # rngSeed
    ]

    words: list[int] = [
        encode_push((1 << 4) | (1 << 14)),  # push {r4, lr}
        encode_mov_reg(4, 0),          # keep this pointer for the one-shot guard
        encode_ldr_imm(12, 4, 0x16C),
        encode_cmp_imm(12, 0x77),
        with_cond(encode_mov_imm(0, 1), 0), # moveq r0, #1
        with_cond(POP_PC | (1 << 4), 0),    # popeq {r4, pc}
        encode_mov_imm(12, 0x77),
        encode_str_imm(12, 4, 0x16C),
        encode_sub_sp_imm(0x38),
        encode_mov_imm(0, scene),
        encode_mov_imm(1, 1),          # vs mode
        encode_mov_imm(2, 9),          # MvL stage group
        encode_mov_imm(3, stage),
    ]
    current_ip_value: int | None = None
    for i, value in enumerate(stack_values):
        if current_ip_value != value:
            words.append(encode_mov_imm(12, value))
            current_ip_value = value
        words.append(encode_str_imm(12, 13, i * 4))

    bl_addr = start_addr + len(words) * 4
    words.extend([
        encode_bl(bl_addr, load_level_addr),
        encode_add_sp_imm(0x38),
        encode_mov_imm(0, 1),
        POP_PC | (1 << 4),             # pop {r4, pc}
    ])
    return words


def patch_direct_mvl_entry(
    rom: NintendoDSRom,
    symbols: dict[str, int],
    *,
    scene: int,
    stage: int,
    player_id: int,
    rng_seed: int,
    first_scene: int,
    skip_direct_loadlevel: bool,
) -> list[str]:
    arm9 = rom.loadArm9()
    overlays = rom.loadArm9Overlays()
    changes: list[str] = []

    # Scene::prepareFirstScene normally makes the title screen (SceneID 4) the
    # first non-boot scene. For this PoC, point that path at VSConnect instead.
    first_scene_addr = 0x02013428
    first_scene_word = encode_mov_imm(12, first_scene)
    old = patch_arm9_words(arm9, first_scene_addr, [first_scene_word])
    changes.append(
        f"Scene::prepareFirstScene first scene @ 0x{first_scene_addr:08X}: "
        f"{old.hex()} -> {struct.pack('<I', first_scene_word).hex()}"
    )

    # VSConnectScene::onCreate starts in selectModeSME through a literal.
    # Redirect that literal to loadGameSME so updateLoadGameSM runs immediately.
    submenu_literal_addr = 0x02159348
    submenu_word = symbols["_ZN14VSConnectScene10loadGameSME"]
    ov_id, old = patch_overlay_words(overlays, submenu_literal_addr, [submenu_word])
    changes.append(
        f"VSConnectScene initial submenu literal overlay{ov_id} @ 0x{submenu_literal_addr:08X}: "
        f"{old.hex()} -> {struct.pack('<I', submenu_word).hex()}"
    )

    if not skip_direct_loadlevel:
        # Replace updateLoadGameSM with a compact direct Game::loadLevel call.
        update_addr = symbols["_ZN14VSConnectScene16updateLoadGameSMEv"]
        stub = build_direct_loadlevel_stub(
            update_addr,
            symbols["_ZN4Game9loadLevelEtmhhhhhhhhhhhhhhm"],
            scene=scene,
            stage=stage,
            player_id=player_id,
            rng_seed=rng_seed,
        )
        ov_id, old = patch_overlay_words(overlays, update_addr, stub)
        changes.append(
            f"VSConnectScene::updateLoadGameSM direct loadLevel stub overlay{ov_id} @ 0x{update_addr:08X}: "
            f"{old.hex()} -> {words_hex(stub)}"
        )

    rom.arm9 = arm9.save(compress=True)
    save_overlays(rom, overlays)
    return changes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="roms/nsmb-us.nds")
    ap.add_argument("--symbols", default="external/NSMB-Code-Reference/symbols9.x")
    ap.add_argument("--out", required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_rng = sub.add_parser("rng-constant")
    p_rng.add_argument("--value", type=lambda x: int(x, 0), default=0x100)
    p_direct = sub.add_parser("direct-mvl-entry")
    p_direct.add_argument("--scene", type=lambda x: int(x, 0), default=0x0F)
    p_direct.add_argument("--stage", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--player-id", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--rng-seed", type=lambda x: int(x, 0), default=0x100)
    p_direct.add_argument("--first-scene", type=lambda x: int(x, 0), default=6)
    p_direct.add_argument("--skip-direct-loadlevel", action="store_true")
    args = ap.parse_args()

    symbols = load_symbols(Path(args.symbols))
    rom = NintendoDSRom.fromFile(args.rom)

    if args.cmd == "rng-constant":
        changes = patch_rng_constant(rom, symbols, args.value)
    elif args.cmd == "direct-mvl-entry":
        changes = patch_direct_mvl_entry(
            rom,
            symbols,
            scene=args.scene,
            stage=args.stage,
            player_id=args.player_id,
            rng_seed=args.rng_seed,
            first_scene=args.first_scene,
            skip_direct_loadlevel=args.skip_direct_loadlevel,
        )
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
