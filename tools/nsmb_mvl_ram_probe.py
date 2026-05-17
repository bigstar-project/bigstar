#!/usr/bin/env python3
"""Probe NSMB Mario vs Luigi RAM dumps for random-event candidates.

The current PoC writes raw MainRAM snapshots from melonDS. This helper keeps the
first pass deliberately heuristic: it looks for the known MvsL Big Star actor ID
(210 / 0x00d2) and for compact object-like records that differ between runs.
"""

from __future__ import annotations

import argparse
import re
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

A2DJ_FUNCTIONS = {
    # US Net function symbols shifted by -0x154. Verified against decrypted
    # runtime ARM9 code in MainRAM dumps.
    "Net::getRandom12()": 0x0200E550,
    "Net::getRandom()": 0x0200E5A0,
    "Net::syncRandomFull()": 0x0200E5E8,
    "Net::syncRandomFast()": 0x0200E5F4,
    "Net::Core::readUserInfo(MBUserInfo*)": 0x0200F320,
    "Net::Core::transferPacket(Net::PacketAction)": 0x0200F98C,
    "Net::update()": 0x0200FF40,
    "Net::updatePacket()": 0x020101E4,
    "Net::onPacketPollingDefault()": 0x02010810,
    "Net::onRenderSignalStrengthDefault()": 0x02010828,
    "Net::setDefaultHandlers()": 0x02010930,
    "Net::Core::shareRandomSeed()": 0x02010F04,
}

A2DJ_PROCESS_LISTS = {
    # US Game process globals shifted by -0x9c0 for A2DJ in the current MvsL dumps.
    "execute": 0x0208F158,
    "render": 0x0208F178,
    "create": 0x0208F188,
}

A2DJ_OBJECT_NAMES = {
    0x0003: "StageScene",
    0x0012: "StageFX",
    0x0015: "Player",
    0x001F: "Item",
    0x0021: "Actor33",
    0x0022: "Actor34",
    0x0053: "Goomba",
    0x005E: "KoopaTroopa",
    0x010B: "MvsLObject267",
    0x010C: "MvsLObject268/VSBattleStarCandidate",
    0x012F: "StageActorManager",
    0x0130: "StageController",
    0x013C: "StageCamera",
    0x0145: "StageLayout",
}


def u16(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 2], "little")


def u32(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 4], "little")


def read_u16_addr(data: bytes, addr: int) -> int | None:
    off = addr - MAIN_RAM_BASE
    if off < 0 or off + 2 > len(data):
        return None
    return u16(data, off)


def read_u32_addr(data: bytes, addr: int) -> int | None:
    off = addr - MAIN_RAM_BASE
    if off < 0 or off + 4 > len(data):
        return None
    return u32(data, off)


def ptr_in_mainram(data: bytes, addr: int | None) -> bool:
    if not addr:
        return False
    off = addr - MAIN_RAM_BASE
    return 0 <= off < len(data)


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
    parser.add_argument("--a2dj-functions", action="store_true")
    parser.add_argument("--a2dj-rng-timeline", action="store_true")
    parser.add_argument("--a2dj-process-lists", action="store_true")
    parser.add_argument("--a2dj-object-scan", action="store_true")
    parser.add_argument("--a2dj-object-dump", action="store_true")
    parser.add_argument("--object-guid", default="")
    parser.add_argument("--object-id", default="")
    parser.add_argument("--object-settings", default="")
    parser.add_argument("--object-size", default="0x120")
    parser.add_argument("--rng-timeline-only", action="store_true")
    parser.add_argument("--find-arm-bl-to", default="")
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


def print_functions(label: str, data: bytes, functions: dict[str, int], function_label: str) -> None:
    print(f"== {function_label} function probe {label} ==")
    for name, addr in functions.items():
        off = addr - MAIN_RAM_BASE
        if off < 0 or off + 8 > len(data):
            print(f"  {name} addr=0x{addr:08x} out_of_dump")
            continue
        print(
            f"  {name} addr=0x{addr:08x} "
            f"head32=0x{u32(data, off):08x},0x{u32(data, off + 4):08x}"
        )


def print_arm_bl_hits(label: str, data: bytes, target: int) -> None:
    print(f"== ARM BL hits {label} target=0x{target:08x} ==")
    hits: list[int] = []
    for off in range(0, len(data) - 4, 4):
        word = u32(data, off)
        if (word & 0x0F000000) != 0x0B000000:
            continue
        imm = word & 0x00FFFFFF
        if imm & 0x00800000:
            imm -= 0x01000000
        dest = (MAIN_RAM_BASE + off + 8 + (imm << 2)) & 0xFFFFFFFF
        if dest == target:
            hits.append(MAIN_RAM_BASE + off)
    print(f"count={len(hits)}")
    for addr in hits[:128]:
        print(f"  0x{addr:08x}")
    if len(hits) > 128:
        print(f"  ... {len(hits) - 128} more")


def frame_from_label(label: str) -> int:
    match = re.search(r"frame(\d+)", label)
    if not match:
        return -1
    return int(match.group(1))


def print_a2dj_rng_timeline(loaded: list[tuple[str, bytes]]) -> None:
    rows = []
    for label, data in loaded:
        count_off = A2DJ_SYMBOLS["Net::randomCallCount"] - MAIN_RAM_BASE
        value_off = A2DJ_SYMBOLS["Net::random.value"] - MAIN_RAM_BASE
        branch_off = A2DJ_SYMBOLS["Net::randomBranchAddress"] - MAIN_RAM_BASE
        if max(count_off, value_off + 3, branch_off + 3) >= len(data):
            continue
        rows.append(
            (
                frame_from_label(label),
                label,
                data[count_off],
                u32(data, value_off),
                u32(data, branch_off),
            )
        )

    rows.sort(key=lambda row: (row[0], row[1]))
    print("== A2DJ Net RNG timeline ==")
    last: tuple[int, int, int] | None = None
    for frame, label, count, value, branch in rows:
        current = (count, value, branch)
        if current == last:
            continue
        frame_text = f"{frame:06d}" if frame >= 0 else "unknown"
        print(
            f"  frame={frame_text} count=0x{count:02x} "
            f"value=0x{value:08x} branch=0x{branch:08x} file={label}"
        )
        last = current


def a2dj_process_entries(data: bytes, list_addr: int) -> list[dict[str, int]]:
    entries: list[dict[str, int]] = []
    node = read_u32_addr(data, list_addr)
    seen: set[int] = set()

    for _ in range(512):
        if not node or node in seen:
            break
        seen.add(node)

        base = read_u32_addr(data, node + 8)
        if not base:
            break

        entries.append(
            {
                "node": node,
                "base": base,
                "vtable": read_u32_addr(data, base) or 0,
                "guid": read_u32_addr(data, base + 4) or 0,
                "settings": read_u32_addr(data, base + 8) or 0,
                "object_id": read_u16_addr(data, base + 0x0C) or 0,
                "state_type": read_u16_addr(data, base + 0x0E) or 0,
                "flags": read_u32_addr(data, base + 0x10) or 0,
                # Base is 0x5c bytes including the vtable pointer. Actor-derived
                # objects place Vec3 position immediately after Base.
                "pos_x": read_u32_addr(data, base + 0x5C) or 0,
                "pos_y": read_u32_addr(data, base + 0x60) or 0,
                "pos_z": read_u32_addr(data, base + 0x64) or 0,
                "priority": read_u32_addr(data, node + 0x0C) or 0,
            }
        )

        node = read_u32_addr(data, node + 4)

    return entries


def read_base_entry(data: bytes, base: int, node: int = 0, depth: int = 0) -> dict[str, int]:
    return {
        "node": node,
        "depth": depth,
        "base": base,
        "vtable": read_u32_addr(data, base) or 0,
        "guid": read_u32_addr(data, base + 4) or 0,
        "settings": read_u32_addr(data, base + 8) or 0,
        "object_id": read_u16_addr(data, base + 0x0C) or 0,
        "state_type": read_u16_addr(data, base + 0x0E) or 0,
        "flags": read_u32_addr(data, base + 0x10) or 0,
        "pos_x": read_u32_addr(data, base + 0x5C) or 0,
        "pos_y": read_u32_addr(data, base + 0x60) or 0,
        "pos_z": read_u32_addr(data, base + 0x64) or 0,
    }


def a2dj_object_scan_entries(data: bytes) -> list[dict[str, int]]:
    entries: list[dict[str, int]] = []
    for off in range(0, len(data) - 0x120, 4):
        base = MAIN_RAM_BASE + off
        vtable = u32(data, off)
        guid = u32(data, off + 4)
        object_id = u16(data, off + 0x0C)
        state_type = u16(data, off + 0x0E)
        flags = u32(data, off + 0x10)
        if not (MAIN_RAM_BASE <= vtable < MAIN_RAM_BASE + len(data)):
            continue
        if not (0 < guid < 0x10000):
            continue
        if not (0 < object_id < 0x400):
            continue
        if state_type not in (1, 2, 3):
            continue
        if flags >= 0x01000000:
            continue
        entries.append(read_base_entry(data, base))
    return entries


def process_semantic_key(entry: dict[str, int]) -> tuple[int, ...]:
    return (
        entry["vtable"],
        entry["settings"],
        entry["object_id"],
        entry["state_type"],
        entry["flags"],
        entry["pos_x"],
        entry["pos_y"],
        entry["pos_z"],
        entry["priority"],
    )


def print_a2dj_process_lists(loaded: list[tuple[str, bytes]]) -> None:
    print("== A2DJ process lists ==")
    per_dump: list[tuple[str, dict[str, list[dict[str, int]]]]] = []
    for label, data in loaded:
        lists = {name: a2dj_process_entries(data, addr) for name, addr in A2DJ_PROCESS_LISTS.items()}
        per_dump.append((label, lists))
        print(f"-- {label} --")
        for name, entries in lists.items():
            print(f"  {name}: count={len(entries)}")
            for index, entry in enumerate(entries[:16]):
                print(
                    "    "
                    f"{index:02d} base=0x{entry['base']:08x} guid=0x{entry['guid']:08x} "
                    f"id=0x{entry['object_id']:04x} stateType=0x{entry['state_type']:04x} "
                    f"settings=0x{entry['settings']:08x} pos="
                    f"0x{entry['pos_x']:08x},0x{entry['pos_y']:08x},0x{entry['pos_z']:08x}"
                )
            if len(entries) > 16:
                print(f"    ... {len(entries) - 16} more")

    if len(per_dump) == 2:
        (label_a, lists_a), (label_b, lists_b) = per_dump
        print(f"== A2DJ process semantic diff {label_a} vs {label_b} ==")
        for name in A2DJ_PROCESS_LISTS:
            ordered_a = [process_semantic_key(entry) for entry in lists_a[name]]
            ordered_b = [process_semantic_key(entry) for entry in lists_b[name]]
            sorted_a = sorted(ordered_a)
            sorted_b = sorted(ordered_b)
            print(
                f"  {name}: same_order={ordered_a == ordered_b} "
                f"same_multiset={sorted_a == sorted_b}"
            )
            if sorted_a != sorted_b:
                only_a = [item for item in sorted_a if item not in sorted_b][:8]
                only_b = [item for item in sorted_b if item not in sorted_a][:8]
                print(f"    only_a={only_a}")
                print(f"    only_b={only_b}")


def print_entry_summary(prefix: str, entry: dict[str, int]) -> None:
    name = A2DJ_OBJECT_NAMES.get(entry["object_id"], "unknown")
    print(
        f"{prefix}base=0x{entry['base']:08x} guid=0x{entry['guid']:08x} "
        f"id=0x{entry['object_id']:04x}({name}) state=0x{entry['state_type']:04x} "
        f"flags=0x{entry['flags']:08x} settings=0x{entry['settings']:08x} "
        f"pos=0x{entry['pos_x']:08x},0x{entry['pos_y']:08x},0x{entry['pos_z']:08x} "
        f"vtable=0x{entry['vtable']:08x}"
    )


def print_a2dj_object_scan(loaded: list[tuple[str, bytes]]) -> None:
    print("== A2DJ object scan ==")
    per_dump: list[tuple[str, list[dict[str, int]]]] = []
    for label, data in loaded:
        entries = a2dj_object_scan_entries(data)
        per_dump.append((label, entries))
        print(f"-- {label} --")
        print(f"  count={len(entries)}")
        for index, entry in enumerate(entries[:128]):
            print_entry_summary(f"  {index:03d} ", entry)
        if len(entries) > 128:
            print(f"  ... {len(entries) - 128} more")

    if len(per_dump) == 2:
        (label_a, entries_a), (label_b, entries_b) = per_dump
        by_guid_a = {entry["guid"]: entry for entry in entries_a if entry["guid"]}
        by_guid_b = {entry["guid"]: entry for entry in entries_b if entry["guid"]}
        print(f"== A2DJ object scan diff {label_a} vs {label_b} ==")
        for guid in sorted(set(by_guid_a) | set(by_guid_b)):
            a = by_guid_a.get(guid)
            b = by_guid_b.get(guid)
            if a and b:
                key_a = (
                    a["vtable"], a["settings"], a["object_id"], a["state_type"],
                    a["flags"], a["pos_x"], a["pos_y"], a["pos_z"],
                )
                key_b = (
                    b["vtable"], b["settings"], b["object_id"], b["state_type"],
                    b["flags"], b["pos_x"], b["pos_y"], b["pos_z"],
                )
                if key_a == key_b:
                    continue

            print(f"  guid=0x{guid:08x}")
            if a:
                print_entry_summary("    a: ", a)
            else:
                print("    a: <missing>")
            if b:
                print_entry_summary("    b: ", b)
            else:
                print("    b: <missing>")


def find_a2dj_object_for_dump(
    data: bytes,
    guid: int | None,
    object_id: int | None,
    settings: int | None,
) -> dict[str, int] | None:
    for entry in a2dj_object_scan_entries(data):
        if guid is not None and entry["guid"] != guid:
            continue
        if object_id is not None and entry["object_id"] != object_id:
            continue
        if settings is not None and entry["settings"] != settings:
            continue
        return entry
    return None


def object_dump_words(data: bytes, base: int, size: int) -> list[int]:
    off = base - MAIN_RAM_BASE
    if off < 0 or off >= len(data):
        return []
    end = min(off + size, len(data))
    end -= (end - off) % 4
    return [u32(data, pos) for pos in range(off, end, 4)]


def print_a2dj_object_dump(
    loaded: list[tuple[str, bytes]],
    guid_text: str,
    object_id_text: str,
    settings_text: str,
    size_text: str,
) -> None:
    guid = int(guid_text, 0) if guid_text else None
    object_id = int(object_id_text, 0) if object_id_text else None
    settings = int(settings_text, 0) if settings_text else None
    size = int(size_text, 0)

    print("== A2DJ object dump ==")
    print(
        "selector="
        f"guid={guid_text or '*'} object_id={object_id_text or '*'} "
        f"settings={settings_text or '*'} size=0x{size:x}"
    )

    previous_label = ""
    previous_entry: dict[str, int] | None = None
    previous_words: list[int] | None = None
    for label, data in loaded:
        entry = find_a2dj_object_for_dump(data, guid, object_id, settings)
        print(f"-- {label} --")
        if not entry:
            print("  <missing>")
            previous_label = label
            previous_entry = None
            previous_words = None
            continue

        print_entry_summary("  ", entry)
        words = object_dump_words(data, entry["base"], size)
        for index, word in enumerate(words):
            rel = index * 4
            print(f"    +0x{rel:03x}: 0x{word:08x}")

        if previous_words is not None and previous_entry is not None:
            print(f"  diff_vs={previous_label}")
            max_words = max(len(previous_words), len(words))
            changed = 0
            for index in range(max_words):
                prev = previous_words[index] if index < len(previous_words) else None
                cur = words[index] if index < len(words) else None
                if prev == cur:
                    continue
                rel = index * 4
                prev_text = "<missing>" if prev is None else f"0x{prev:08x}"
                cur_text = "<missing>" if cur is None else f"0x{cur:08x}"
                print(f"    +0x{rel:03x}: {prev_text} -> {cur_text}")
                changed += 1
            print(f"  changed_words={changed}")

        previous_label = label
        previous_entry = entry
        previous_words = words


def main() -> int:
    args = parse_args()
    scan_start = int(args.scan_start, 0)
    scan_end = int(args.scan_end, 0)
    find_bl_to = int(args.find_arm_bl_to, 0) if args.find_arm_bl_to else 0
    print_rom_info(args.rom)

    loaded: list[tuple[str, bytes]] = []
    dumps: list[Path] = []
    for dump in args.dumps:
        if any(ch in dump.as_posix() for ch in "*?[]"):
            matches = sorted(dump.parent.glob(dump.name))
            dumps.extend(matches)
        else:
            dumps.append(dump)

    for dump in dumps:
        data = dump.read_bytes()
        loaded.append((dump.as_posix(), data))
        if args.rng_timeline_only:
            continue
        print_dump_summary(dump.as_posix(), data, scan_start, scan_end)
        if args.us_symbols:
            print_symbols(dump.as_posix(), data, US_SYMBOLS, "US")
        if args.a2dj_symbols:
            print_symbols(dump.as_posix(), data, A2DJ_SYMBOLS, "A2DJ")
        if args.a2dj_functions:
            print_functions(dump.as_posix(), data, A2DJ_FUNCTIONS, "A2DJ")
        if find_bl_to:
            print_arm_bl_hits(dump.as_posix(), data, find_bl_to)

    if len(loaded) == 2 and not args.rng_timeline_only:
        print_diff(loaded[0][0], loaded[0][1], loaded[1][0], loaded[1][1], scan_start, scan_end)
    if args.a2dj_rng_timeline:
        print_a2dj_rng_timeline(loaded)
    if args.a2dj_process_lists:
        print_a2dj_process_lists(loaded)
    if args.a2dj_object_scan:
        print_a2dj_object_scan(loaded)
    if args.a2dj_object_dump:
        print_a2dj_object_dump(
            loaded,
            args.object_guid,
            args.object_id,
            args.object_settings,
            args.object_size,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
