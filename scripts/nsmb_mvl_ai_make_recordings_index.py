#!/usr/bin/env python3
"""Create an index JSON that groups multiple NSMB MvL AI recording manifests."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def rel(path: Path, base: Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path, help="recordings-index JSON")
    parser.add_argument("manifests", type=Path, nargs="+", help="recording manifest JSON files")
    parser.add_argument("--stage", type=int, default=0)
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    base = args.output.parent
    recordings = []
    for manifest in args.manifests:
        with manifest.open("r", encoding="utf-8") as f:
            data = json.load(f)
        recordings.append(
            {
                "manifest": rel(manifest, base),
                "kind": data.get("kind"),
                "player": data.get("player"),
                "labelSource": data.get("labelSource"),
                "rows": (data.get("summary") or {}).get("rows"),
                "frameStart": (data.get("summary") or {}).get("frameStart"),
                "frameEnd": (data.get("summary") or {}).get("frameEnd"),
            }
        )

    index = {
        "schema": "nsmb_mvl_ai_recordings_index_v1",
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "stageScope": args.stage,
        "notes": args.notes,
        "recordings": recordings,
    }
    args.output.write_text(json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"recordings={len(recordings)} index={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
