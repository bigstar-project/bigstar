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


def encode_ldr_reg_lsl(rd: int, rn: int, rm: int, shift: int) -> int:
    if shift < 0 or shift > 31:
        raise ValueError(f"LDR register shift {shift} is out of range")
    return 0xE7900000 | (rn << 16) | (rd << 12) | (shift << 7) | rm


def encode_cmp_imm(rn: int, imm: int) -> int:
    return 0xE3500000 | (rn << 16) | encode_arm_imm12(imm)


def encode_mov_reg(rd: int, rm: int) -> int:
    return 0xE1A00000 | (rd << 12) | rm


def encode_add_reg(rd: int, rn: int, rm: int) -> int:
    return 0xE0800000 | (rn << 16) | (rd << 12) | rm


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
    ap.add_argument("--symbols", default="external/NSMB-Code-Reference/symbols9.x")
    ap.add_argument("--out", required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_rng = sub.add_parser("rng-constant")
    p_rng.add_argument("--value", type=lambda x: int(x, 0), default=0x100)
    p_camera = sub.add_parser("stage-camera-player-id")
    p_camera.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    p_camera_state = sub.add_parser("stage-camera-state-player-id")
    p_camera_state.add_argument("--player-id", type=lambda x: int(x, 0), required=True)
    sub.add_parser("camera-fallback-slot-zero")
    sub.add_parser("camera-player1-out-of-view-slot0")
    p_camera_loop = sub.add_parser("camera-focus-loop-count")
    p_camera_loop.add_argument("--count", type=lambda x: int(x, 0), default=2)
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
