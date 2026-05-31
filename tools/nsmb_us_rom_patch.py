#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

from ndspy.code import saveOverlayTable
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


def encode_load_imm(rd: int, imm: int) -> int:
    imm &= 0xFFFFFFFF
    try:
        return encode_mov_imm(rd, imm)
    except ValueError:
        inverse = (~imm) & 0xFFFFFFFF
        imm12 = encode_arm_imm12(inverse)
        return 0xE3E00000 | (rd << 12) | imm12


def encode_add_sp_imm(imm: int) -> int:
    return 0xE28DD000 | encode_arm_imm12(imm)


def encode_add_imm(rd: int, rn: int, imm: int) -> int:
    return 0xE2800000 | (rn << 16) | (rd << 12) | encode_arm_imm12(imm)


def encode_sub_imm(rd: int, rn: int, imm: int) -> int:
    return 0xE2400000 | (rn << 16) | (rd << 12) | encode_arm_imm12(imm)


def encode_sub_sp_imm(imm: int) -> int:
    return 0xE24DD000 | encode_arm_imm12(imm)


def encode_str_imm(rd: int, rn: int, off: int) -> int:
    if off < 0 or off > 0xFFF:
        raise ValueError(f"STR offset 0x{off:X} is out of range")
    return 0xE5800000 | (rn << 16) | (rd << 12) | off


def encode_strb_imm(rd: int, rn: int, off: int) -> int:
    if off < 0 or off > 0xFFF:
        raise ValueError(f"STRB offset 0x{off:X} is out of range")
    return 0xE5C00000 | (rn << 16) | (rd << 12) | off


def encode_ldr_imm(rd: int, rn: int, off: int) -> int:
    if off < 0 or off > 0xFFF:
        raise ValueError(f"LDR offset 0x{off:X} is out of range")
    return 0xE5900000 | (rn << 16) | (rd << 12) | off


def encode_ldr_pc_imm(rd: int, off: int) -> int:
    if off < 0 or off > 0xFFF:
        raise ValueError(f"LDR literal offset 0x{off:X} is out of range")
    return 0xE59F0000 | (rd << 12) | off


def encode_ldr_reg_lsl(rd: int, rn: int, rm: int, shift: int) -> int:
    if shift < 0 or shift > 31:
        raise ValueError(f"LDR register shift {shift} is out of range")
    return 0xE7900000 | (rn << 16) | (rd << 12) | (shift << 7) | rm


def encode_cmp_imm(rn: int, imm: int) -> int:
    return 0xE3500000 | (rn << 16) | encode_arm_imm12(imm)


def encode_cmn_imm(rn: int, imm: int) -> int:
    return 0xE3700000 | (rn << 16) | encode_arm_imm12(imm)


def encode_mov_reg(rd: int, rm: int) -> int:
    return 0xE1A00000 | (rd << 12) | rm


def encode_add_reg(rd: int, rn: int, rm: int) -> int:
    return 0xE0800000 | (rn << 16) | (rd << 12) | rm


def encode_sub_reg(rd: int, rn: int, rm: int) -> int:
    return 0xE0400000 | (rn << 16) | (rd << 12) | rm


def encode_rsb_imm(rd: int, rn: int, imm: int) -> int:
    return 0xE2600000 | (rn << 16) | (rd << 12) | encode_arm_imm12(imm)


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


def encode_b(src_addr: int, dst_addr: int) -> int:
    diff = dst_addr - (src_addr + 8)
    if diff % 4:
        raise ValueError(f"unaligned B target 0x{dst_addr:08X} from 0x{src_addr:08X}")
    off = diff // 4
    if off < -(1 << 23) or off >= (1 << 23):
        raise ValueError(f"B target 0x{dst_addr:08X} out of range from 0x{src_addr:08X}")
    return 0xEA000000 | (off & 0x00FFFFFF)


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


def patch_overlay_words_by_id(overlays: dict[int, object], overlay_id: int, addr: int, words: list[int]) -> bytes:
    overlay = overlays[overlay_id]
    start = overlay.ramAddress
    end = start + len(overlay.data)
    if not (start <= addr < end):
        raise ValueError(f"address 0x{addr:08X} is not in overlay{overlay_id}")
    off = addr - start
    old = bytes(overlay.data[off:off + len(words) * 4])
    for i, word in enumerate(words):
        struct.pack_into("<I", overlay.data, off + i * 4, word)
    return old


def save_overlays(rom: NintendoDSRom, overlays: dict[int, object]) -> None:
    for overlay in overlays.values():
        rom.files[overlay.fileID] = overlay.save(compress=overlay.compressed)
    rom.arm9OverlayTable = saveOverlayTable(overlays)


def words_hex(words: list[int]) -> str:
    return "".join(struct.pack("<I", w).hex() for w in words)


def encode_ldr_pc_literal(rd: int, instruction_addr: int, literal_addr: int, *, cond: int = 0xE) -> int:
    pc = instruction_addr + 8
    off = literal_addr - pc
    if off < 0 or off > 0xFFF or off % 4:
        raise ValueError(
            f"LDR literal target 0x{literal_addr:08X} out of range from 0x{instruction_addr:08X}"
        )
    return (cond << 28) | 0x059F0000 | (rd << 12) | off


def build_getpacket_mirror_stub(
    start_addr: int,
    *,
    send_packet_addr: int,
    fake_state: bool,
) -> list[int]:
    words: list[int] = []
    literals: list[int] = []
    literal_refs: list[tuple[int, int, int, int]] = []

    def emit_ldr_literal(rd: int, value: int, *, cond: int = 0xE) -> None:
        literal_index = len(literals)
        literals.append(value)
        word_index = len(words)
        words.append(0)
        literal_refs.append((word_index, rd, literal_index, cond))

    if fake_state:
        words.append(encode_mov_imm(2, 2))
        for addr in (
            0x020887FC,  # Net::connectionState
            0x02088800,  # Net::packetTransIntegrity / loadGameSM state1 gate
            0x02088804,  # Net::connectedConsoleCount
            0x0208880C,  # Net::expectedConsoleCount
            0x02088814,  # Net::sessionState
            0x0208881C,  # Net::maxSessionChildren
            0x0208882C,  # Net::maxConsoleCount
        ):
            emit_ldr_literal(1, addr)
            words.append(encode_str_imm(2, 1, 0))

    words.append(encode_cmp_imm(0, 2))
    emit_ldr_literal(0, send_packet_addr, cond=3) # ldrlo
    words.append(with_cond(encode_mov_imm(0, 0), 2)) # movhs r0, #0
    words.append(BX_LR)

    literal_start_addr = start_addr + (len(words) * 4)
    for word_index, rd, literal_index, cond in literal_refs:
        instruction_addr = start_addr + (word_index * 4)
        literal_addr = literal_start_addr + (literal_index * 4)
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond=cond)

    words.extend(literals)
    return words


def build_fake_nickname_stub(start_addr: int, *, fake_net_state: bool) -> list[int]:
    words: list[int] = []
    literals: list[int] = []
    literal_refs: list[tuple[int, int, int, int]] = []

    def emit_ldr_literal(rd: int, value: int, *, cond: int = 0xE) -> int:
        literal_index = len(literals)
        literals.append(value)
        word_index = len(words)
        words.append(0)
        literal_refs.append((word_index, rd, literal_index, cond))
        return literal_index

    if fake_net_state:
        words.append(encode_mov_imm(2, 2))
        for addr in (
            0x020887FC,  # Net::connectionState
            0x02088800,  # Net::packetTransIntegrity / loadGameSM state1 gate
            0x02088804,  # Net::connectedConsoleCount
            0x0208880C,  # Net::expectedConsoleCount
            0x02088814,  # Net::sessionState
            0x0208881C,  # Net::maxSessionChildren
            0x0208882C,  # Net::maxConsoleCount
        ):
            emit_ldr_literal(1, addr)
            words.append(encode_str_imm(2, 1, 0))

        # Net::update() treats missing bit0 in the per-console session flags as
        # a disconnect condition. The real lower MP path sets these flags while
        # pairing consoles; the fake peer route must provide the same minimum
        # session liveness signal or VSConnect immediately enters
        # connectionInterruptedSM.
        emit_ldr_literal(1, 0x02088854) # pointer to per-console session flags
        words.append(encode_ldr_imm(1, 1, 0))
        words.append(encode_mov_imm(2, 3)) # active | paired
        words.append(encode_cmp_imm(1, 0))
        words.append(with_cond(encode_strb_imm(2, 1, 0), 1)) # strbne
        words.append(with_cond(encode_strb_imm(2, 1, 1), 1)) # strbne

    fake_data_literal_index = emit_ldr_literal(0, 0)
    words.append(BX_LR)

    literal_start_addr = start_addr + (len(words) * 4)
    fake_data_addr = literal_start_addr + ((len(literals) + 0) * 4)
    literals[fake_data_literal_index] = fake_data_addr

    for word_index, rd, literal_index, cond in literal_refs:
        instruction_addr = start_addr + (word_index * 4)
        literal_addr = literal_start_addr + (literal_index * 4)
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond=cond)

    words.extend(literals)
    words.extend([0, 0, 0, 0])
    return words


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


def patch_stage_camera_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # StageCamera::onUpdate normally reads Game::localPlayerID and selects
    # Stage::cameraX[localPlayerID]. For client-display experiments, force only
    # this display-side choice without changing the gameplay localPlayerID.
    player_id &= 1
    changes: list[str] = []
    mov_addr = 0x020CE438
    load_addr = 0x020CE440
    mov_word = encode_mov_imm(0, player_id)

    ov_id = 10
    old = patch_overlay_words_by_id(overlays, ov_id, mov_addr, [mov_word])
    changes.append(
        f"StageCamera::onUpdate display player mov overlay{ov_id} @ 0x{mov_addr:08X}: "
        f"{old.hex()} -> {struct.pack('<I', mov_word).hex()}"
    )

    old = patch_overlay_words_by_id(overlays, ov_id, load_addr, [NOP])
    changes.append(
        f"StageCamera::onUpdate display player load NOP overlay{ov_id} @ 0x{load_addr:08X}: "
        f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
    )
    return changes


def patch_stage_camera_state_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # The camera state functions build the OrthoView fields from
    # Stage::cameraX/Y/width/height[Game::localPlayerID]. For the client display,
    # force only these state-function array indexes without changing gameplay state.
    player_id &= 1
    changes: list[str] = []
    ov_id = 10
    mov_ip = encode_mov_imm(12, player_id)
    mov_r2 = encode_mov_imm(2, player_id)
    mov_r4 = encode_mov_imm(4, player_id)
    patches = [
        (0x020CE25C, mov_ip, "StageCamera state0 localPlayerID load #1"),
        (0x020CE284, mov_ip, "StageCamera state0 localPlayerID load #2"),
        (0x020CE298, mov_r2, "StageCamera state0 localPlayerID load #3"),
        (0x020CE314, mov_r4, "StageCamera state1 localPlayerID load"),
    ]
    for addr, word, label in patches:
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [word])
        changes.append(
            f"{label} overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()}"
        )
    return changes


def patch_stage_camera_state_vertical_slot_zero(overlays: dict[int, object]) -> list[str]:
    # Diagnostic only. For the direct localPlayerID=1 client route, Stage::cameraY[1]
    # can be rewritten before StageCamera::state1 builds the view, leaving the
    # render target/position Z at 0. Keep the horizontal slot selection natural,
    # but force state1's vertical camera-height/Y reads to slot0.
    patches = [
        (0x020CE328, encode_ldr_imm(5, 2, 0), "StageCamera state1 cameraHeight slot0"),
        (0x020CE32C, encode_ldr_imm(12, 0, 0), "StageCamera state1 cameraY slot0"),
    ]
    expected = {
        0x020CE328: 0xE7925104,  # ldr r5, [r2, r4, lsl #2]
        0x020CE32C: 0xE790C104,  # ldr ip, [r0, r4, lsl #2]
    }
    changes: list[str] = []
    for addr, word, label in patches:
        old = patch_overlay_words_by_id(overlays, 10, addr, [word])
        old_word = struct.unpack("<I", old)[0]
        if old_word != expected[addr]:
            raise ValueError(f"{label} @ 0x{addr:08X} expected 0x{expected[addr]:08X}, got 0x{old_word:08X}")
        changes.append(
            f"{label} overlay10 @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()}"
        )
    return changes


def build_is_out_of_view_vertical_camera_fallback_stub(start_addr: int, *, player1_slot0: bool = False) -> list[int]:
    words: list[int] = []
    literals: list[int] = []
    literal_refs: list[tuple[int, int, int, int]] = []

    def emit_ldr_literal(rd: int, value: int, *, cond: int = 0xE) -> None:
        literal_index = len(literals)
        literals.append(value)
        word_index = len(words)
        words.append(0)
        literal_refs.append((word_index, rd, literal_index, cond))

    words.append(encode_push((1 << 4) | (1 << 14)))  # push {r4, lr}
    if player1_slot0:
        words.append(encode_cmp_imm(2, 1))
        words.append(with_cond(encode_mov_imm(2, 0), 0x0))  # moveq r2, #0
    emit_ldr_literal(3, 0x020CAD8C)  # Stage::cameraHeight
    words.append(encode_ldr_reg_lsl(12, 3, 2, 2))  # ldr ip, [r3, r2, lsl #2]
    words.append(encode_cmp_imm(12, 0))
    words.append(with_cond(encode_mov_imm(2, 0), 0x0))  # moveq r2, #0
    emit_ldr_literal(12, 0x020CAD94)  # Stage::cameraY
    words.append(encode_ldr_imm(14, 1, 4))  # ldr lr, [r1, #4]
    emit_ldr_literal(3, 0x020CAD8C)  # Stage::cameraHeight
    words.append(encode_ldr_imm(4, 0, 0x64))  # ldr r4, [r0, #0x64]
    words.append(encode_ldr_reg_lsl(12, 12, 2, 2))  # ldr ip, [ip, r2, lsl #2]
    words.append(encode_ldr_reg_lsl(0, 3, 2, 2))  # ldr r0, [r3, r2, lsl #2]
    words.append(encode_add_reg(2, 4, 14))  # add r2, r4, lr
    words.append(encode_add_reg(0, 12, 0))  # add r0, ip, r0
    words.append(encode_ldr_imm(1, 1, 0x0C))  # ldr r1, [r1, #0xC]
    words.append(encode_add_imm(2, 2, 0x18000))
    words.append(encode_add_reg(1, 2, 1))  # add r1, r2, r1
    words.append(encode_rsb_imm(0, 0, 0))
    words.append(0xE1510000)  # cmp r1, r0
    words.append(with_cond(encode_mov_imm(0, 1), 0xB))  # movlt r0, #1
    words.append(with_cond(encode_mov_imm(0, 0), 0xA))  # movge r0, #0
    words.append(POP_PC | (1 << 4))  # pop {r4, pc}

    literal_start_addr = start_addr + (len(words) * 4)
    for word_index, rd, literal_index, cond in literal_refs:
        instruction_addr = start_addr + (word_index * 4)
        literal_addr = literal_start_addr + (literal_index * 4)
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond=cond)
    words.extend(literals)
    return words


def patch_is_out_of_view_vertical_camera_fallback(
    overlays: dict[int, object],
    *,
    player1_slot0: bool = False,
) -> list[str]:
    ov_id = 0
    func_addr = 0x020A06DC
    # overlay0 ends at 0x020CA280, followed by overlay BSS/globals used during
    # stage startup. Extending the overlay collides with those globals, so keep
    # this patch in-place in a zero-filled padding cave inside overlay0.
    stub_addr = 0x020C5298
    stub = build_is_out_of_view_vertical_camera_fallback_stub(stub_addr, player1_slot0=player1_slot0)
    cave_old = patch_overlay_words_by_id(overlays, ov_id, stub_addr, stub)
    if any(cave_old):
        raise ValueError(
            f"StageActor::isOutOfViewVertical stub cave 0x{stub_addr:08X} is not empty"
        )
    branch_word = encode_b(func_addr, stub_addr)
    old = patch_overlay_words_by_id(overlays, ov_id, func_addr, [branch_word])
    return [
        f"StageActor::isOutOfViewVertical camera fallback stub overlay{ov_id} @ 0x{stub_addr:08X}"
        f" player1Slot0={int(player1_slot0)}",
        f"StageActor::isOutOfViewVertical branch overlay{ov_id} @ 0x{func_addr:08X}: "
        f"{old.hex()} -> {struct.pack('<I', branch_word).hex()}",
    ]


def patch_camera_focus_loop_count(overlays: dict[int, object], count: int) -> list[str]:
    ov_id = 0
    count &= 0xFF
    word = encode_mov_imm(0, count)
    changes: list[str] = []
    for addr in (0x020BAAE4, 0x020BAC18):
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [word])
        changes.append(
            f"camera focus loop count overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()} count={count}"
        )
    return changes


def build_mvl_camera_lead_stub(
    start_addr: int,
    *,
    return_addr: int,
    get_player_addr: int,
    right_lead: int,
    left_lead: int,
    neutral_lead: int,
    velocity_threshold: int,
    camera_step: int,
) -> list[int]:
    words: list[int] = []
    literals: list[int] = []
    literal_refs: list[tuple[int, int, int, int]] = []

    def emit_ldr_literal(rd: int, value: int, *, cond: int = 0xE) -> None:
        literal_index = len(literals)
        literals.append(value)
        word_index = len(words)
        words.append(0)
        literal_refs.append((word_index, rd, literal_index, cond))

    right_offset = max(0, neutral_lead - right_lead)
    left_offset = max(0, left_lead - neutral_lead)

    # The overwritten site is the Stage::cameraX store in the camera update.
    # Keep the caller's live registers, use the previous Stage::cameraX value
    # as the smooth current position, and bias NSMB's natural camera target by
    # the real player actor velocity. The MvL stage wraps horizontally, so the
    # final approach must use the shortest delta on the 0x400000 camera ring.
    words.append(encode_push((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 14)))  # push {r0-r5,lr}
    words.append(encode_ldr_reg_lsl(5, 3, 6, 2))  # ldr r5, [r3, r6, lsl #2] ; previous Stage::cameraX
    words.append(encode_mov_reg(0, 6))  # mov r0, r6 ; player slot
    words.append(encode_bl(start_addr + len(words) * 4, get_player_addr))
    words.append(encode_cmp_imm(0, 0))
    use_original_branch_index = len(words)
    words.append(0)  # beq useOriginal
    words.append(encode_mov_reg(2, 8))  # mov r2, r8 ; natural camera x target
    words.append(encode_ldr_imm(3, 0, 0xD0))  # ldr r3, [r0, #0xD0] ; actor vel x
    emit_ldr_literal(4, 0x003FFFFF)
    words.append(encode_cmp_imm(3, velocity_threshold))
    words.append(with_cond(encode_add_imm(2, 2, right_offset), 0xC))  # addgt r2, r2, #rightOffset
    words.append(encode_cmn_imm(3, velocity_threshold))
    words.append(with_cond(encode_sub_imm(2, 2, left_offset), 0x4))  # submi r2, r2, #leftOffset
    words.append(0xE0022004)  # and r2, r2, r4

    words.append(encode_sub_reg(0, 2, 5))  # sub r0, r2, r5 ; ring delta target-current
    words.append(encode_cmp_imm(0, 0x200000))
    words.append(with_cond(encode_sub_imm(0, 0, 0x400000), 0xC))  # subgt r0, r0, #span
    words.append(encode_cmn_imm(0, 0x200000))
    words.append(with_cond(encode_add_imm(0, 0, 0x400000), 0x4))  # addmi r0, r0, #span
    words.append(encode_cmp_imm(0, camera_step))
    words.append(with_cond(encode_mov_imm(0, camera_step), 0xC))  # movgt r0, #cameraStep
    words.append(encode_cmn_imm(0, camera_step))
    words.append(with_cond(encode_mov_imm(0, camera_step), 0x4))  # movmi r0, #cameraStep
    words.append(with_cond(encode_rsb_imm(0, 0, 0), 0x4))  # rsbmi r0, r0, #0
    words.append(encode_add_reg(8, 5, 0))  # add r8, r5, r0
    words.append(0xE0088004)  # and r8, r8, r4
    use_original_addr = start_addr + len(words) * 4
    words.append(0xE8BD0000 | (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 14))  # pop {r0-r5,lr}
    words.append(0xE7838106)  # str r8, [r3, r6, lsl #2]
    words.append(encode_b(start_addr + len(words) * 4, return_addr))

    words[use_original_branch_index] = with_cond(
        encode_b(start_addr + use_original_branch_index * 4, use_original_addr),
        0x0,
    )

    literal_start_addr = start_addr + (len(words) * 4)
    for word_index, rd, literal_index, cond in literal_refs:
        instruction_addr = start_addr + (word_index * 4)
        literal_addr = literal_start_addr + (literal_index * 4)
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond=cond)
    words.extend(literals)
    return words


def patch_mvl_camera_lead_from_player_velocity(
    overlays: dict[int, object],
    *,
    right_lead: int = 0x00050000,
    left_lead: int = 0x000B0000,
    neutral_lead: int = 0x00080000,
    velocity_threshold: int = 0x40,
    camera_step: int = 0x6000,
) -> list[str]:
    ov_id = 0
    store_addr = 0x020AD784
    return_addr = 0x020AD788
    stub_addr = 0x020C5300
    expected_store = 0xE7838106  # str r8, [r3, r6, lsl #2]
    stub = build_mvl_camera_lead_stub(
        stub_addr,
        return_addr=return_addr,
        get_player_addr=0x02020608,
        right_lead=right_lead,
        left_lead=left_lead,
        neutral_lead=neutral_lead,
        velocity_threshold=velocity_threshold,
        camera_step=camera_step,
    )
    if len(stub) * 4 > 0x78:
        raise ValueError(f"MvL camera lead stub too large: 0x{len(stub) * 4:X} bytes")
    old_cave = patch_overlay_words_by_id(overlays, ov_id, stub_addr, stub)
    if any(old_cave):
        raise ValueError(f"MvL camera lead stub cave 0x{stub_addr:08X} is not empty")
    branch = encode_b(store_addr, stub_addr)
    old = patch_overlay_words_by_id(overlays, ov_id, store_addr, [branch])
    old_word = struct.unpack("<I", old)[0]
    if old_word != expected_store:
        raise ValueError(f"camera store @ 0x{store_addr:08X} expected 0x{expected_store:08X}, got 0x{old_word:08X}")
    return [
        f"MvL camera lead stub overlay{ov_id} @ 0x{stub_addr:08X} bytes=0x{len(stub) * 4:X}",
        f"MvL camera lead branch overlay{ov_id} @ 0x{store_addr:08X}: {old.hex()} -> {struct.pack('<I', branch).hex()} "
        f"right=0x{right_lead:X} left=0x{left_lead:X} neutral=0x{neutral_lead:X} "
        f"threshold=0x{velocity_threshold:X} cameraStep=0x{camera_step:X}",
    ]


def patch_stage_set_zoom_camera_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    ov_id = 10
    slot_offset = (player_id & 1) * 4
    replacements = {
        0x020FB33C: 0x020CAE1C + slot_offset,  # Stage::cameraX
        0x020FB450: 0x020CAE1C + slot_offset,  # Stage::cameraX
        0x020FB454: 0x020CADA4 + slot_offset,  # Stage::cameraWidth
    }
    changes: list[str] = []
    for addr, value in replacements.items():
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [value])
        changes.append(
            f"Stage::setZoom camera slot literal overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', value).hex()} player={player_id & 1}"
        )
    return changes


def patch_stagefx_display_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # StageFX reads Game::localPlayerID while showing start/result/times-up
    # effects. Force only those display-side reads for client UX experiments;
    # do not change the global Game::localPlayerID used by gameplay logic.
    ov_id = 10
    player_id &= 1
    changes: list[str] = []
    patches = [
        # StageFX::updateVsTimesUp
        (0x020FBA88, NOP, "StageFX::updateVsTimesUp localPlayerID literal for r6"),
        (0x020FBA90, encode_mov_imm(6, player_id), "StageFX::updateVsTimesUp localPlayerID value r6"),
        (0x020FBB6C, encode_mov_imm(0, player_id), "StageFX::updateVsTimesUp status localPlayerID r0"),
        (0x020FBB74, NOP, "StageFX::updateVsTimesUp status localPlayerID load"),
        # StageFX::updateLose
        (0x020FBF1C, encode_mov_imm(0, player_id), "StageFX::updateLose localPlayerID r0"),
        (0x020FBF24, NOP, "StageFX::updateLose localPlayerID load"),
        # StageFX::updateClear
        (0x020FC17C, encode_mov_imm(0, player_id), "StageFX::updateClear localPlayerID r0"),
        (0x020FC184, NOP, "StageFX::updateClear localPlayerID load"),
        # StageFX::updateStart
        (0x020FC3AC, encode_mov_imm(0, player_id), "StageFX::updateStart localPlayerID r0 #1"),
        (0x020FC3B4, NOP, "StageFX::updateStart localPlayerID load #1"),
        (0x020FC3C8, encode_mov_imm(0, player_id), "StageFX::updateStart localPlayerID r0 #2"),
        (0x020FC3CC, NOP, "StageFX::updateStart localPlayerID load #2"),
        (0x020FC3D4, NOP, "StageFX::updateStart localPlayerID literal #3"),
        (0x020FC3DC, encode_mov_imm(0, player_id), "StageFX::updateStart localPlayerID value r0 #3"),
        (0x020FC404, encode_mov_imm(0, player_id), "StageFX::updateStart localPlayerID r0 #4"),
        (0x020FC408, NOP, "StageFX::updateStart localPlayerID load #4"),
        (0x020FC4D8, encode_mov_imm(1, player_id), "StageFX::updateStart localPlayerID r1 #5"),
        (0x020FC4E0, NOP, "StageFX::updateStart localPlayerID load #5"),
    ]
    for addr, word, label in patches:
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [word])
        changes.append(
            f"{label} overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()} player={player_id}"
        )
    return changes


def patch_stage_layout_inventory_display_player_id(
    overlays: dict[int, object],
    player_id: int,
    *,
    mode: str,
) -> list[str]:
    # StageLayout owns the lower-screen MvL HUD. These patches only replace
    # the argument passed to Game::getPlayerInventoryPowerup(), keeping the
    # inventory write/consume path untouched.
    ov_id = 0
    player_id &= 1
    mode_patches = {
        "hud": [
            (0x020BE934, "StageLayout inventory HUD primary read #1"),
            (0x020BE95C, "StageLayout inventory HUD primary read #2"),
        ],
        "all-read": [
            (0x020BE1EC, "StageLayout inventory availability read"),
            (0x020BE934, "StageLayout inventory HUD primary read #1"),
            (0x020BE95C, "StageLayout inventory HUD primary read #2"),
            (0x020BFB64, "StageLayout inventory state read"),
            (0x020C06B8, "StageLayout inventory spawn/read animation"),
        ],
    }
    if mode not in mode_patches:
        raise ValueError(f"unknown StageLayout inventory display patch mode: {mode}")

    word = encode_mov_imm(0, player_id)
    changes: list[str] = []
    for addr, label in mode_patches[mode]:
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [word])
        changes.append(
            f"{label} overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()} player={player_id} mode={mode}"
        )
    return changes


def patch_stage_layout_inventory_use_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # StageLayout consumes the lower-screen stock item through
    # Game::setPlayerInventoryPowerup(player, 0). Force only that consume-side
    # player argument; display reads are handled by
    # stage-layout-inventory-display-player-id.
    ov_id = 0
    addr = 0x020BF594
    original = 0xE1A00004  # mov r0, r4
    word = encode_mov_imm(0, player_id & 1)
    old = patch_overlay_words_by_id(overlays, ov_id, addr, [word])
    old_word = struct.unpack("<I", old)[0]
    if old_word != original:
        raise ValueError(
            f"StageLayout inventory consume player arg @ 0x{addr:08X} expected "
            f"0x{original:08X}, got 0x{old_word:08X}"
        )
    return [
        f"StageLayout inventory consume player arg overlay{ov_id} @ 0x{addr:08X}: "
        f"0x{old_word:08X} -> 0x{word:08X} player={player_id & 1}"
    ]


def patch_vs_results_display_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # VSResults stores the display-local player in its scene object around
    # +0x9B. The actual winner/result variable must stay untouched; only the
    # init-time comparison that chooses the local win/lose text is fixed.
    # Other +0x9B reads also affect result panel resource indices; forcing them
    # broadly can select invalid tile data on the lose path.
    ov_id = 52
    player_id &= 1
    patches = [
        (0x02156B0C, encode_mov_imm(1, player_id), "VSResults init win/lose local player read"),
    ]
    changes: list[str] = []
    for addr, word, label in patches:
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [word])
        changes.append(
            f"{label} overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()} player={player_id}"
        )
    return changes


def parse_int_list(value: str) -> list[int]:
    result: list[int] = []
    for item in value.replace(";", ",").split(","):
        item = item.strip()
        if item:
            result.append(int(item, 0))
    return result


def patch_overlay0_localplayer_literal_alias(
    overlays: dict[int, object],
    alias_addr: int,
    mode: str,
    literal_addrs: list[int] | None = None,
) -> list[str]:
    # Diagnostic only: keep Game::localPlayerID itself unchanged, but make the
    # stage/layout/collision code in overlay0 read a zero-like byte instead.
    # This isolates whether localPlayerID=1 breaks the visible stage through
    # overlay0's broad layout/camera path.
    local_player_addr = 0x02085A7C
    actor_collision_literal_addrs = [
        0x020991EC,
        0x0209ADAC,
        0x0209C6BC,
        0x020A1D30,
        0x020A2EE0,
        0x020A36D4,
        0x020A6F40,
        0x020A9D34,
        0x020ACF38,
        0x020AD008,
        0x020AD26C,
        0x020AEA0C,
    ]
    layout_literal_addrs = [
        0x020AF748,
        0x020B05A8,
        0x020B1038,
        0x020B6908,
        0x020B6DF4,
        0x020B8D20,
        0x020BA194,
        0x020BACC0,
        0x020BC668,
        0x020BCBC0,
        0x020BE7D0,
        0x020BF704,
        0x020BFFD8,
        0x020C006C,
        0x020C00EC,
        0x020C02B4,
        0x020C07D8,
        0x020C0D78,
    ]
    # These late StageLayout literals include the lower-screen stock item path.
    # Aliasing them to player0 fixes some broad local1 display paths, but makes
    # client/local1 touches consume player0's stock item. Keep them out for the
    # local1 bootstrap route and patch the narrower display/use call sites
    # separately.
    inventory_sensitive_layout_addrs = [
        0x020BE7D0,
        0x020BF704,
        0x020BFFD8,
        0x020C006C,
        0x020C00EC,
        0x020C02B4,
        0x020C07D8,
        0x020C0D78,
    ]
    if literal_addrs is not None:
        literal_addrs = sorted(set(literal_addrs))
    elif mode == "all":
        literal_addrs = sorted(set(actor_collision_literal_addrs + layout_literal_addrs))
    elif mode == "all-no-inventory":
        literal_addrs = sorted(set(
            actor_collision_literal_addrs +
            [addr for addr in layout_literal_addrs if addr not in inventory_sensitive_layout_addrs]
        ))
    elif mode == "layout":
        literal_addrs = sorted(set(layout_literal_addrs))
    elif mode == "actor-collision":
        literal_addrs = sorted(set(actor_collision_literal_addrs))
    else:
        raise ValueError(f"unknown overlay0 localPlayerID alias mode: {mode}")
    changes: list[str] = []
    for addr in literal_addrs:
        ov_id = 0
        old = patch_overlay_words_by_id(overlays, ov_id, addr, [alias_addr])
        old_word = struct.unpack("<I", old)[0]
        if old_word != local_player_addr:
            raise ValueError(
                f"overlay0 localPlayerID literal @ 0x{addr:08X} was "
                f"0x{old_word:08X}, expected 0x{local_player_addr:08X}"
            )
        changes.append(
            f"overlay0 localPlayerID literal alias overlay{ov_id} @ 0x{addr:08X}: "
            f"0x{old_word:08X} -> 0x{alias_addr:08X} mode={mode}"
        )
    return changes


def patch_stage_range_localplayer_literal_alias(arm9, alias_addr: int) -> list[str]:
    local_player_addr = 0x02085A7C
    # Stage::isOutsidePlayerRange has two localPlayerID references that share
    # this literal pool word. Redirecting it is a narrow diagnostic for player
    # render culling without touching Game::localPlayerID itself.
    addr = 0x0200AFFC
    old = patch_arm9_words(arm9, addr, [alias_addr])
    old_word = struct.unpack("<I", old)[0]
    if old_word != local_player_addr:
        raise ValueError(
            f"Stage::isOutsidePlayerRange localPlayerID literal @ 0x{addr:08X} was "
            f"0x{old_word:08X}, expected 0x{local_player_addr:08X}"
        )
    return [
        f"Stage::isOutsidePlayerRange localPlayerID literal alias @ 0x{addr:08X}: "
        f"0x{old_word:08X} -> 0x{alias_addr:08X}"
    ]


def patch_wifi_communicating_consoles(arm9, count: int) -> list[str]:
    if count < 1 or count > 4:
        raise ValueError(f"communicating console count must be 1..4, got {count}")

    # Diagnostic for direct MvL entry. The natural StageLayout init loops over
    # Wifi::getCommunicatingConsoleCount() and initializes per-view Stage
    # arrays, including Stage::liquidPosition[]. Direct single-instance boot
    # reports one console, which leaves slot1 uninitialized when
    # Game::localPlayerID is 1.
    count_addr = 0x02046C34
    count_words = [
        encode_mov_imm(0, count),
        BX_LR,
    ]
    old_count = patch_arm9_words(arm9, count_addr, count_words)

    is_comm_addr = 0x02046C44
    is_comm_words = [
        encode_cmp_imm(0, count),
        with_cond(encode_mov_imm(0, 1), 3),  # LO
        with_cond(encode_mov_imm(0, 0), 2),  # HS
        BX_LR,
    ]
    old_is_comm = patch_arm9_words(arm9, is_comm_addr, is_comm_words)

    return [
        f"Wifi::getCommunicatingConsoleCount @ 0x{count_addr:08X}: "
        f"{old_count.hex()} -> {words_hex(count_words)} count={count}",
        f"Wifi::isConsoleCommunicating @ 0x{is_comm_addr:08X}: "
        f"{old_is_comm.hex()} -> {words_hex(is_comm_words)} count={count}",
    ]


def patch_stage_object_activation_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # Stage object activation chooses a player camera/range before it decides
    # which StageObject records to instantiate. In direct localPlayerID=1, this
    # skips the early Goomba that host localPlayerID=0 spawns. Force only this
    # activation scan to use the requested player while leaving Game::localPlayerID
    # unchanged for gameplay, UI, camera, and player-local logic.
    if player_id < 0 or player_id > 1:
        raise ValueError(f"player id must be 0 or 1, got {player_id}")

    ov_id = 0
    hook_addr = 0x0209B048
    return_addr = 0x0209B050
    stub_addr = 0x020C53B0
    get_player_addr = 0x02020608
    stub = [
        encode_push(1 << 14),                         # push {lr}
        encode_mov_imm(0, player_id),                 # mov r0, #player_id
        encode_str_imm(0, 13, 0x0C),                  # str r0, [sp, #0x0C] (original sp+8)
        encode_bl(stub_addr + 0x0C, get_player_addr), # bl Game::getPlayer(player_id)
        0xE8BD4000,                                   # pop {lr}
        encode_b(stub_addr + 0x14, return_addr),      # b original ldrb r0, [r0, #0x2be]
    ]
    old_stub = patch_overlay_words_by_id(overlays, ov_id, stub_addr, stub)
    if old_stub != b"\0" * len(old_stub):
        raise ValueError(f"stage object activation player stub cave @ 0x{stub_addr:08X} is not empty")
    branch = encode_b(hook_addr, stub_addr)
    old_hook = patch_overlay_words_by_id(overlays, ov_id, hook_addr, [branch, NOP])
    return [
        f"Stage object activation player stub overlay{ov_id} @ 0x{stub_addr:08X}: "
        f"{old_stub.hex()} -> {words_hex(stub)}",
        f"Stage object activation player hook overlay{ov_id} @ 0x{hook_addr:08X}: "
        f"{old_hook.hex()} -> {words_hex([branch, NOP])} player={player_id}",
    ]


def patch_stage_object_activation_wrap_width(overlays: dict[int, object], width: int) -> list[str]:
    # Diagnostic only. The localPlayerID=1 direct route leaves Game::wrapX as
    # 0xFFFFFFFF, so the 0x0209B320 stage object activation path computes a
    # wrapped stage width of zero and skips the left-side Goomba. Force the
    # computed tile-space width to match the MvL wrapped stage while we trace
    # the real initialization source.
    if width < 0:
        raise ValueError(f"width must be non-negative, got {width}")

    ov_id = 0
    hook_addr = 0x0209B3AC
    word = encode_mov_imm(2, width)
    old_hook = patch_overlay_words_by_id(overlays, ov_id, hook_addr, [word])
    return [
        f"Stage object activation wrap width overlay{ov_id} @ 0x{hook_addr:08X}: "
        f"{old_hook.hex()} -> {words_hex([word])} width=0x{width:X}",
    ]


def patch_stage_object_activation_force_wrap_x(overlays: dict[int, object], wrap_x: int) -> list[str]:
    # Diagnostic only. Write Game::wrapX at the beginning of the 0x0209B320
    # activation path, then execute the two original instructions we replaced.
    if wrap_x < 0 or wrap_x > 0xFFFFFFFF:
        raise ValueError(f"wrap_x must be a u32, got {wrap_x}")

    ov_id = 0
    hook_addr = 0x0209B328
    return_addr = 0x0209B330
    stub_addr = 0x020C53D0
    game_wrap_x_addr = 0x02085AA4
    original_stage_blocks_addr = 0x0208B168
    stub = [
        encode_ldr_pc_imm(2, 0x14),                   # ldr r2, =Game::wrapX
        encode_ldr_pc_imm(3, 0x14),                   # ldr r3, =wrap_x
        encode_str_imm(3, 2, 0),                      # str r3, [r2]
        encode_str_imm(3, 2, 4),                      # str r3, [r2, #4]
        encode_ldr_pc_imm(1, 0x0C),                   # ldr r1, =StageBlocks
        encode_str_imm(0, 13, 8),                     # str r0, [sp, #8]
        encode_b(stub_addr + 0x18, return_addr),      # b 0x0209B330
        game_wrap_x_addr,
        wrap_x & 0xFFFFFFFF,
        original_stage_blocks_addr,
    ]
    old_stub = patch_overlay_words_by_id(overlays, ov_id, stub_addr, stub)
    if old_stub != b"\0" * len(old_stub):
        raise ValueError(f"stage object activation force wrapX stub cave @ 0x{stub_addr:08X} is not empty")
    branch = encode_b(hook_addr, stub_addr)
    old_hook = patch_overlay_words_by_id(overlays, ov_id, hook_addr, [branch])
    return [
        f"Stage object activation force wrapX stub overlay{ov_id} @ 0x{stub_addr:08X}: "
        f"{old_stub.hex()} -> {words_hex(stub)}",
        f"Stage object activation force wrapX hook overlay{ov_id} @ 0x{hook_addr:08X}: "
        f"{old_hook.hex()} -> {words_hex([branch])} wrapX=0x{wrap_x:08X}",
    ]


def patch_player_render_model_visible(overlays: dict[int, object]) -> list[str]:
    # Diagnostic only: Player::renderModel(bool) receives a boolean that is 1
    # when the player is treated as outside/remote for display. Force the value
    # to 0 at function entry to see whether the missing client player is only a
    # visibility/culling flag problem or a deeper camera-coordinate problem.
    hook_addr = 0x020FCF74
    return_addr = 0x020FCF7C
    stub_addr = 0x02122FF4
    original_store = 0xE58D1000  # str r1, [sp]
    original_add = 0xE2801C06  # add r1, r0, #0x600
    stub = [
        encode_mov_imm(1, 0),
        original_store,
        original_add,
        encode_b(stub_addr + 0x0C, return_addr),
    ]
    old_stub = patch_overlay_words_by_id(overlays, 10, stub_addr, stub)
    if any(old_stub):
        raise ValueError(f"Player::renderModel visible arg stub cave @ 0x{stub_addr:08X} is not empty")
    branch = encode_b(hook_addr, stub_addr)
    old = patch_overlay_words_by_id(overlays, 10, hook_addr, [branch, NOP])
    old_words = struct.unpack("<II", old)
    if old_words != (original_store, original_add):
        raise ValueError(
            f"Player::renderModel visible arg hook @ 0x{hook_addr:08X} expected "
            f"0x{original_store:08X}/0x{original_add:08X}, got 0x{old_words[0]:08X}/0x{old_words[1]:08X}"
        )
    return [
        f"Player::renderModel visible arg stub overlay10 @ 0x{stub_addr:08X}: {old_stub.hex()} -> {words_hex(stub)}",
        f"Player::renderModel visible arg hook overlay10 @ 0x{hook_addr:08X}: {old.hex()} -> {words_hex([branch, NOP])}"
    ]


def patch_player_render_range_view_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # Diagnostic only: Player::renderModel() asks Stage::isOutsidePlayerRange()
    # with a view/player slot loaded from Player+0x11E. Force just that argument
    # to separate "range slot/camera state is wrong" from "renderModel(bool)
    # has a different semantic".
    if player_id < 0 or player_id > 1:
        raise ValueError(f"player id must be 0 or 1, got {player_id}")
    addr = 0x020FD0C4
    original = 0xE1D021DE  # ldrsb r2, [r0, #0x1e]
    replacement = encode_mov_imm(2, player_id)
    old = patch_overlay_words_by_id(overlays, 10, addr, [replacement])
    old_word = struct.unpack("<I", old)[0]
    if old_word != original:
        raise ValueError(
            f"Player::renderModel range view hook @ 0x{addr:08X} expected "
            f"0x{original:08X}, got 0x{old_word:08X}"
        )
    return [
        f"Player::renderModel range view arg overlay10 @ 0x{addr:08X}: "
        f"0x{old_word:08X} -> 0x{replacement:08X} playerID={player_id}"
    ]


def patch_player_view_transit_local_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # Diagnostic only. Player::viewTransitState compares Player+0x11E against
    # Game::localPlayerID and then runs a large amount of local-only transit
    # setup. Force only that comparison's loaded localPlayerID to see whether
    # the direct localPlayerID=1 route is leaking into simulation setup.
    if player_id < 0 or player_id > 1:
        raise ValueError(f"player id must be 0 or 1, got {player_id}")
    addr = 0x0211872C
    original = 0xE5920000  # ldr r0, [r2]
    replacement = encode_mov_imm(0, player_id)
    old = patch_overlay_words_by_id(overlays, 10, addr, [replacement])
    old_word = struct.unpack("<I", old)[0]
    if old_word != original:
        raise ValueError(
            f"Player::viewTransitState localPlayerID load @ 0x{addr:08X} expected "
            f"0x{original:08X}, got 0x{old_word:08X}"
        )
    return [
        f"Player::viewTransitState localPlayerID load overlay10 @ 0x{addr:08X}: "
        f"0x{old_word:08X} -> 0x{replacement:08X} playerID={player_id}"
    ]


def patch_player_vs_pipe_local_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # Diagnostic only. Player::vsPipeTransitState also gates a local-only path
    # on Player+0x11E == Game::localPlayerID near the end of the pipe transit.
    # Force just that comparison to identify whether the start-life/state issue
    # comes from this transition instead of the broader viewTransitState path.
    if player_id < 0 or player_id > 1:
        raise ValueError(f"player id must be 0 or 1, got {player_id}")
    addr = 0x0211C59C
    original = 0xE5911000  # ldr r1, [r1]
    replacement = encode_mov_imm(1, player_id)
    old = patch_overlay_words_by_id(overlays, 10, addr, [replacement])
    old_word = struct.unpack("<I", old)[0]
    if old_word != original:
        raise ValueError(
            f"Player::vsPipeTransitState localPlayerID load @ 0x{addr:08X} expected "
            f"0x{original:08X}, got 0x{old_word:08X}"
        )
    return [
        f"Player::vsPipeTransitState localPlayerID load overlay10 @ 0x{addr:08X}: "
        f"0x{old_word:08X} -> 0x{replacement:08X} playerID={player_id}"
    ]


def patch_mvl_load_thread_entrance_ids(overlays: dict[int, object]) -> list[str]:
    # The direct route supplies finite-life sceneSettings before the normal
    # CourseSelect state has prepared spawn IDs. loadMvsLFilesThread otherwise
    # copies those settings bytes into both entrance slots, which places
    # Luigi's initial pipe at Mario's entrance. Keep player 0/1 on entrances 0/1.
    patches = [
        (0x02152D64, encode_mov_imm(0, 1), "player1 early temp value"),
        (0x02152D68, encode_strb_imm(0, 13, 0x1D), "player1 early temp store"),
        (0x02152D74, encode_strb_imm(0, 13, 0x1C), "player0 early temp store"),
        (0x02152DC0, encode_mov_imm(0, 0), "player0 temp"),
        (0x02152DC8, encode_mov_imm(0, 1), "player1 temp"),
        (0x02152E00, encode_mov_imm(0, 0), "player0"),
        (0x02152E0C, encode_mov_imm(0, 1), "player1"),
    ]
    changes: list[str] = []
    for addr, word, label in patches:
        ov_id, old = patch_overlay_words(overlays, addr, [word])
        changes.append(
            f"loadMvsLFilesThread entrance ID {label} overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()}"
        )
    return changes


def patch_player_render_wrap_x_offset(overlays: dict[int, object], offset: int) -> list[str]:
    # Diagnostic only. In the direct localPlayerID=1 client route the terrain
    # can be visible while Player::onRender passes unwrapped X coordinates to
    # Stage::isOutsideCamera, so both players are treated as outside the camera.
    # Hook the first display-Vec3 X store and add a fixed wrap offset.
    hook_addr = 0x020FCB04
    stub_addr = 0x020C5394
    display_vec_aux_addr = 0x0212AFD0
    if offset >= 0:
        adjust = encode_add_imm(1, 1, offset)
    else:
        adjust = encode_sub_imm(1, 1, -offset)
    stub = [
        adjust,
        encode_str_imm(1, 0, 4),
        encode_ldr_imm(1, 5, 0x64),
        encode_ldr_pc_literal(12, stub_addr + 0x0C, stub_addr + 0x18),
        encode_b(stub_addr + 0x10, 0x020FCB10),
        NOP,
        display_vec_aux_addr,
    ]
    old_stub = patch_overlay_words_by_id(overlays, 0, stub_addr, stub)
    if any(old_stub):
        raise ValueError(f"player render wrap stub cave @ 0x{stub_addr:08X} is not empty")
    branch = encode_b(hook_addr, stub_addr)
    old_hook = patch_overlay_words_by_id(overlays, 10, hook_addr, [branch])
    return [
        f"Player::onRender wrap-x stub overlay0 @ 0x{stub_addr:08X}: {old_stub.hex()} -> {words_hex(stub)}",
        f"Player::onRender wrap-x hook overlay10 @ 0x{hook_addr:08X}: {old_hook.hex()} -> {struct.pack('<I', branch).hex()} offset=0x{offset:X}",
    ]


def patch_player_render_r12_offset(overlays: dict[int, object], offset: int) -> list[str]:
    # Diagnostic only. Player::renderModel sees r12 as a camera-relative
    # transform component. With player1 camera this value remains positive in
    # the localPlayerID=1 route, while the visible player0-camera route uses a
    # wrapped negative value. Adjust r12 at function entry to test whether the
    # missing model is caused by that final render transform rather than actor
    # state or G3D submission.
    hook_addr = 0x020FCF6C
    return_addr = 0x020FCF70
    stub_addr = 0x02122FF4
    original_prologue = 0xE92D4000  # push {lr}
    if offset >= 0:
        adjust = encode_add_imm(12, 12, offset)
    else:
        adjust = encode_sub_imm(12, 12, -offset)
    stub = [
        original_prologue,
        adjust,
        encode_b(stub_addr + 0x08, return_addr),
    ]
    old_stub = patch_overlay_words_by_id(overlays, 10, stub_addr, stub)
    if any(old_stub):
        raise ValueError(f"player render r12 stub cave @ 0x{stub_addr:08X} is not empty")
    branch = encode_b(hook_addr, stub_addr)
    old_hook = patch_overlay_words_by_id(overlays, 10, hook_addr, [branch])
    old_word = struct.unpack("<I", old_hook)[0]
    if old_word != original_prologue:
        raise ValueError(
            f"Player::renderModel hook @ 0x{hook_addr:08X} expected 0x{original_prologue:08X}, got 0x{old_word:08X}"
        )
    return [
        f"Player::renderModel r12-offset stub overlay10 @ 0x{stub_addr:08X}: {old_stub.hex()} -> {words_hex(stub)}",
        f"Player::renderModel r12-offset hook overlay10 @ 0x{hook_addr:08X}: {old_hook.hex()} -> {struct.pack('<I', branch).hex()} offset={offset}",
    ]


def patch_player_signal_locked_noop(overlays: dict[int, object]) -> list[str]:
    # Diagnostic MvL patch: PlayerBase::signalLocked() locks the other player
    # through PlayerBase::updateLocked during death transitions. Returning early
    # lets us verify whether this is the source of the unwanted gameplay pause.
    addr = 0x0212C1B8
    overlay_id, old = patch_overlay_words(overlays, addr, [BX_LR])
    return [
        f"PlayerBase::signalLocked no-op overlay{overlay_id} @ 0x{addr:08X}: {old.hex()} -> {struct.pack('<I', BX_LR).hex()}",
    ]


def patch_player_freeze_stage_noop(overlays: dict[int, object]) -> list[str]:
    # Diagnostic MvL patch: PlayerBase::freezeStage() is called from Luigi's
    # damage/death path and pauses non-player stage actors while the transition
    # runs. Returning early lets us verify whether that is the remaining enemy
    # freeze after signalLocked() has been disabled.
    addr = 0x0212C130
    overlay_id, old = patch_overlay_words(overlays, addr, [BX_LR])
    return [
        f"PlayerBase::freezeStage no-op overlay{overlay_id} @ 0x{addr:08X}: {old.hex()} -> {struct.pack('<I', BX_LR).hex()}",
    ]


def patch_player_stage_lock_vsmode_noop(overlays: dict[int, object]) -> list[str]:
    # Narrower MvL diagnostic patch. Player::beginDeathTransition calls
    # PlayerBase::freezeStage() and PlayerBase::signalLocked(); that is too
    # broad for Mario vs Luigi because it pauses the living player and stage
    # actors during a VS respawn. Keep the original behavior outside VS mode.
    patches = [
        (0x0212C130, 0x020C5390, 0xE92D4000, "PlayerBase::freezeStage VS-mode skip"),
        (0x0212C1B8, 0x020C53B0, 0xE92D4010, "PlayerBase::signalLocked VS-mode skip"),
    ]
    changes: list[str] = []
    for func_addr, stub_addr, original_word, label in patches:
        stub = [
            0xE59FC010,  # ldr ip, [pc, #0x10]
            0xE5DCC000,  # ldrb ip, [ip]
            0xE35C0000,  # cmp ip, #0
            0x112FFF1E,  # bxne lr
            original_word,
            encode_b(stub_addr + 0x14, func_addr + 0x04),
            0x02085A84,  # Game::vsMode
        ]
        old_stub = patch_overlay_words_by_id(overlays, 0, stub_addr, stub)
        if any(old_stub):
            raise ValueError(f"{label} stub cave @ 0x{stub_addr:08X} is not empty")
        branch = encode_b(func_addr, stub_addr)
        hook_overlay_id, old_hook = patch_overlay_words(overlays, func_addr, [branch])
        old_hook_word = struct.unpack("<I", old_hook)[0]
        if old_hook_word != original_word:
            raise ValueError(
                f"{label} hook @ 0x{func_addr:08X} expected 0x{original_word:08X}, got 0x{old_hook_word:08X}"
            )
        changes.append(
            f"{label} stub overlay0 @ 0x{stub_addr:08X}: {old_stub.hex()} -> {words_hex(stub)}"
        )
        changes.append(
            f"{label} hook overlay{hook_overlay_id} @ 0x{func_addr:08X}: {old_hook.hex()} -> {struct.pack('<I', branch).hex()}"
        )
    return changes


def patch_stage_layout_final_view_player_id(
    overlays: dict[int, object],
    player_id: int,
    which: str,
) -> list[str]:
    # Diagnostic only. The single 0x020BACC0 localPlayerID literal is enough to
    # bring terrain back in the localPlayerID=1 direct route, but it aliases a
    # whole StageLayout update function. Narrow that to the final per-view calls
    # that receive r1=view/player ID.
    patches: list[tuple[int, str]] = []
    if which in ("prepare", "both"):
        patches.append((0x020BAC84, "StageLayout final prepare/view call player arg"))
    if which in ("render", "both"):
        patches.append((0x020BAC90, "StageLayout final render/view call player arg"))
    if not patches:
        raise ValueError(f"unknown stage-layout-final-view-player-id mode: {which}")
    word = encode_mov_imm(1, player_id & 1)
    changes: list[str] = []
    for addr, label in patches:
        old = patch_overlay_words_by_id(overlays, 0, addr, [word])
        changes.append(
            f"{label} overlay0 @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()} player={player_id}"
        )
    return changes


def patch_stage_entity_skip_render_player_id(overlays: dict[int, object], player_id: int) -> list[str]:
    # Display-only diagnostic. StageEntity::skipRender chooses a camera/range
    # slot from Game::localPlayerID. In the hybrid route simulation remains
    # canonical local0, while the client display camera can be player1. Force
    # only skipRender's camera slot so enemies near the Luigi view are drawn.
    if player_id < 0 or player_id > 1:
        raise ValueError(f"player id must be 0 or 1, got {player_id}")
    addr = 0x0209AD60
    original = 0xE5911000  # ldr r1, [r1]
    replacement = encode_mov_imm(1, player_id)
    old = patch_overlay_words_by_id(overlays, 0, addr, [replacement])
    old_word = struct.unpack("<I", old)[0]
    if old_word != original:
        raise ValueError(
            f"StageEntity::skipRender localPlayerID load @ 0x{addr:08X} expected "
            f"0x{original:08X}, got 0x{old_word:08X}"
        )
    return [
        f"StageEntity::skipRender display player overlay0 @ 0x{addr:08X}: "
        f"0x{old_word:08X} -> 0x{replacement:08X} playerID={player_id}"
    ]


def patch_stage_entity_liquid_check_result(overlays: dict[int, object], *, in_liquid: bool) -> list[str]:
    # Diagnostic only. StageEntity::updateLiquidCollision branches on the
    # result of the liquid tile probe at 0x0209C6F8/0x0209C6FC. Forcing the
    # branch result tells us whether localPlayerID=1 still has a bad
    # collision/liquid map after the wrapX diagnostic patch.
    cmp_addr = 0x0209C6F8
    branch_addr = 0x0209C6FC
    false_path_addr = 0x0209C780
    true_path_addr = 0x0209C700

    if in_liquid:
        # Force the condition to fall through into the liquid path.
        words = [encode_mov_imm(0, 1), NOP]
        description = "force liquid"
    else:
        # Force the non-liquid path, equivalent to the probe returning 0.
        words = [NOP, encode_b(branch_addr, false_path_addr)]
        description = "force non-liquid"

    old = patch_overlay_words_by_id(overlays, 0, cmp_addr, words)
    return [
        f"StageEntity::updateLiquidCollision {description} overlay0 @ 0x{cmp_addr:08X}: "
        f"{old.hex()} -> {words_hex(words)} target=0x{true_path_addr if in_liquid else false_path_addr:08X}"
    ]


def build_direct_loadlevel_stub(
    start_addr: int,
    load_level_addr: int,
    *,
    scene: int,
    stage: int,
    act: int,
    player_id: int,
    entrance: int,
    flag: int,
    unused1: int,
    control_options: int,
    unused2: int,
    challenge_mode: int,
    rng_seed: int,
    force_scene_settings: int | None = None,
    load_mvl_files_before_addr: int | None = None,
    load_mvl_files_after_addr: int | None = None,
) -> list[int]:
    stack_values = [
        act,        # act
        player_id,  # playerID
        3,          # playerMask
        0,          # character1: Mario
        1,          # character2: Luigi
        0,          # powerup
        entrance,   # entrance
        flag,       # flag
        unused1,    # unused1
        control_options,  # controlOptions
        unused2,    # unused2
        challenge_mode,  # challengeMode
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
    ]
    if load_mvl_files_before_addr is not None:
        bl_load_files_addr = start_addr + len(words) * 4
        words.append(encode_bl(bl_load_files_addr, load_mvl_files_before_addr))
    words.extend([
        encode_sub_sp_imm(0x38),
        encode_mov_imm(0, scene),
        encode_mov_imm(1, 1),          # vs mode
        encode_mov_imm(2, 9),          # MvL stage group
        encode_mov_imm(3, stage),
    ])
    literals: list[int] = []
    literal_refs: list[tuple[int, int, int, int]] = []

    def emit_ldr_literal(rd: int, value: int, *, cond: int = 0xE) -> None:
        literal_index = len(literals)
        literals.append(value)
        word_index = len(words)
        words.append(0)
        literal_refs.append((word_index, rd, literal_index, cond))

    current_ip_value: int | None = None
    for i, value in enumerate(stack_values):
        if current_ip_value != value:
            words.append(encode_load_imm(12, value))
            current_ip_value = value
        words.append(encode_str_imm(12, 13, i * 4))

    bl_addr = start_addr + len(words) * 4
    words.append(encode_bl(bl_addr, load_level_addr))
    if load_mvl_files_after_addr is not None:
        bl_load_files_addr = start_addr + len(words) * 4
        words.append(encode_bl(bl_load_files_addr, load_mvl_files_after_addr))
    if force_scene_settings is not None:
        emit_ldr_literal(0, 0x02088F38)  # Scene::nextSceneSettings
        emit_ldr_literal(1, force_scene_settings)
        words.append(encode_str_imm(1, 0, 0))
    # The direct route bypasses the normal CourseSelect setup. With finite lives,
    # NSMB reuses the lives byte from sceneSettings as both players' entrance ID,
    # which also leaves Luigi's spawn pipe at Mario's entrance. Normalize the
    # entrance globals before the stage creates the initial player/pipe actors.
    emit_ldr_literal(0, 0x0208B094)  # Entrance::spawnEntranceID[0]
    words.append(encode_mov_imm(1, 0))
    words.append(encode_strb_imm(1, 0, 0))
    words.append(encode_mov_imm(1, 1))
    words.append(encode_strb_imm(1, 0, 1))
    emit_ldr_literal(0, 0x0208B098)  # Entrance::transitionFlags[0]
    words.append(encode_mov_imm(1, 0))
    words.append(encode_strb_imm(1, 0, 0))
    words.append(encode_strb_imm(1, 0, 1))
    emit_ldr_literal(0, 0x0208B0A0)  # Entrance::spawnEntrance[0]
    words.append(encode_ldr_imm(1, 0, 0))
    words.append(encode_add_imm(2, 1, 0x14))
    words.append(encode_str_imm(2, 0, 4))
    # Direct MvL entry bypasses the normal local MP pairing path, so
    # Net::localAid stays at its single-player default. VSResults uses
    # Net::localAid, not Game::localPlayerID, to decide local win/lose text.
    emit_ldr_literal(0, 0x020887F0)  # Net::localAid
    words.append(encode_mov_imm(1, player_id & 3))
    words.append(encode_str_imm(1, 0, 0))
    words.extend([
        encode_add_sp_imm(0x38),
        encode_mov_imm(0, 1),
        POP_PC | (1 << 4),             # pop {r4, pc}
    ])

    literal_start_addr = start_addr + (len(words) * 4)
    for word_index, rd, literal_index, cond in literal_refs:
        instruction_addr = start_addr + (word_index * 4)
        literal_addr = literal_start_addr + (literal_index * 4)
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond=cond)
    words.extend(literals)
    return words


def patch_direct_mvl_entry(
    rom: NintendoDSRom,
    symbols: dict[str, int],
    *,
    scene: int,
    stage: int,
    act: int,
    player_id: int,
    entrance: int,
    flag: int,
    unused1: int,
    control_options: int,
    unused2: int,
    challenge_mode: int,
    rng_seed: int,
    first_scene: int,
    skip_direct_loadlevel: bool,
    force_ready_progress: bool,
    force_transfer_result: int | None,
    clear_actor_category_mask: bool,
    force_scene_settings: int | None,
    call_load_mvl_files: bool,
    call_load_mvl_files_after: bool,
    stage_camera_player_id: int | None,
    stage_camera_state_player_id: int | None,
    camera_fallback_slot_zero: bool,
    camera_player1_out_of_view_slot0: bool,
    camera_focus_loop_count: int | None,
    mvl_camera_lead_from_player_velocity: bool,
    mvl_camera_right_lead: int,
    mvl_camera_left_lead: int,
    mvl_camera_neutral_lead: int,
    mvl_camera_velocity_threshold: int,
    mvl_camera_step: int,
    stage_set_zoom_camera_player_id: int | None,
    stagefx_display_player_id: int | None,
    stage_layout_inventory_display_player_id: int | None,
    stage_layout_inventory_display_mode: str,
    vs_results_display_player_id: int | None,
    player_signal_locked_noop: bool,
    player_freeze_stage_noop: bool,
    player_stage_lock_vsmode_noop: bool,
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
            act=act,
            player_id=player_id,
            entrance=entrance,
            flag=flag,
            unused1=unused1,
            control_options=control_options,
            unused2=unused2,
            challenge_mode=challenge_mode,
            rng_seed=rng_seed,
            force_scene_settings=force_scene_settings,
            load_mvl_files_before_addr=symbols["_ZN14VSConnectScene19loadMvsLFilesThreadEv"]
            if call_load_mvl_files else None,
            load_mvl_files_after_addr=symbols["_ZN14VSConnectScene19loadMvsLFilesThreadEv"]
            if call_load_mvl_files_after else None,
        )
        ov_id, old = patch_overlay_words(overlays, update_addr, stub)
        changes.append(
            f"VSConnectScene::updateLoadGameSM direct loadLevel stub overlay{ov_id} @ 0x{update_addr:08X}: "
            f"{old.hex()} -> {words_hex(stub)}"
        )

    if force_ready_progress:
        # Direct loadLevel reaches VSStageIntro, but without a real LocalMP
        # session the ready synchronization never completes. Reuse the same
        # wait bypass used by the fake-opponent diagnostic path.
        vs_stage_intro_wait_addr = 0x02152888
        ov_id, old = patch_overlay_words(overlays, vs_stage_intro_wait_addr, [NOP])
        changes.append(
            f"VSStageIntro ready wait branch NOP overlay{ov_id} @ 0x{vs_stage_intro_wait_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )

    if force_transfer_result is not None:
        # Direct-entry diagnostic equivalent of the fake-opponent transfer
        # bypass. This tells whether VSStageIntro can progress when the game
        # observes the expected peer action packet bits.
        addr = symbols["_ZN3Net4Core14transferPacketENS_12PacketActionE"]
        words = [
            encode_mov_imm(0, force_transfer_result),
            BX_LR,
        ]
        old = patch_arm9_words(arm9, addr, words)
        changes.append(
            f"Net::Core::transferPacket forced result 0x{force_transfer_result:X} @ 0x{addr:08X}: "
            f"{old.hex()} -> {words_hex(words)}"
        )

    if clear_actor_category_mask:
        # loadMvsLFilesThread normally writes 0x26 to Actor::category mask
        # at 0x020CA850, freezing categories until the VS ready flow clears it.
        # The fake/direct PoC paths can miss that clear, so make the initial
        # value zero for diagnostic ROMs.
        addr = 0x02152E64
        word = encode_mov_imm(1, 0)
        ov_id, old = patch_overlay_words(overlays, addr, [word])
        changes.append(
            f"loadMvsLFilesThread actor category mask value overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()}"
        )

    changes.extend(patch_mvl_load_thread_entrance_ids(overlays))

    if stage_camera_player_id is not None:
        changes.extend(patch_stage_camera_player_id(overlays, stage_camera_player_id))
    if stage_camera_state_player_id is not None:
        changes.extend(patch_stage_camera_state_player_id(overlays, stage_camera_state_player_id))
    if camera_fallback_slot_zero or camera_player1_out_of_view_slot0:
        changes.extend(patch_is_out_of_view_vertical_camera_fallback(
            overlays,
            player1_slot0=camera_player1_out_of_view_slot0,
        ))
    if camera_focus_loop_count is not None:
        changes.extend(patch_camera_focus_loop_count(overlays, camera_focus_loop_count))
    if mvl_camera_lead_from_player_velocity:
        changes.extend(patch_mvl_camera_lead_from_player_velocity(
            overlays,
            right_lead=mvl_camera_right_lead,
            left_lead=mvl_camera_left_lead,
            neutral_lead=mvl_camera_neutral_lead,
            velocity_threshold=mvl_camera_velocity_threshold,
            camera_step=mvl_camera_step,
        ))
    if stage_set_zoom_camera_player_id is not None:
        changes.extend(patch_stage_set_zoom_camera_player_id(overlays, stage_set_zoom_camera_player_id))
    if stagefx_display_player_id is not None:
        changes.extend(patch_stagefx_display_player_id(overlays, stagefx_display_player_id))
    if stage_layout_inventory_display_player_id is not None:
        changes.extend(patch_stage_layout_inventory_display_player_id(
            overlays,
            stage_layout_inventory_display_player_id,
            mode=stage_layout_inventory_display_mode,
        ))
    if vs_results_display_player_id is not None:
        changes.extend(patch_vs_results_display_player_id(overlays, vs_results_display_player_id))
    if player_signal_locked_noop:
        changes.extend(patch_player_signal_locked_noop(overlays))
    if player_freeze_stage_noop:
        changes.extend(patch_player_freeze_stage_noop(overlays))
    if player_stage_lock_vsmode_noop:
        changes.extend(patch_player_stage_lock_vsmode_noop(overlays))

    rom.arm9 = arm9.save(compress=True)
    save_overlays(rom, overlays)
    return changes


NOP = 0xE1A00000


def patch_fake_opponent(
    rom: NintendoDSRom,
    symbols: dict[str, int],
    *,
    force_confirm_load: bool,
    force_loadgame_progress: bool,
    mirror_packets: bool,
    fake_net_state: bool,
    fake_net_state_on_nickname: bool,
    force_transfer_result: int | None,
    clear_actor_category_mask: bool,
) -> list[str]:
    arm9 = rom.loadArm9()
    overlays = rom.loadArm9Overlays()
    changes: list[str] = []

    if mirror_packets:
        # Diagnostic lower-boundary adapter: make Net::getPacket(consoleID)
        # return the live local sendPacket for console 0 and 1. This does not
        # create real remote input yet, but it lets NSMB's own packet readers
        # see a second console packet instead of nullptr.
        addr = symbols["_ZN3Net9getPacketEt"]
        words = build_getpacket_mirror_stub(
            addr,
            send_packet_addr=symbols["_ZN3Net10sendPacketE"],
            fake_state=fake_net_state,
        )
        old = patch_arm9_words(arm9, addr, words)
        changes.append(
            f"Net::getPacket mirror local packet @ 0x{addr:08X}: "
            f"{old.hex()} -> {words_hex(words)}"
        )

    if force_transfer_result is not None:
        # Diagnostic only. This tells whether VSConnect/NetCore is failing on
        # transferPacket's status bits before we spend more time fabricating the
        # lower MP packet stream. 0x08 is the normal "all peer action packets
        # observed" bit in transferPacket().
        addr = symbols["_ZN3Net4Core14transferPacketENS_12PacketActionE"]
        words = [
            encode_mov_imm(0, force_transfer_result),
            BX_LR,
        ]
        old = patch_arm9_words(arm9, addr, words)
        changes.append(
            f"Net::Core::transferPacket forced result 0x{force_transfer_result:X} @ 0x{addr:08X}: "
            f"{old.hex()} -> {words_hex(words)}"
        )

    # Return a small static fake NicknameInfo-like buffer in overlay code RAM.
    # updateSearchSM only needs a non-null pointer, then reads byte 1 as the
    # nickname length and bytes from +2 for display text. Use length 0 to avoid
    # copying arbitrary memory while still advancing to confirmSM.
    addr = symbols["_ZN14VSConnectScene19getOpponentNicknameEv"]
    words = build_fake_nickname_stub(addr, fake_net_state=fake_net_state_on_nickname)
    ov_id, old = patch_overlay_words(overlays, addr, words)
    changes.append(
        f"VSConnectScene::getOpponentNickname fake peer overlay{ov_id} @ 0x{addr:08X}: "
        f"{old.hex()} -> {words_hex(words)}"
    )

    if force_confirm_load:
        # In the fake-peer diagnostic route, confirmSM quickly decides the peer
        # has left because no real local-wireless/session state exists. Redirect
        # that playerLeftSME literal to loadGameSME to expose the next missing
        # prerequisite without editing the whole state machine.
        literal_addr = 0x021582B4
        target = symbols["_ZN14VSConnectScene10loadGameSME"]
        ov_id, old = patch_overlay_words(overlays, literal_addr, [target])
        changes.append(
            f"VSConnectScene confirm playerLeft literal -> loadGameSME overlay{ov_id} @ 0x{literal_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', target).hex()}"
        )

    if force_loadgame_progress:
        # updateLoadGameSM state 1 waits for a communication/session byte at
        # 0x02088800 to become 2. In the fake-peer route that never happens, so
        # bypass only this wait branch to expose the next real prerequisite.
        wait_branch_addr = 0x021578B0
        ov_id, old = patch_overlay_words(overlays, wait_branch_addr, [NOP])
        changes.append(
            f"VSConnectScene::updateLoadGameSM state1 wait branch NOP overlay{ov_id} @ 0x{wait_branch_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )
        wait_branch2_addr = 0x021578D0
        ov_id, old = patch_overlay_words(overlays, wait_branch2_addr, [NOP])
        changes.append(
            f"VSConnectScene::updateLoadGameSM state1 secondary wait branch NOP overlay{ov_id} @ 0x{wait_branch2_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )
        wait_branch3_addr = 0x02157998
        ov_id, old = patch_overlay_words(overlays, wait_branch3_addr, [NOP])
        changes.append(
            f"VSConnectScene::updateLoadGameSM state3 session wait branch NOP overlay{ov_id} @ 0x{wait_branch3_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )
        force_state4_addr = 0x021579C0
        force_state4_words = [
            encode_mov_imm(0, 5),
            encode_str_imm(0, 4, 0x16C),
        ]
        ov_id, old = patch_overlay_words(overlays, force_state4_addr, force_state4_words)
        changes.append(
            f"VSConnectScene::updateLoadGameSM state4 force state5 overlay{ov_id} @ 0x{force_state4_addr:08X}: "
            f"{old.hex()} -> {words_hex(force_state4_words)}"
        )
        wait_branch5_addr = 0x021579F8
        ov_id, old = patch_overlay_words(overlays, wait_branch5_addr, [NOP])
        changes.append(
            f"VSConnectScene::updateLoadGameSM state5 ready-bit wait branch NOP overlay{ov_id} @ 0x{wait_branch5_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )
        wait_branch6_addr = 0x02157A1C
        ov_id, old = patch_overlay_words(overlays, wait_branch6_addr, [NOP])
        changes.append(
            f"VSConnectScene::updateLoadGameSM state6 completion wait branch NOP overlay{ov_id} @ 0x{wait_branch6_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )
        vs_menu_wait_addr = 0x021551F8
        ov_id, old = patch_overlay_words(overlays, vs_menu_wait_addr, [NOP])
        changes.append(
            f"VSMenu post-load transfer wait branch NOP overlay{ov_id} @ 0x{vs_menu_wait_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )
        vs_stage_intro_wait_addr = 0x02152888
        ov_id, old = patch_overlay_words(overlays, vs_stage_intro_wait_addr, [NOP])
        changes.append(
            f"VSStageIntro ready wait branch NOP overlay{ov_id} @ 0x{vs_stage_intro_wait_addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', NOP).hex()}"
        )

    if clear_actor_category_mask:
        addr = 0x02152E64
        word = encode_mov_imm(1, 0)
        ov_id, old = patch_overlay_words(overlays, addr, [word])
        changes.append(
            f"loadMvsLFilesThread actor category mask value overlay{ov_id} @ 0x{addr:08X}: "
            f"{old.hex()} -> {struct.pack('<I', word).hex()}"
        )

    rom.arm9 = arm9.save(compress=True)
    save_overlays(rom, overlays)
    return changes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="roms/nsmb-us.nds")
    ap.add_argument("--symbols", default="tools/nsmb-mvl-rom/resources/symbols9.x")
    ap.add_argument("--out", required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_rng = sub.add_parser("rng-constant")
    p_rng.add_argument("--value", type=lambda x: int(x, 0), default=0x100)
    p_camera = sub.add_parser("stage-camera-player-id")
    p_camera.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_camera_state = sub.add_parser("stage-camera-state-player-id")
    p_camera_state.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    sub.add_parser("stage-camera-state-vertical-slot-zero")
    sub.add_parser("camera-fallback-slot-zero")
    sub.add_parser("camera-player1-out-of-view-slot0")
    p_camera_loop = sub.add_parser("camera-focus-loop-count")
    p_camera_loop.add_argument("--count", type=lambda x: int(x, 0), default=2)
    p_camera_lead = sub.add_parser("mvl-camera-lead-from-player-velocity")
    p_camera_lead.add_argument("--right-lead", type=lambda x: int(x, 0), default=0x00050000)
    p_camera_lead.add_argument("--left-lead", type=lambda x: int(x, 0), default=0x000B0000)
    p_camera_lead.add_argument("--neutral-lead", type=lambda x: int(x, 0), default=0x00080000)
    p_camera_lead.add_argument("--velocity-threshold", type=lambda x: int(x, 0), default=0x40)
    p_camera_lead.add_argument("--camera-step", type=lambda x: int(x, 0), default=0x6000)
    p_set_zoom_camera = sub.add_parser("stage-set-zoom-camera-player-id")
    p_set_zoom_camera.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_stagefx_display = sub.add_parser("stagefx-display-player-id")
    p_stagefx_display.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_stage_layout_inventory = sub.add_parser("stage-layout-inventory-display-player-id")
    p_stage_layout_inventory.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_stage_layout_inventory.add_argument("--mode", choices=("hud", "all-read"), default="hud")
    p_stage_layout_inventory_use = sub.add_parser("stage-layout-inventory-use-player-id")
    p_stage_layout_inventory_use.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_vs_results_display = sub.add_parser("vs-results-display-player-id")
    p_vs_results_display.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_overlay0_alias = sub.add_parser("overlay0-localplayer-literal-alias")
    p_overlay0_alias.add_argument("--alias-addr", type=lambda x: int(x, 0), default=0x020CA280)
    p_overlay0_alias.add_argument("--mode", choices=("layout", "actor-collision", "all", "all-no-inventory"), default="layout")
    p_overlay0_alias.add_argument("--literal-addrs", default="")
    p_stage_range_alias = sub.add_parser("stage-range-localplayer-literal-alias")
    p_stage_range_alias.add_argument("--alias-addr", type=lambda x: int(x, 0), default=0x020CA280)
    p_wifi_comm = sub.add_parser("wifi-communicating-consoles")
    p_wifi_comm.add_argument("--count", type=lambda x: int(x, 0), default=2)
    p_stage_object_activation = sub.add_parser("stage-object-activation-player-id")
    p_stage_object_activation.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_stage_object_wrap = sub.add_parser("stage-object-activation-wrap-width")
    p_stage_object_wrap.add_argument("--width", type=lambda x: int(x, 0), default=0x400)
    p_stage_object_force_wrap_x = sub.add_parser("stage-object-activation-force-wrap-x")
    p_stage_object_force_wrap_x.add_argument("--wrap-x", type=lambda x: int(x, 0), default=0x003FFFFF)
    sub.add_parser("player-render-model-visible")
    p_player_range_view = sub.add_parser("player-render-range-view-player-id")
    p_player_range_view.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_player_view_transit = sub.add_parser("player-view-transit-local-player-id")
    p_player_view_transit.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_player_vs_pipe = sub.add_parser("player-vs-pipe-local-player-id")
    p_player_vs_pipe.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_player_wrap = sub.add_parser("player-render-wrap-x-offset")
    p_player_wrap.add_argument("--offset", type=lambda x: int(x, 0), default=0x400000)
    p_player_r12 = sub.add_parser("player-render-r12-offset")
    p_player_r12.add_argument("--offset", type=lambda x: int(x, 0), default=-0x400000)
    sub.add_parser("player-signal-locked-noop")
    sub.add_parser("player-freeze-stage-noop")
    sub.add_parser("player-stage-lock-vsmode-noop")
    p_stage_layout_final_view = sub.add_parser("stage-layout-final-view-player-id")
    p_stage_layout_final_view.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_stage_layout_final_view.add_argument("--which", choices=("prepare", "render", "both"), default="both")
    p_stage_entity_skip_render = sub.add_parser("stage-entity-skip-render-player-id")
    p_stage_entity_skip_render.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_stage_entity_liquid = sub.add_parser("stage-entity-liquid-check-result")
    p_stage_entity_liquid.add_argument("--in-liquid", action="store_true")
    p_direct = sub.add_parser("direct-mvl-entry")
    p_direct.add_argument("--scene", type=lambda x: int(x, 0), default=0x0F)
    p_direct.add_argument("--stage", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--act", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--player-id", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--entrance", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--flag", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--unused1", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--control-options", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--unused2", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--challenge-mode", type=lambda x: int(x, 0), default=0)
    p_direct.add_argument("--rng-seed", type=lambda x: int(x, 0), default=0x100)
    p_direct.add_argument("--first-scene", type=lambda x: int(x, 0), default=6)
    p_direct.add_argument("--skip-direct-loadlevel", action="store_true")
    p_direct.add_argument("--force-ready-progress", action="store_true")
    p_direct.add_argument("--force-transfer-result", type=lambda x: int(x, 0))
    p_direct.add_argument("--clear-actor-category-mask", action="store_true")
    p_direct.add_argument("--force-scene-settings", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--call-load-mvsl-files", action="store_true")
    p_direct.add_argument("--call-load-mvsl-files-after", action="store_true")
    p_direct.add_argument("--stage-camera-player-id", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--stage-camera-state-player-id", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--camera-fallback-slot-zero", action="store_true")
    p_direct.add_argument("--camera-player1-out-of-view-slot0", action="store_true")
    p_direct.add_argument("--camera-focus-loop-count", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--mvl-camera-lead-from-player-velocity", action="store_true")
    p_direct.add_argument("--mvl-camera-right-lead", type=lambda x: int(x, 0), default=0x00050000)
    p_direct.add_argument("--mvl-camera-left-lead", type=lambda x: int(x, 0), default=0x000B0000)
    p_direct.add_argument("--mvl-camera-neutral-lead", type=lambda x: int(x, 0), default=0x00080000)
    p_direct.add_argument("--mvl-camera-velocity-threshold", type=lambda x: int(x, 0), default=0x40)
    p_direct.add_argument("--mvl-camera-step", type=lambda x: int(x, 0), default=0x6000)
    p_direct.add_argument("--stage-set-zoom-camera-player-id", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--stagefx-display-player-id", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--stage-layout-inventory-display-player-id", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--stage-layout-inventory-display-mode", choices=("hud", "all-read"), default="hud")
    p_direct.add_argument("--vs-results-display-player-id", type=lambda x: int(x, 0), default=None)
    p_direct.add_argument("--player-signal-locked-noop", action="store_true")
    p_direct.add_argument("--player-freeze-stage-noop", action="store_true")
    p_direct.add_argument("--player-stage-lock-vsmode-noop", action="store_true")
    p_fake = sub.add_parser("fake-opponent")
    p_fake.add_argument("--force-confirm-load", action="store_true")
    p_fake.add_argument("--force-loadgame-progress", action="store_true")
    p_fake.add_argument("--mirror-packets", action="store_true")
    p_fake.add_argument("--fake-net-state", action="store_true")
    p_fake.add_argument("--fake-net-state-on-nickname", action="store_true")
    p_fake.add_argument("--force-transfer-result", type=lambda x: int(x, 0))
    p_fake.add_argument("--clear-actor-category-mask", action="store_true")
    args = ap.parse_args()

    symbols = load_symbols(Path(args.symbols))
    rom = NintendoDSRom.fromFile(args.rom)

    if args.cmd == "rng-constant":
        changes = patch_rng_constant(rom, symbols, args.value)
    elif args.cmd == "stage-camera-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_camera_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-camera-state-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_camera_state_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-camera-state-vertical-slot-zero":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_camera_state_vertical_slot_zero(overlays)
        save_overlays(rom, overlays)
    elif args.cmd == "camera-fallback-slot-zero":
        overlays = rom.loadArm9Overlays()
        changes = patch_is_out_of_view_vertical_camera_fallback(overlays)
        save_overlays(rom, overlays)
    elif args.cmd == "camera-player1-out-of-view-slot0":
        overlays = rom.loadArm9Overlays()
        changes = patch_is_out_of_view_vertical_camera_fallback(overlays, player1_slot0=True)
        save_overlays(rom, overlays)
    elif args.cmd == "camera-focus-loop-count":
        overlays = rom.loadArm9Overlays()
        changes = patch_camera_focus_loop_count(overlays, args.count)
        save_overlays(rom, overlays)
    elif args.cmd == "mvl-camera-lead-from-player-velocity":
        overlays = rom.loadArm9Overlays()
        changes = patch_mvl_camera_lead_from_player_velocity(
            overlays,
            right_lead=args.right_lead,
            left_lead=args.left_lead,
            neutral_lead=args.neutral_lead,
            velocity_threshold=args.velocity_threshold,
            camera_step=args.camera_step,
        )
        save_overlays(rom, overlays)
    elif args.cmd == "stage-set-zoom-camera-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_set_zoom_camera_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "stagefx-display-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stagefx_display_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-layout-inventory-display-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_layout_inventory_display_player_id(overlays, args.player_id, mode=args.mode)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-layout-inventory-use-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_layout_inventory_use_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "vs-results-display-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_vs_results_display_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "overlay0-localplayer-literal-alias":
        overlays = rom.loadArm9Overlays()
        literal_addrs = parse_int_list(args.literal_addrs) if args.literal_addrs else None
        changes = patch_overlay0_localplayer_literal_alias(overlays, args.alias_addr, args.mode, literal_addrs)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-range-localplayer-literal-alias":
        arm9 = rom.loadArm9()
        changes = patch_stage_range_localplayer_literal_alias(arm9, args.alias_addr)
        rom.arm9 = arm9.save(compress=True)
    elif args.cmd == "wifi-communicating-consoles":
        arm9 = rom.loadArm9()
        changes = patch_wifi_communicating_consoles(arm9, args.count)
        rom.arm9 = arm9.save(compress=True)
    elif args.cmd == "stage-object-activation-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_object_activation_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-object-activation-wrap-width":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_object_activation_wrap_width(overlays, args.width)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-object-activation-force-wrap-x":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_object_activation_force_wrap_x(overlays, args.wrap_x)
        save_overlays(rom, overlays)
    elif args.cmd == "player-render-model-visible":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_render_model_visible(overlays)
        save_overlays(rom, overlays)
    elif args.cmd == "player-render-range-view-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_render_range_view_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "player-view-transit-local-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_view_transit_local_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "player-vs-pipe-local-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_vs_pipe_local_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "player-render-wrap-x-offset":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_render_wrap_x_offset(overlays, args.offset)
        save_overlays(rom, overlays)
    elif args.cmd == "player-render-r12-offset":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_render_r12_offset(overlays, args.offset)
        save_overlays(rom, overlays)
    elif args.cmd == "player-signal-locked-noop":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_signal_locked_noop(overlays)
        save_overlays(rom, overlays)
    elif args.cmd == "player-freeze-stage-noop":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_freeze_stage_noop(overlays)
        save_overlays(rom, overlays)
    elif args.cmd == "player-stage-lock-vsmode-noop":
        overlays = rom.loadArm9Overlays()
        changes = patch_player_stage_lock_vsmode_noop(overlays)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-layout-final-view-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_layout_final_view_player_id(overlays, args.player_id, args.which)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-entity-skip-render-player-id":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_entity_skip_render_player_id(overlays, args.player_id)
        save_overlays(rom, overlays)
    elif args.cmd == "stage-entity-liquid-check-result":
        overlays = rom.loadArm9Overlays()
        changes = patch_stage_entity_liquid_check_result(overlays, in_liquid=args.in_liquid)
        save_overlays(rom, overlays)
    elif args.cmd == "direct-mvl-entry":
        changes = patch_direct_mvl_entry(
            rom,
            symbols,
            scene=args.scene,
            stage=args.stage,
            act=args.act,
            player_id=args.player_id,
            entrance=args.entrance,
            flag=args.flag,
            unused1=args.unused1,
            control_options=args.control_options,
            unused2=args.unused2,
            challenge_mode=args.challenge_mode,
            rng_seed=args.rng_seed,
            first_scene=args.first_scene,
            skip_direct_loadlevel=args.skip_direct_loadlevel,
            force_ready_progress=args.force_ready_progress,
            force_transfer_result=args.force_transfer_result,
            clear_actor_category_mask=args.clear_actor_category_mask,
            force_scene_settings=args.force_scene_settings,
            call_load_mvl_files=args.call_load_mvsl_files,
            call_load_mvl_files_after=args.call_load_mvsl_files_after,
            stage_camera_player_id=args.stage_camera_player_id,
            stage_camera_state_player_id=args.stage_camera_state_player_id,
            camera_fallback_slot_zero=args.camera_fallback_slot_zero,
            camera_player1_out_of_view_slot0=args.camera_player1_out_of_view_slot0,
            camera_focus_loop_count=args.camera_focus_loop_count,
            mvl_camera_lead_from_player_velocity=args.mvl_camera_lead_from_player_velocity,
            mvl_camera_right_lead=args.mvl_camera_right_lead,
            mvl_camera_left_lead=args.mvl_camera_left_lead,
            mvl_camera_neutral_lead=args.mvl_camera_neutral_lead,
            mvl_camera_velocity_threshold=args.mvl_camera_velocity_threshold,
            mvl_camera_step=args.mvl_camera_step,
            stage_set_zoom_camera_player_id=args.stage_set_zoom_camera_player_id,
            stagefx_display_player_id=args.stagefx_display_player_id,
            stage_layout_inventory_display_player_id=args.stage_layout_inventory_display_player_id,
            stage_layout_inventory_display_mode=args.stage_layout_inventory_display_mode,
            vs_results_display_player_id=args.vs_results_display_player_id,
            player_signal_locked_noop=args.player_signal_locked_noop,
            player_freeze_stage_noop=args.player_freeze_stage_noop,
            player_stage_lock_vsmode_noop=args.player_stage_lock_vsmode_noop,
        )
    elif args.cmd == "fake-opponent":
        changes = patch_fake_opponent(
            rom,
            symbols,
            force_confirm_load=args.force_confirm_load,
            force_loadgame_progress=args.force_loadgame_progress,
            mirror_packets=args.mirror_packets,
            fake_net_state=args.fake_net_state,
            fake_net_state_on_nickname=args.fake_net_state_on_nickname,
            force_transfer_result=args.force_transfer_result,
            clear_actor_category_mask=args.clear_actor_category_mask,
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
