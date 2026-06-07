#!/usr/bin/env python3
"""Create a metadata manifest for an NSMB MvL AI/human play recording."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


INTERESTING_CATEGORIES = [
    "big_star_actor",
    "big_star_related",
    "big_star_candidate",
    "world_item",
    "neutral_item",
    "dropped_star_item",
    "item",
    "coin",
    "moving_hazard",
    "hazard",
    "enemy_goomba",
    "enemy_koopa",
    "platform",
    "item_spawn_effect",
    "projectile",
    "player_fireball",
    "enemy_fireball",
]


def num(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    return default


def rel(path: Path | None, base: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path)


def iter_records(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def player_summary(player: dict[str, Any]) -> dict[str, Any]:
    pos = player.get("pos") or {}
    visual_state = player.get("visualState") or {}
    powerup_state = visual_state.get("powerup") or {}
    inventory_state = visual_state.get("inventoryPowerup") or {}
    return {
        "found": bool(player.get("found")),
        "x": num(pos.get("x")),
        "y": num(pos.get("y")),
        "z": num(pos.get("z")),
        "powerup": num(player.get("powerup")),
        "powerupName": powerup_state.get("name"),
        "dead": num(player.get("dead")),
        "inventoryPowerup": num(player.get("inventoryPowerup")),
        "inventoryPowerupName": inventory_state.get("name"),
        "hasReserveItemCandidate": bool(visual_state.get("hasReserveItemCandidate")),
        "visualPowerupKindCandidate": num(visual_state.get("visualPowerupKindCandidate")),
        "visualPowerupSourceMask": num(visual_state.get("visualPowerupSourceMask")),
        "isFireVisualCandidate": bool(visual_state.get("isFireVisualCandidate")),
        "canShootFireVisualCandidate": bool(visual_state.get("canShootFireVisualCandidate")),
        "damageCooldown": num(player.get("damageCooldown")),
        "damageGuardTimer": num(visual_state.get("damageGuardTimer")),
        "damageGuardFlag": num(visual_state.get("damageGuardFlag")),
        "damagePhysicsGuard": bool(visual_state.get("damagePhysicsGuard")),
        "shellState": num(visual_state.get("shellState")),
        "actorPowerupState": num(visual_state.get("actorPowerupState")),
        "actorPowerupFormState": num(visual_state.get("actorPowerupFormState")),
        "invincibleKnown": bool(visual_state.get("invincibleKnown")),
        "invincibleCandidate": bool(visual_state.get("invincibleCandidate")),
        "battleStars": num(player.get("battleStars")),
        "coins": num(player.get("coins")),
    }


def label_value(record: dict[str, Any], player: int, source: str) -> int | None:
    inputs = record.get("inputs") or {}
    applied = inputs.get(f"appliedPlayer{player}") or {}
    if source == "applied":
        return num(applied.get("held")) if applied.get("valid") else None
    if source == "player":
        return num((inputs.get(f"player{player}") or {}).get("held"))
    if source == "console":
        return num((inputs.get(f"console{player}") or {}).get("held"))
    if applied.get("valid"):
        return num(applied.get("held"))
    return num((inputs.get(f"player{player}") or {}).get("held"))


def summarize(playlog: Path, player: int, label_source: str, max_event_samples: int) -> dict[str, Any]:
    first: dict[str, Any] | None = None
    last: dict[str, Any] | None = None
    prev_players: list[dict[str, Any]] | None = None
    rows = 0
    gameplay_rows = 0
    player_found_rows = 0
    label_rows = 0
    nonzero_label_rows = 0
    category_frames = {name: 0 for name in INTERESTING_CATEGORIES}
    special_object_frames = {
        "fireballActive": 0,
    }
    block_candidate_frames = 0
    event_counts = {
        "starPickup": 0,
        "coinChange": 0,
        "powerupChange": 0,
        "visualPowerupChange": 0,
        "playerDeath": 0,
        "blockCandidateVisible": 0,
        "itemVisible": 0,
        "projectileVisible": 0,
        "fireballActive": 0,
    }
    event_samples: dict[str, list[dict[str, Any]]] = {name: [] for name in event_counts}

    def add_event_sample(name: str, record: dict[str, Any], details: dict[str, Any] | None = None) -> None:
        if max_event_samples <= 0 or len(event_samples[name]) >= max_event_samples:
            return
        sample: dict[str, Any] = {"frame": num(record.get("frame"))}
        if details:
            sample.update(details)
        event_samples[name].append(sample)

    for record in iter_records(playlog):
        rows += 1
        if first is None:
            first = record
        last = record
        if record.get("inGameplay"):
            gameplay_rows += 1
        players = record.get("players") or []
        if len(players) > player and players[player].get("found"):
            player_found_rows += 1
        held = label_value(record, player, label_source)
        if held is not None:
            label_rows += 1
            if held:
                nonzero_label_rows += 1

        categories = {str(obj.get("category")) for obj in record.get("objects") or []}
        for name in category_frames:
            if name in categories:
                category_frames[name] += 1
        if categories.intersection({"world_item", "neutral_item", "dropped_star_item", "item"}):
            event_counts["itemVisible"] += 1
            add_event_sample("itemVisible", record, {"categories": sorted(categories.intersection({"world_item", "neutral_item", "dropped_star_item", "item"}))})
        if categories.intersection({"projectile", "player_fireball", "enemy_fireball"}):
            event_counts["projectileVisible"] += 1
            add_event_sample("projectileVisible", record, {"categories": sorted(categories.intersection({"projectile", "player_fireball", "enemy_fireball"}))})
        fireballs = ((record.get("specialObjects") or {}).get("fireballs")) or {}
        fireballs_active = num(fireballs.get("active"))
        slots = fireballs.get("slots") or []
        if fireballs_active > 0 or slots:
            special_object_frames["fireballActive"] += 1
            event_counts["fireballActive"] += 1
            add_event_sample(
                "fireballActive",
                record,
                {
                    "active": fireballs_active,
                    "activeSlots": num(fireballs.get("activeSlots")),
                    "slotCount": len(slots),
                    "slots": slots[:4],
                    "handler": fireballs.get("handler"),
                    "handlerPtr": fireballs.get("handlerPtr"),
                    "words": fireballs.get("words") or [],
                },
            )

        block_visible = False
        block_details: dict[str, Any] | None = None
        for player_index, p in enumerate(players):
            for sample_index, sample in enumerate((p.get("tileProbe") or {}).get("samples") or []):
                block = sample.get("block") or {}
                if block.get("any") or block.get("itemBox"):
                    block_visible = True
                    block_details = {
                        "player": player_index,
                        "sampleIndex": sample_index,
                        "sample": sample.get("name"),
                        "tileId": num(block.get("currentTileId")),
                        "behavior": num(block.get("currentBehavior")),
                        "itemBox": bool(block.get("itemBox")),
                        "storageContents": num(block.get("storageContents")),
                    }
                    break
            if block_visible:
                break
        if block_visible:
            block_candidate_frames += 1
            event_counts["blockCandidateVisible"] += 1
            add_event_sample("blockCandidateVisible", record, block_details)

        current_players = [player_summary(p) for p in players[:2]]
        if prev_players and len(prev_players) == len(current_players):
            for player_index, (before, after) in enumerate(zip(prev_players, current_players)):
                if after["battleStars"] > before["battleStars"]:
                    event_counts["starPickup"] += 1
                    add_event_sample("starPickup", record, {"player": player_index, "before": before["battleStars"], "after": after["battleStars"]})
                if after["coins"] != before["coins"]:
                    event_counts["coinChange"] += 1
                    add_event_sample("coinChange", record, {"player": player_index, "before": before["coins"], "after": after["coins"]})
                if after["powerup"] != before["powerup"]:
                    event_counts["powerupChange"] += 1
                    add_event_sample(
                        "powerupChange",
                        record,
                        {
                            "player": player_index,
                            "before": before["powerup"],
                            "after": after["powerup"],
                            "beforeName": before.get("powerupName"),
                            "afterName": after.get("powerupName"),
                        },
                    )
                if after["visualPowerupKindCandidate"] != before["visualPowerupKindCandidate"]:
                    event_counts["visualPowerupChange"] += 1
                    add_event_sample(
                        "visualPowerupChange",
                        record,
                        {
                            "player": player_index,
                            "before": before["visualPowerupKindCandidate"],
                            "after": after["visualPowerupKindCandidate"],
                            "beforeSourceMask": before.get("visualPowerupSourceMask"),
                            "afterSourceMask": after.get("visualPowerupSourceMask"),
                            "beforeFire": before.get("isFireVisualCandidate"),
                            "afterFire": after.get("isFireVisualCandidate"),
                        },
                    )
                if after["dead"] and not before["dead"]:
                    event_counts["playerDeath"] += 1
                    add_event_sample("playerDeath", record, {"player": player_index})
        prev_players = current_players

    if first is None or last is None:
        raise ValueError(f"{playlog}: no records")

    return {
        "rows": rows,
        "gameplayRows": gameplay_rows,
        "playerFoundRows": player_found_rows,
        "labelRows": label_rows,
        "nonzeroLabelRows": nonzero_label_rows,
        "frameStart": num(first.get("frame")),
        "frameEnd": num(last.get("frame")),
        "hashStart": first.get("hash"),
        "hashEnd": last.get("hash"),
        "stageStart": first.get("stage"),
        "stageEnd": last.get("stage"),
        "playersEnd": [player_summary(p) for p in (last.get("players") or [])[:2]],
        "objectSummaryEnd": last.get("objectSummary"),
        "categoryFrames": category_frames,
        "specialObjectFrames": special_object_frames,
        "blockCandidateFrames": block_candidate_frames,
        "eventCounts": event_counts,
        "eventSamples": event_samples,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlog", type=Path, help="AI/human play log JSONL")
    parser.add_argument("output", type=Path, help="recording manifest JSON")
    parser.add_argument("--kind", choices=["human", "rule_ai", "replay", "self_play"], default="human")
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--label-source", choices=["auto", "applied", "player", "console"], default="player")
    parser.add_argument("--stage", type=int, default=0)
    parser.add_argument("--host-input-script", type=Path)
    parser.add_argument("--client-input-script", type=Path)
    parser.add_argument("--replay-mode", choices=["auto", "input_script", "packet_replay"], default="auto")
    parser.add_argument("--packet-replay-file", type=Path)
    parser.add_argument("--host-packet-replay-file", type=Path)
    parser.add_argument("--client-packet-replay-file", type=Path)
    parser.add_argument("--host-packet-capture", type=Path)
    parser.add_argument("--client-packet-capture", type=Path)
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--stdout", type=Path)
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--match-seed", default="")
    parser.add_argument("--host-rom", type=Path)
    parser.add_argument("--client-rom", type=Path)
    parser.add_argument("--build-id", default="")
    parser.add_argument("--rom-id", default="")
    parser.add_argument("--scenario", default="")
    parser.add_argument("--quality", choices=["unreviewed", "accepted", "rejected", "needs_reclassification"], default="unreviewed")
    parser.add_argument("--max-event-samples", type=int, default=64)
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    base = args.output.parent
    base.mkdir(parents=True, exist_ok=True)
    summary = summarize(args.playlog, args.player, args.label_source, args.max_event_samples)
    replay_frames = args.frames if args.frames > 0 else int(summary.get("frameEnd") or 0)
    replay_mode = args.replay_mode
    if replay_mode == "auto":
        if args.packet_replay_file or args.host_packet_replay_file or args.client_packet_replay_file:
            replay_mode = "packet_replay"
        elif args.host_input_script or args.client_input_script:
            replay_mode = "input_script"
        else:
            replay_mode = ""
    manifest = {
        "schema": "nsmb_mvl_ai_recording_manifest_v1",
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "kind": args.kind,
        "stageScope": args.stage,
        "player": args.player,
        "labelSource": args.label_source,
        "playLog": rel(args.playlog, base),
        "hostInputScript": rel(args.host_input_script, base),
        "clientInputScript": rel(args.client_input_script, base),
        "packetReplayFile": rel(args.packet_replay_file, base),
        "hostPacketReplayFile": rel(args.host_packet_replay_file, base),
        "clientPacketReplayFile": rel(args.client_packet_replay_file, base),
        "logDir": rel(args.log_dir, base),
        "stdout": rel(args.stdout, base),
        "replay": {
            "mode": replay_mode,
            "frames": replay_frames,
            "matchSeed": args.match_seed,
            "hostInputScript": rel(args.host_input_script, base),
            "clientInputScript": rel(args.client_input_script, base),
            "packetReplayFile": rel(args.packet_replay_file, base),
            "hostPacketReplayFile": rel(args.host_packet_replay_file, base),
            "clientPacketReplayFile": rel(args.client_packet_replay_file, base),
            "hostRom": rel(args.host_rom, base),
            "clientRom": rel(args.client_rom, base),
        },
        "packetCapture": {
            "host": rel(args.host_packet_capture, base),
            "client": rel(args.client_packet_capture, base),
        },
        "metadata": {
            "buildId": args.build_id,
            "romId": args.rom_id,
            "scenario": args.scenario,
        },
        "quality": {
            "status": args.quality,
            "reviewed": args.quality in {"accepted", "rejected", "needs_reclassification"},
        },
        "notes": args.notes,
        "summary": summary,
    }
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"manifest={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
