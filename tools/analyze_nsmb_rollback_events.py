#!/usr/bin/env python3
"""Compare low-frequency NSMB MvL gameplay events between both peers.

The analyzer intentionally checks event timelines rather than only the final
state.  It supports player hitbox contact, observable block tile transitions,
and the eight-coin reward transition used by the rollback stress routes.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


ROLES = ("host", "client")


@dataclass(frozen=True)
class TileState:
    tile_id: int
    behavior: str
    block: bool
    storage_contents: int


@dataclass(frozen=True)
class BlockEvent:
    frame: int
    previous_frame: int
    pixel_x: int
    pixel_y: int
    before: TileState
    after: TileState


@dataclass(frozen=True)
class CoinEvent:
    frame: int
    previous_frame: int
    player: int
    before: int
    after: int


@dataclass(frozen=True)
class ContactEvent:
    frame: int
    player0_vel_x: int
    player0_vel_y: int
    player1_vel_x: int
    player1_vel_y: int
    player0_collision_flag: int
    player1_collision_flag: int
    player0_sub_action_flag: int
    player1_sub_action_flag: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_dir", type=Path)
    parser.add_argument(
        "--checks",
        nargs="+",
        choices=("contact", "block", "eight-coin"),
        required=True,
    )
    parser.add_argument("--coin-player", type=int, choices=(0, 1), default=0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-fail", action="store_true")
    return parser.parse_args()


def load_ai_rows(root: Path, role: str) -> Iterable[dict[str, Any]]:
    path = root / role / "ai-playlog.jsonl"
    if not path.is_file():
        raise FileNotFoundError(f"AI play log was not found: {path}")
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            try:
                yield json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid JSON at {path}:{line_number}: {error}") from error


def player_bounds(player: dict[str, Any]) -> tuple[int, int, int, int] | None:
    hitbox = player.get("hitbox", {})
    if not player.get("found") or not hitbox.get("found"):
        return None
    position = player["pos"]
    return (
        int(position["x"]) + int(hitbox["minOffsetX"]),
        int(position["x"]) + int(hitbox["maxOffsetX"]),
        int(position["y"]) + int(hitbox["minOffsetY"]),
        int(position["y"]) + int(hitbox["maxOffsetY"]),
    )


def hitboxes_overlap(left: tuple[int, int, int, int], right: tuple[int, int, int, int]) -> bool:
    return (
        max(left[0], right[0]) <= min(left[1], right[1])
        and max(left[2], right[2]) <= min(left[3], right[3])
    )


def contact_events(root: Path, role: str) -> list[ContactEvent]:
    result: set[ContactEvent] = set()
    for row in load_ai_rows(root, role):
        players = row.get("players", [])
        if len(players) < 2:
            continue
        bounds = (player_bounds(players[0]), player_bounds(players[1]))
        if bounds[0] is not None and bounds[1] is not None and hitboxes_overlap(bounds[0], bounds[1]):
            result.add(
                ContactEvent(
                    frame=int(row["frame"]),
                    player0_vel_x=int(players[0]["vel"]["x"]),
                    player0_vel_y=int(players[0]["vel"]["y"]),
                    player1_vel_x=int(players[1]["vel"]["x"]),
                    player1_vel_y=int(players[1]["vel"]["y"]),
                    player0_collision_flag=int(players[0]["collisionFlag"]),
                    player1_collision_flag=int(players[1]["collisionFlag"]),
                    player0_sub_action_flag=int(players[0]["subActionFlag"]),
                    player1_sub_action_flag=int(players[1]["subActionFlag"]),
                )
            )
    return sorted(result, key=lambda event: event.frame)


def tile_state(cell: dict[str, Any]) -> TileState:
    block = cell.get("block", {})
    return TileState(
        tile_id=int(cell.get("tileId", 0)),
        behavior=str(cell.get("behavior", "")),
        block=bool(block.get("any", 0)),
        storage_contents=int(block.get("storageContents", 0)),
    )


def block_events(root: Path, role: str) -> list[BlockEvent]:
    last_seen: dict[tuple[int, int], tuple[int, TileState]] = {}
    events: set[BlockEvent] = set()
    for row in load_ai_rows(root, role):
        frame = int(row["frame"])
        for player in row.get("players", []):
            if not player.get("found"):
                continue
            cells = player.get("tileProbe", {}).get("grid", {}).get("cells", [])
            for cell in cells:
                if not cell.get("found"):
                    continue
                key = (int(cell["pixelX"]), int(cell["pixelY"]))
                current = tile_state(cell)
                previous = last_seen.get(key)
                if previous is not None and previous[1] != current and (previous[1].block or current.block):
                    events.add(
                        BlockEvent(
                            frame=frame,
                            previous_frame=previous[0],
                            pixel_x=key[0],
                            pixel_y=key[1],
                            before=previous[1],
                            after=current,
                        )
                    )
                last_seen[key] = (frame, current)
    return sorted(events, key=lambda event: (event.frame, event.pixel_y, event.pixel_x))


def parse_integer(value: str) -> int:
    return int(value, 0)


def eight_coin_events(root: Path, role: str, player: int) -> list[CoinEvent]:
    path = root / role / f"{role}.game-state.csv"
    if not path.is_file():
        raise FileNotFoundError(f"game-state log was not found: {path}")
    column = f"player{player}Coins"
    previous: tuple[int, int] | None = None
    events: list[CoinEvent] = []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row.get("instance") != "0":
                continue
            frame = parse_integer(row["frame"])
            coins = parse_integer(row[column])
            if previous is not None and previous[1] == 7 and coins == 0:
                events.append(
                    CoinEvent(
                        frame=frame,
                        previous_frame=previous[0],
                        player=player,
                        before=previous[1],
                        after=coins,
                    )
                )
            previous = (frame, coins)
    return events


def serializable_events(events: list[Any]) -> list[Any]:
    return [asdict(event) if hasattr(event, "__dataclass_fields__") else event for event in events]


def main() -> int:
    args = parse_args()
    root = args.log_dir.resolve()
    if not root.is_dir():
        raise FileNotFoundError(f"log directory was not found: {root}")

    results: dict[str, Any] = {}
    passed = True
    for check in args.checks:
        if check == "contact":
            by_role = {role: contact_events(root, role) for role in ROLES}
        elif check == "block":
            by_role = {role: block_events(root, role) for role in ROLES}
        else:
            by_role = {role: eight_coin_events(root, role, args.coin_player) for role in ROLES}

        events_match = by_role["host"] == by_role["client"]
        events_present = bool(by_role["host"])
        check_passed = events_match and events_present
        passed = passed and check_passed
        results[check] = {
            "eventsPresent": events_present,
            "peerTimelinesMatch": events_match,
            "pass": check_passed,
            "roles": {role: serializable_events(events) for role, events in by_role.items()},
        }
        print(
            f"{check}: host={len(by_role['host'])} client={len(by_role['client'])} "
            f"present={events_present} match={events_match} pass={check_passed}"
        )

    payload = {"logDir": str(root), "checks": results, "pass": passed}
    output = args.output or root / "rollback-event-analysis.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"rollback event analysis written: {output}")
    return 0 if passed or args.no_fail else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError, KeyError) as error:
        print(f"rollback event analysis failed: {error}", file=sys.stderr)
        raise SystemExit(2)
