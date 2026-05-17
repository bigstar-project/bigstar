#!/usr/bin/env python3
"""Compare extracted NSMB packet streams from a host/client LAN run.

The expected Mario vs Luigi flow is:

* client sends one 52-byte packet as a reply (`type=65538`, slot 0)
* host receives that reply and later sends a command (`type=1`) containing
  slot 0 = host packet and slot 1 = client packet
* client receives the host command with the same two slots

This helper verifies that relationship on packet CSVs produced by
`nsmb_localmp_packet_extract.py`.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path


def load_packets(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def index(rows: list[dict[str, str]], event: str, localmp_type: str, slot: str | None = None) -> dict[str, list[dict[str, str]]]:
    out: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row["event"] != event or row["localmp_type"] != localmp_type:
            continue
        if slot is not None and row["packet_slot"] != slot:
            continue
        out[row["tick"]].append(row)
    return out


def first_hex(rows: dict[str, list[dict[str, str]]], tick: str) -> str | None:
    items = rows.get(tick)
    if not items:
        return None
    return items[0]["packet_hex"]


def compare_streams(host_rows: list[dict[str, str]], client_rows: list[dict[str, str]], action: str) -> tuple[int, list[str]]:
    host_send_slot0 = index(host_rows, "send", "1", "0")
    host_send_slot1 = index(host_rows, "send", "1", "1")
    host_replies = index(host_rows, "replies", "65538", "0")
    client_send = index(client_rows, "send", "65538", "0")
    client_recv_slot0 = index(client_rows, "recv", "1", "0")
    client_recv_slot1 = index(client_rows, "recv", "1", "1")

    ticks = sorted(
        tick
        for tick, rows in host_send_slot0.items()
        if rows and rows[0]["action"] == action
    )
    checked = 0
    errors: list[str] = []

    for tick in ticks:
        host0 = first_hex(host_send_slot0, tick)
        host1 = first_hex(host_send_slot1, tick)
        reply = first_hex(host_replies, tick)
        client0 = first_hex(client_send, tick)
        recv0 = first_hex(client_recv_slot0, tick)
        recv1 = first_hex(client_recv_slot1, tick)

        # Some early/late ticks may be partially observed depending on when the
        # trace starts/stops. Skip incomplete ticks but report true mismatches.
        if not all([host0, host1, recv0, recv1]):
            continue

        checked += 1
        if host0 != recv0:
            errors.append(f"{tick}: host send slot0 != client recv slot0")
        if host1 != recv1:
            errors.append(f"{tick}: host send slot1 != client recv slot1")
        if reply and client0 and reply != client0:
            errors.append(f"{tick}: host received reply != client sent reply")
        if reply and host1 != reply:
            errors.append(f"{tick}: host command slot1 != received client reply")

    return checked, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host_packets", type=Path)
    parser.add_argument("client_packets", type=Path)
    parser.add_argument("--action", default="0x03", help="Packet action to compare. MvL gameplay defaults to 0x03.")
    parser.add_argument("--max-errors", type=int, default=20)
    args = parser.parse_args()

    checked, errors = compare_streams(load_packets(args.host_packets), load_packets(args.client_packets), args.action)
    print(f"checked_ticks={checked} action={args.action} errors={len(errors)}")
    for error in errors[: args.max_errors]:
        print(error)
    return 1 if errors or checked == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
