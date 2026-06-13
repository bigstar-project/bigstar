#!/usr/bin/env python3
"""Audit visible item actors and infer item kinds from nearby state changes."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import math
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ITEM_CATEGORIES = {"world_item", "neutral_item", "coin_item", "dropped_star_item", "item"}
DROP_STAR_SETTINGS_NORMALIZED = {0x1002, 0x1012, 0x1102, 0x1112}
FIRE_ITEM_SETTINGS = {0x00090000, 0x00011089}
COIN_ITEM_SETTINGS = {0x00090002}
MINI_ITEM_SETTINGS = {0x0001108B}
HORIZONTAL_WRAP_WIDTH = 0x400000


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


def hex_u(value: int, width: int = 8) -> str:
    return f"0x{value & ((1 << (width * 4)) - 1):0{width}X}"


def wrapped_dx(target_x: int, origin_x: int, wrap_width: int = HORIZONTAL_WRAP_WIDTH) -> int:
    if wrap_width <= 0:
        return target_x - origin_x
    half = wrap_width // 2
    return ((target_x - origin_x + half) % wrap_width) - half


def object_category(obj: dict[str, Any]) -> str:
    category = str(obj.get("category") or "object")
    object_id = num(obj.get("objectId"))
    settings = num(obj.get("settings"))
    if object_id == 0x001F and settings in COIN_ITEM_SETTINGS:
        return "coin_item"
    if object_id == 0x0022 and (settings & 0x7FFFFFFF) in DROP_STAR_SETTINGS_NORMALIZED:
        return "dropped_star_item"
    if object_id == 0x010C and settings == 0x00001120:
        return "big_star_marker"
    return category


def item_kind_candidate(object_id: int, settings: int, category: str) -> str:
    normalized = settings & 0x7FFFFFFF
    if category == "coin_item" or (object_id == 0x001F and settings in COIN_ITEM_SETTINGS):
        return "coin"
    if category == "dropped_star_item" or (object_id == 0x0022 and normalized in DROP_STAR_SETTINGS_NORMALIZED):
        return "dropped_battle_star"
    if settings in FIRE_ITEM_SETTINGS:
        return "fire_flower_candidate"
    if settings in MINI_ITEM_SETTINGS:
        return "mini_mushroom_suspected"
    return "unknown"


def pos(entity: dict[str, Any]) -> dict[str, int]:
    value = entity.get("pos") or {}
    return {"x": num(value.get("x")), "y": num(value.get("y")), "z": num(value.get("z"))}


def player_state(player: dict[str, Any]) -> dict[str, int]:
    visual = player.get("visualState") or {}
    return {
        "found": num(player.get("found")),
        "dead": num(player.get("dead")),
        "coins": num(player.get("coins")),
        "battleStars": num(player.get("battleStars")),
        "powerup": num(player.get("powerup")),
        "inventoryPowerup": num(player.get("inventoryPowerup")),
        "visualPowerupKindCandidate": num(visual.get("visualPowerupKindCandidate")),
        "invincibleCandidate": num(visual.get("invincibleCandidate")),
        "shellState": num(visual.get("shellState")),
    }


def player_changes(before: dict[str, int], after: dict[str, int]) -> dict[str, dict[str, int]]:
    changes: dict[str, dict[str, int]] = {}
    for key in [
        "dead",
        "coins",
        "battleStars",
        "powerup",
        "inventoryPowerup",
        "visualPowerupKindCandidate",
        "invincibleCandidate",
        "shellState",
    ]:
        if before.get(key) != after.get(key):
            changes[key] = {"before": before.get(key, 0), "after": after.get(key, 0)}
    return changes


def iter_records(path: Path):
    opener = gzip.open if path.name.lower().endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8-sig") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield line_no, json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def object_key(obj: dict[str, Any]) -> str:
    base = str(obj.get("base") or "")
    guid = str(obj.get("guid") or "")
    object_id = num(obj.get("objectId"))
    settings = num(obj.get("settings"))
    vtable = num(obj.get("vtable"))
    if base and base != "0x00000000":
        return f"base:{base}"
    if guid and guid != "0x00000000":
        return f"guid:{guid}:id:{object_id:04X}:settings:{settings:08X}"
    return f"id:{object_id:04X}:settings:{settings:08X}:vtable:{vtable:08X}:pos:{num((obj.get('pos') or {}).get('x'))}:{num((obj.get('pos') or {}).get('y'))}"


def cluster_key_for(obj: dict[str, Any], category: str) -> str:
    return "|".join(
        [
            category,
            hex_u(num(obj.get("objectId")), 4),
            hex_u(num(obj.get("settings"))),
            hex_u(num(obj.get("vtable"))),
        ]
    )


def nearest_player_distance(obj: dict[str, Any], players: list[dict[str, Any]]) -> dict[str, int]:
    obj_pos = pos(obj)
    best: dict[str, int] | None = None
    for player_index, player in enumerate(players[:2]):
        if not player or not num(player.get("found")):
            continue
        p = pos(player)
        dx = wrapped_dx(obj_pos["x"], p["x"])
        dy = obj_pos["y"] - p["y"]
        dist = int(math.isqrt(dx * dx + dy * dy))
        candidate = {"player": player_index, "dx": dx, "dy": dy, "dist": dist}
        if best is None or dist < best["dist"]:
            best = candidate
    return best or {"player": -1, "dx": 0, "dy": 0, "dist": 0}


def in_any_camera(obj: dict[str, Any]) -> int:
    screen = obj.get("screen") or {}
    return int(any(num((screen.get(camera) or {}).get("inView")) for camera in ("camera0", "camera1")))


def empty_track(
    playlog: Path,
    record: dict[str, Any],
    obj: dict[str, Any],
    category: str,
    key: str,
) -> dict[str, Any]:
    object_id = num(obj.get("objectId"))
    settings = num(obj.get("settings"))
    cluster_key = cluster_key_for(obj, category)
    return {
        "key": key,
        "playlog": str(playlog),
        "clusterKey": cluster_key,
        "category": category,
        "kindCandidate": item_kind_candidate(object_id, settings, category),
        "objectId": hex_u(object_id, 4),
        "settings": hex_u(settings),
        "settingsInt": settings,
        "vtable": hex_u(num(obj.get("vtable"))),
        "firstFrame": num(record.get("frame")),
        "lastFrame": num(record.get("frame")),
        "frames": 0,
        "visibleFrames": 0,
        "minPlayerDist": None,
        "nearestPlayer": -1,
        "nearestDx": 0,
        "nearestDy": 0,
        "lastX": 0,
        "lastY": 0,
        "lastZ": 0,
        "sampleFrames": [],
        "events": [],
    }


def update_track(track: dict[str, Any], record: dict[str, Any], obj: dict[str, Any], sample_limit: int) -> None:
    frame = num(record.get("frame"))
    p = pos(obj)
    track["lastFrame"] = frame
    track["frames"] += 1
    track["visibleFrames"] += in_any_camera(obj)
    track["lastX"] = p["x"]
    track["lastY"] = p["y"]
    track["lastZ"] = p["z"]
    nearest = nearest_player_distance(obj, record.get("players") or [])
    min_dist = track.get("minPlayerDist")
    if min_dist is None or nearest["dist"] < min_dist:
        track["minPlayerDist"] = nearest["dist"]
        track["nearestPlayer"] = nearest["player"]
        track["nearestDx"] = nearest["dx"]
        track["nearestDy"] = nearest["dy"]
    if len(track["sampleFrames"]) < sample_limit:
        track["sampleFrames"].append(
            {
                "frame": frame,
                "x": p["x"],
                "y": p["y"],
                "z": p["z"],
                "nearestPlayer": nearest,
                "inView": in_any_camera(obj),
            }
        )


def assign_event_to_track(
    event: dict[str, Any],
    candidates: list[dict[str, Any]],
    max_event_distance: int,
    max_event_lag: int,
) -> None:
    best: tuple[int, dict[str, Any], int] | None = None
    event_pos = event.get("playerPos") or {}
    event_player = num(event.get("player"), -1)
    for track in candidates:
        frame_lag = abs(num(event.get("frame")) - num(track.get("lastFrame")))
        if frame_lag > max_event_lag:
            continue
        dx = wrapped_dx(num(track.get("lastX")), num(event_pos.get("x")))
        dy = num(track.get("lastY")) - num(event_pos.get("y"))
        dist = int(math.isqrt(dx * dx + dy * dy))
        if dist > max_event_distance:
            continue
        score = frame_lag * 0x100000 + int(dist)
        if best is None or score < best[0]:
            best = (score, track, dist)
    if best is not None:
        track = best[1]
        event = dict(event)
        event["assignedDistance"] = best[2]
        event["assignedFrameLag"] = abs(num(event.get("frame")) - num(track.get("lastFrame")))
        event["assignedPlayer"] = event_player
        track["events"].append(event)


def summarize_clusters(tracks: list[dict[str, Any]], sample_limit: int) -> list[dict[str, Any]]:
    clusters: dict[str, dict[str, Any]] = {}
    for track in tracks:
        key = str(track["clusterKey"])
        cluster = clusters.setdefault(
            key,
            {
                "clusterKey": key,
                "category": track["category"],
                "kindCandidate": track["kindCandidate"],
                "objectId": track["objectId"],
                "settings": track["settings"],
                "vtable": track["vtable"],
                "trackCount": 0,
                "totalFrames": 0,
                "visibleFrames": 0,
                "minPlayerDist": None,
                "eventCounts": Counter(),
                "changeCounts": Counter(),
                "sampleTracks": [],
            },
        )
        cluster["trackCount"] += 1
        cluster["totalFrames"] += int(track["frames"])
        cluster["visibleFrames"] += int(track["visibleFrames"])
        dist = track.get("minPlayerDist")
        if dist is not None and (cluster["minPlayerDist"] is None or dist < cluster["minPlayerDist"]):
            cluster["minPlayerDist"] = dist
        for event in track["events"]:
            cluster["eventCounts"][str(event.get("type"))] += 1
            for changed in (event.get("changes") or {}).keys():
                cluster["changeCounts"][changed] += 1
        if len(cluster["sampleTracks"]) < sample_limit:
            cluster["sampleTracks"].append(track)

    result: list[dict[str, Any]] = []
    for cluster in clusters.values():
        cluster["eventCounts"] = dict(cluster["eventCounts"])
        cluster["changeCounts"] = dict(cluster["changeCounts"])
        result.append(cluster)
    result.sort(key=lambda item: (-int(item["trackCount"]), str(item["clusterKey"])))
    return result


def audit_playlogs(
    playlogs: list[Path],
    sample_limit: int,
    event_lag: int,
    event_distance: int,
) -> dict[str, Any]:
    all_tracks: list[dict[str, Any]] = []
    total_rows = 0
    total_item_rows = 0
    transition_counts: Counter[str] = Counter()

    for playlog in playlogs:
        active: dict[str, dict[str, Any]] = {}
        recently_seen: list[dict[str, Any]] = []
        previous_players: list[dict[str, int]] | None = None
        rows = 0
        item_rows = 0
        for _line_no, record in iter_records(playlog):
            rows += 1
            frame = num(record.get("frame"))
            players = record.get("players") or []
            current_players = [player_state(player) for player in players[:2]]
            frame_items: dict[str, tuple[dict[str, Any], str]] = {}
            for obj in record.get("objects") or []:
                category = object_category(obj)
                if category not in ITEM_CATEGORIES:
                    continue
                key = object_key(obj)
                frame_items[key] = (obj, category)
                if key not in active:
                    active[key] = empty_track(playlog, record, obj, category, key)
                update_track(active[key], record, obj, sample_limit)

            if frame_items:
                item_rows += 1

            ended_keys = [key for key in active.keys() if key not in frame_items]
            for key in ended_keys:
                track = active.pop(key)
                recently_seen.append(track)
                all_tracks.append(track)
            recently_seen = [
                track
                for track in recently_seen
                if frame - int(track.get("lastFrame", frame)) <= event_lag
            ]

            if previous_players is not None:
                for player_index, state in enumerate(current_players):
                    if player_index >= len(previous_players):
                        continue
                    changes = player_changes(previous_players[player_index], state)
                    if not changes:
                        continue
                    event_type = "player_state_change"
                    transition_counts[event_type] += 1
                    for changed in changes:
                        transition_counts[f"change:{changed}"] += 1
                    event = {
                        "type": event_type,
                        "frame": frame,
                        "player": player_index,
                        "playerPos": pos(players[player_index]),
                        "changes": changes,
                    }
                    assign_event_to_track(
                        event,
                        list(active.values()) + recently_seen,
                        event_distance,
                        event_lag,
                    )
            previous_players = current_players

        all_tracks.extend(active.values())
        total_rows += rows
        total_item_rows += item_rows

    clusters = summarize_clusters(all_tracks, sample_limit)
    return {
        "schema": "nsmb_mvl_item_audit_v1",
        "playlogs": [str(path) for path in playlogs],
        "rows": total_rows,
        "itemRows": total_item_rows,
        "trackCount": len(all_tracks),
        "clusterCount": len(clusters),
        "transitionCounts": dict(transition_counts),
        "clusters": clusters,
    }


def write_cluster_csv(path: Path, clusters: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "clusterKey",
        "category",
        "kindCandidate",
        "objectId",
        "settings",
        "vtable",
        "trackCount",
        "totalFrames",
        "visibleFrames",
        "minPlayerDist",
        "eventCounts",
        "changeCounts",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for cluster in clusters:
            row = {field: cluster.get(field) for field in fields}
            row["eventCounts"] = json.dumps(row["eventCounts"], ensure_ascii=False, sort_keys=True)
            row["changeCounts"] = json.dumps(row["changeCounts"], ensure_ascii=False, sort_keys=True)
            writer.writerow(row)


def render_svgs(report: dict[str, Any], output_dir: Path, per_cluster: int, player: int) -> None:
    if per_cluster <= 0:
        return
    script = Path(__file__).with_name("nsmb_mvl_ai_render_playlog_svg.py")
    output_dir.mkdir(parents=True, exist_ok=True)
    for cluster_index, cluster in enumerate(report.get("clusters") or []):
        rendered = 0
        safe_key = "".join(ch if ch.isalnum() else "_" for ch in str(cluster["clusterKey"]))[:96]
        for track in cluster.get("sampleTracks") or []:
            for sample in track.get("sampleFrames") or []:
                if rendered >= per_cluster:
                    break
                frame = num(sample.get("frame"))
                out = output_dir / f"cluster{cluster_index:03d}-{safe_key}-f{frame}-p{player}.svg"
                subprocess.run(
                    [
                        sys.executable,
                        str(script),
                        str(track["playlog"]),
                        str(out),
                        "--frame",
                        str(frame),
                        "--player",
                        str(player),
                    ],
                    check=True,
                )
                rendered += 1
            if rendered >= per_cluster:
                break


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("playlogs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--svg-dir", type=Path)
    parser.add_argument("--svg-per-cluster", type=int, default=0)
    parser.add_argument("--svg-player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--sample-limit", type=int, default=4)
    parser.add_argument("--event-lag", type=int, default=45)
    parser.add_argument("--event-distance", type=int, default=0x50000)
    args = parser.parse_args()

    report = audit_playlogs(args.playlogs, args.sample_limit, args.event_lag, args.event_distance)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    if args.csv:
        write_cluster_csv(args.csv, report["clusters"])
    if args.svg_dir:
        render_svgs(report, args.svg_dir, args.svg_per_cluster, args.svg_player)
    print(
        f"itemAudit={args.output} rows={report['rows']} "
        f"tracks={report['trackCount']} clusters={report['clusterCount']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
