#!/usr/bin/env python3
"""Render SVG snapshots for closed-loop evaluation event frames."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import nsmb_mvl_ai_render_playlog_svg as svg_renderer


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-") or "event"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def parse_csv_ints(value: str) -> list[int]:
    result: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if part:
            result.append(int(part, 0))
    return result


def iter_eval_event_renders(
    eval_path: Path,
    report: dict[str, Any],
    events: set[str],
    offsets: list[int],
    output_dir: Path,
    player: int,
    max_objects: int,
) -> list[dict[str, Any]]:
    rendered: list[dict[str, Any]] = []
    base_dir = eval_path.parent
    policy = str(report.get("policy", eval_path.parent.name))

    for eval_index, evaluation in enumerate(report.get("evaluations") or []):
        input_path = Path(str(evaluation.get("input", "")))
        if not input_path.is_absolute():
            input_path = (base_dir / input_path).resolve()
        if not input_path.exists():
            continue

        event_samples = evaluation.get("eventSamples") or {}
        for event_name in sorted(events):
            for event_index, event in enumerate(event_samples.get(event_name) or []):
                frame = int(event.get("frame"))
                for offset in offsets:
                    target_frame = frame + offset
                    record = svg_renderer.choose_record(input_path, target_frame, player)
                    actual_frame = svg_renderer.num(record.get("frame"))
                    output_name = (
                        f"{safe_name(policy)}-eval{eval_index}-"
                        f"{safe_name(input_path.parent.name)}-{safe_name(event_name)}-"
                        f"{event_index:02d}-f{frame}-o{offset:+d}-actual{actual_frame}.svg"
                    )
                    output_path = output_dir / output_name
                    output_path.write_text(svg_renderer.render(record, player, max_objects), encoding="utf-8")
                    rendered.append(
                        {
                            "eval": str(eval_path),
                            "input": str(input_path),
                            "policy": policy,
                            "evalIndex": eval_index,
                            "event": event_name,
                            "eventIndex": event_index,
                            "eventFrame": frame,
                            "offset": offset,
                            "actualFrame": actual_frame,
                            "output": str(output_path),
                        }
                    )
    return rendered


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("eval_json", type=Path, nargs="+", help="closed-loop-eval.json files")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--events",
        default="deathTransitions,starPickups",
        help="comma-separated eventSamples keys",
    )
    parser.add_argument(
        "--offsets",
        default="-30,0,30",
        help="comma-separated frame offsets around each event frame",
    )
    parser.add_argument("--player", type=int, choices=[0, 1], default=1)
    parser.add_argument("--max-objects", type=int, default=48)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    events = {part.strip() for part in args.events.split(",") if part.strip()}
    offsets = parse_csv_ints(args.offsets)

    rendered: list[dict[str, Any]] = []
    for eval_path in args.eval_json:
        rendered.extend(
            iter_eval_event_renders(
                eval_path,
                load_json(eval_path),
                events,
                offsets,
                args.output_dir,
                args.player,
                args.max_objects,
            )
        )

    manifest = {
        "schema": "nsmb_mvl_ai_eval_event_svgs_v1",
        "renderedCount": len(rendered),
        "events": sorted(events),
        "offsets": offsets,
        "player": args.player,
        "renders": rendered,
    }
    manifest_path = args.manifest or (args.output_dir / "eval-event-svgs.json")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"rendered={len(rendered)} manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
