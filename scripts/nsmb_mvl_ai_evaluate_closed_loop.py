#!/usr/bin/env python3
"""Evaluate closed-loop NSMB MvL AI runs from AI play logs or game-state CSVs."""

from __future__ import annotations

import argparse
import csv
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


BIG_STAR_CATEGORIES = ("big_star_actor", "big_star_candidate", "big_star_related")
ITEM_CATEGORIES = ("world_item", "neutral_item", "dropped_star_item", "item")
PROJECTILE_CATEGORIES = ("projectile", "player_fireball", "enemy_fireball")
HAZARD_CATEGORIES = ("moving_hazard", "hazard", "enemy_goomba", "enemy_koopa")


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str) and value != "":
        return int(value, 0)
    return default


def signed32(value: Any, default: int = 0) -> int:
    raw = num(value, default)
    raw &= 0xFFFFFFFF
    if raw & 0x80000000:
        raw -= 0x100000000
    return raw


def ratio(numerator: int, denominator: int) -> float:
    return float(numerator) / float(denominator) if denominator > 0 else 0.0


def iter_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def top_counts(counts: dict[str, int], limit: int = 8) -> list[dict[str, Any]]:
    return [
        {"value": value, "count": count}
        for value, count in sorted(counts.items(), key=lambda item: (-item[1], item[0]))[:limit]
    ]


def update_min_max(stats: dict[str, Any], value: int, frame: int) -> None:
    if stats.get("min") is None or value < stats["min"]:
        stats["min"] = value
        stats["minFrame"] = frame
    if stats.get("max") is None or value > stats["max"]:
        stats["max"] = value
        stats["maxFrame"] = frame


def held_for_record(record: dict[str, Any], player: int) -> int | None:
    inputs = record.get("inputs") or {}
    applied = inputs.get(f"appliedPlayer{player}") or {}
    if applied.get("valid"):
        return num(applied.get("held"))
    player_input = inputs.get(f"player{player}") or {}
    if "held" in player_input:
        return num(player_input.get("held"))
    return None


def player_record(record: dict[str, Any], player: int) -> dict[str, Any] | None:
    players = record.get("players") or []
    if 0 <= player < len(players):
        return players[player]
    return None


def player_pos(player: dict[str, Any]) -> tuple[int, int] | None:
    if not player.get("found"):
        return None
    pos = player.get("pos") or {}
    return num(pos.get("x")), num(pos.get("y"))


def nearest_from_visual_summary(record: dict[str, Any], player: int, categories: tuple[str, ...]) -> dict[str, Any] | None:
    visual_summary = record.get("visualSummary") or {}
    for entry in visual_summary.get("nearest") or []:
        if num(entry.get("player"), -1) != player:
            continue
        nearest_categories = entry.get("categories") or {}
        for category in categories:
            candidate = nearest_categories.get(category) or {}
            if candidate.get("found"):
                return {
                    "category": category,
                    "dx": num(candidate.get("dx")),
                    "dy": num(candidate.get("dy")),
                    "dist2": num(candidate.get("dist2")),
                }
    return None


def nearest_big_star_json(record: dict[str, Any], player: int) -> dict[str, Any] | None:
    visual = nearest_from_visual_summary(record, player, BIG_STAR_CATEGORIES)
    if visual is not None:
        return visual

    p = player_record(record, player)
    p_pos = player_pos(p or {})
    if p_pos is None:
        return None
    targets = record.get("targets") or {}
    for name in ("bigStarActor", "bigStarCandidate"):
        target = targets.get(name) or {}
        if not target.get("found"):
            continue
        pos = target.get("pos") or {}
        dx = num(pos.get("x")) - p_pos[0]
        dy = num(pos.get("y")) - p_pos[1]
        return {
            "category": "big_star_actor" if name == "bigStarActor" else "big_star_candidate",
            "dx": dx,
            "dy": dy,
            "dist2": dx * dx + dy * dy,
        }
    return None


def category_counts(record: dict[str, Any]) -> dict[str, int]:
    visual_summary = record.get("visualSummary") or {}
    counts = visual_summary.get("categoryCounts")
    if isinstance(counts, dict):
        return {str(k): num(v) for k, v in counts.items()}
    result: dict[str, int] = {}
    for obj in record.get("objects") or []:
        category = str(obj.get("category") or "unknown")
        result[category] = result.get(category, 0) + 1
    return result


def count_categories(counts: dict[str, int], categories: tuple[str, ...]) -> int:
    return sum(num(counts.get(category)) for category in categories)


def base_eval(path: Path, player: int, policy: str, source: str) -> dict[str, Any]:
    return {
        "input": str(path),
        "source": source,
        "policy": policy,
        "player": player,
        "opponent": player ^ 1,
        "rows": 0,
        "gameplayRows": 0,
        "frameStart": None,
        "frameEnd": None,
        "gameplayFrameStart": None,
        "gameplayFrameEnd": None,
        "playerFoundRows": 0,
        "opponentFoundRows": 0,
        "aliveRows": 0,
        "deadRows": 0,
        "deathTransitions": 0,
        "nearCameraBottomRows": 0,
        "belowCameraRows": 0,
        "holeAheadRows": 0,
        "nonzeroInputRows": 0,
        "inputRows": 0,
        "heldTop": [],
        "starPickups": 0,
        "coinChanges": 0,
        "powerupChanges": 0,
        "itemVisibleRows": 0,
        "projectileVisibleRows": 0,
        "fireballActiveRows": 0,
        "hazardVisibleRows": 0,
        "blockCandidateRows": 0,
        "bigStarDistance": {
            "foundRows": 0,
            "first": None,
            "firstFrame": None,
            "final": None,
            "finalFrame": None,
            "min": None,
            "minFrame": None,
            "max": None,
            "maxFrame": None,
            "average": None,
            "approachDelta": None,
        },
        "scoreboard": {
            "playerStarsStart": None,
            "playerStarsEnd": None,
            "opponentStarsStart": None,
            "opponentStarsEnd": None,
            "starDiffEnd": None,
            "winner": "unknown",
            "playerCoinsStart": None,
            "playerCoinsEnd": None,
            "opponentCoinsStart": None,
            "opponentCoinsEnd": None,
        },
        "heuristicScore": 0.0,
        "notes": [],
    }


def finalize_eval(result: dict[str, Any], held_counts: dict[str, int], distance_sum: int) -> dict[str, Any]:
    result["playerFoundRatio"] = ratio(result["playerFoundRows"], result["rows"])
    result["gameplayRatio"] = ratio(result["gameplayRows"], result["rows"])
    result["aliveRatio"] = ratio(result["aliveRows"], result["gameplayRows"])
    result["nonzeroInputRatio"] = ratio(result["nonzeroInputRows"], result["inputRows"])
    result["heldTop"] = top_counts(held_counts)

    dist = result["bigStarDistance"]
    if dist["foundRows"] > 0:
        dist["average"] = distance_sum / float(dist["foundRows"])
        dist["approachDelta"] = int(dist["first"]) - int(dist["final"])

    board = result["scoreboard"]
    if board["playerStarsEnd"] is not None and board["opponentStarsEnd"] is not None:
        board["starDiffEnd"] = int(board["playerStarsEnd"]) - int(board["opponentStarsEnd"])
        if board["starDiffEnd"] > 0:
            board["winner"] = "player"
        elif board["starDiffEnd"] < 0:
            board["winner"] = "opponent"
        else:
            board["winner"] = "tie"

    approach = float(dist["approachDelta"] or 0) / 1_000_000.0
    star_delta = (board["playerStarsEnd"] or 0) - (board["playerStarsStart"] or 0)
    result["heuristicScore"] = (
        star_delta * 1000.0
        + approach
        + result["aliveRows"] * 0.1
        - result["deathTransitions"] * 500.0
        + result["itemVisibleRows"] * 2.0
        + result["projectileVisibleRows"] * 1.0
    )
    return result


def evaluate_jsonl(path: Path, player: int, policy: str) -> dict[str, Any]:
    result = base_eval(path, player, policy, "ai_play_log_jsonl")
    held_counts: dict[str, int] = {}
    prev_player: dict[str, Any] | None = None
    prev_opponent: dict[str, Any] | None = None
    distance_sum = 0

    for record in iter_jsonl(path):
        frame = num(record.get("frame"))
        result["rows"] += 1
        result["frameStart"] = frame if result["frameStart"] is None else result["frameStart"]
        result["frameEnd"] = frame
        in_gameplay = bool(record.get("inGameplay"))
        if in_gameplay:
            result["gameplayRows"] += 1
            result["gameplayFrameStart"] = frame if result["gameplayFrameStart"] is None else result["gameplayFrameStart"]
            result["gameplayFrameEnd"] = frame

        held = held_for_record(record, player)
        if held is not None:
            result["inputRows"] += 1
            held_hex = f"0x{held:03X}"
            held_counts[held_hex] = held_counts.get(held_hex, 0) + 1
            if held:
                result["nonzeroInputRows"] += 1

        p = player_record(record, player) or {}
        o = player_record(record, player ^ 1) or {}
        if p.get("found"):
            result["playerFoundRows"] += 1
        if o.get("found"):
            result["opponentFoundRows"] += 1
        if in_gameplay and p.get("found"):
            if p.get("dead"):
                result["deadRows"] += 1
            else:
                result["aliveRows"] += 1

        fall = p.get("fallRisk") or {}
        if num(fall.get("nearCameraBottom0")) or num(fall.get("nearCameraBottom1")):
            result["nearCameraBottomRows"] += 1
        if num(fall.get("belowCamera0")) or num(fall.get("belowCamera1")):
            result["belowCameraRows"] += 1
        tile_summary = ((p.get("tileProbe") or {}).get("summary")) or {}
        if num(tile_summary.get("effectiveHoleAhead"), num(tile_summary.get("holeAhead"))):
            result["holeAheadRows"] += 1

        nearest = nearest_big_star_json(record, player)
        if nearest is not None:
            dist2 = num(nearest.get("dist2"))
            dist = result["bigStarDistance"]
            dist["foundRows"] += 1
            if dist["first"] is None:
                dist["first"] = dist2
                dist["firstFrame"] = frame
            dist["final"] = dist2
            dist["finalFrame"] = frame
            update_min_max(dist, dist2, frame)
            distance_sum += dist2

        counts = category_counts(record)
        if count_categories(counts, ITEM_CATEGORIES) > 0:
            result["itemVisibleRows"] += 1
        if count_categories(counts, PROJECTILE_CATEGORIES) > 0:
            result["projectileVisibleRows"] += 1
        if count_categories(counts, HAZARD_CATEGORIES) > 0:
            result["hazardVisibleRows"] += 1
        for sample in (p.get("tileProbe") or {}).get("samples") or []:
            block = sample.get("block") or {}
            if block.get("any") or block.get("itemBox"):
                result["blockCandidateRows"] += 1
                break
        fireballs = ((record.get("specialObjects") or {}).get("fireballs")) or {}
        if num(fireballs.get("active")) > 0 or num(fireballs.get("activeSlots")) > 0 or fireballs.get("slots"):
            result["fireballActiveRows"] += 1

        board = result["scoreboard"]
        for prefix, current in (("player", p), ("opponent", o)):
            stars = num(current.get("battleStars"))
            coins = num(current.get("coins"))
            start_key = f"{prefix}StarsStart"
            end_key = f"{prefix}StarsEnd"
            coin_start_key = f"{prefix}CoinsStart"
            coin_end_key = f"{prefix}CoinsEnd"
            if current.get("found") and board[start_key] is None:
                board[start_key] = stars
                board[coin_start_key] = coins
            if current.get("found"):
                board[end_key] = stars
                board[coin_end_key] = coins

        if prev_player is not None:
            if p.get("dead") and not prev_player.get("dead"):
                result["deathTransitions"] += 1
            if num(p.get("battleStars")) > num(prev_player.get("battleStars")):
                result["starPickups"] += 1
            if num(p.get("coins")) != num(prev_player.get("coins")):
                result["coinChanges"] += 1
            if num(p.get("visualPowerupKindCandidate")) != num(prev_player.get("visualPowerupKindCandidate")):
                result["powerupChanges"] += 1
        prev_player = p
        prev_opponent = o

    if result["rows"] == 0:
        raise ValueError(f"{path}: no rows")
    _ = prev_opponent
    return finalize_eval(result, held_counts, distance_sum)


def csv_bool(row: dict[str, str], key: str) -> bool:
    return num(row.get(key)) != 0


def csv_dist_to_big_star(row: dict[str, str], player: int) -> int | None:
    if not csv_bool(row, f"playerActor{player}Found"):
        return None
    target_prefix = "vsStarActor" if csv_bool(row, "vsStarActorFound") else "vsStar"
    if not csv_bool(row, f"{target_prefix}Found"):
        return None
    px = signed32(row.get(f"playerActor{player}X"))
    py = signed32(row.get(f"playerActor{player}Y"))
    tx = signed32(row.get(f"{target_prefix}X"))
    ty = signed32(row.get(f"{target_prefix}Y"))
    dx = tx - px
    dy = ty - py
    return dx * dx + dy * dy


def evaluate_csv(path: Path, player: int, policy: str) -> dict[str, Any]:
    result = base_eval(path, player, policy, "game_state_csv")
    held_counts: dict[str, int] = {}
    prev_dead = 0
    prev_stars: int | None = None
    prev_coins: int | None = None
    distance_sum = 0

    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            frame = num(row.get("frame"))
            result["rows"] += 1
            result["frameStart"] = frame if result["frameStart"] is None else result["frameStart"]
            result["frameEnd"] = frame
            in_gameplay = num(row.get("stageGroup")) == 9 and num(row.get("vsMode")) == 1
            if in_gameplay:
                result["gameplayRows"] += 1
                result["gameplayFrameStart"] = frame if result["gameplayFrameStart"] is None else result["gameplayFrameStart"]
                result["gameplayFrameEnd"] = frame

            held = num(row.get(f"inputPlayer{player}Held"))
            result["inputRows"] += 1
            held_hex = f"0x{held:03X}"
            held_counts[held_hex] = held_counts.get(held_hex, 0) + 1
            if held:
                result["nonzeroInputRows"] += 1

            found = csv_bool(row, f"playerActor{player}Found")
            opponent_found = csv_bool(row, f"playerActor{player ^ 1}Found")
            dead = num(row.get(f"player{player}Dead"))
            if found:
                result["playerFoundRows"] += 1
            if opponent_found:
                result["opponentFoundRows"] += 1
            if in_gameplay and found:
                if dead:
                    result["deadRows"] += 1
                else:
                    result["aliveRows"] += 1
            if result["rows"] > 1 and dead and not prev_dead:
                result["deathTransitions"] += 1
            prev_dead = dead

            dist2 = csv_dist_to_big_star(row, player)
            if dist2 is not None:
                dist = result["bigStarDistance"]
                dist["foundRows"] += 1
                if dist["first"] is None:
                    dist["first"] = dist2
                    dist["firstFrame"] = frame
                dist["final"] = dist2
                dist["finalFrame"] = frame
                update_min_max(dist, dist2, frame)
                distance_sum += dist2

            stars = num(row.get(f"player{player}BattleStars"))
            coins = num(row.get(f"player{player}Coins"))
            opponent_stars = num(row.get(f"player{player ^ 1}BattleStars"))
            opponent_coins = num(row.get(f"player{player ^ 1}Coins"))
            board = result["scoreboard"]
            if found and board["playerStarsStart"] is None:
                board["playerStarsStart"] = stars
                board["playerCoinsStart"] = coins
            if opponent_found and board["opponentStarsStart"] is None:
                board["opponentStarsStart"] = opponent_stars
                board["opponentCoinsStart"] = opponent_coins
            if found:
                board["playerStarsEnd"] = stars
                board["playerCoinsEnd"] = coins
            if opponent_found:
                board["opponentStarsEnd"] = opponent_stars
                board["opponentCoinsEnd"] = opponent_coins
            if prev_stars is not None and stars > prev_stars:
                result["starPickups"] += 1
            if prev_coins is not None and coins != prev_coins:
                result["coinChanges"] += 1
            prev_stars = stars
            prev_coins = coins

    if result["rows"] == 0:
        raise ValueError(f"{path}: no rows")
    return finalize_eval(result, held_counts, distance_sum)


def evaluate_path(path: Path, player: int, policy: str) -> dict[str, Any]:
    if path.suffix.lower() == ".jsonl":
        return evaluate_jsonl(path, player, policy)
    if path.suffix.lower() == ".csv":
        return evaluate_csv(path, player, policy)
    raise ValueError(f"{path}: unsupported input type; expected .jsonl or .csv")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", type=Path, nargs="+", help="AI play log JSONL or game-state CSV")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--policy", default="unknown")
    parser.add_argument("--fail-on-death", action="store_true")
    parser.add_argument("--min-gameplay-rows", type=int, default=0)
    args = parser.parse_args()

    evaluations = [evaluate_path(path, args.player, args.policy) for path in args.inputs]
    failures = []
    for result in evaluations:
        if args.fail_on_death and result["deathTransitions"] > 0:
            failures.append(f"{result['input']}: deathTransitions={result['deathTransitions']}")
        if result["gameplayRows"] < args.min_gameplay_rows:
            failures.append(f"{result['input']}: gameplayRows={result['gameplayRows']} < {args.min_gameplay_rows}")

    report = {
        "schema": "nsmb_mvl_ai_closed_loop_eval_v1",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "policy": args.policy,
        "player": args.player,
        "status": "fail" if failures else "pass",
        "failureCount": len(failures),
        "failures": failures,
        "evaluations": evaluations,
    }

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
