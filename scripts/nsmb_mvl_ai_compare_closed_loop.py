#!/usr/bin/env python3
"""Compare multiple NSMB MvL closed-loop evaluation reports."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


def num(value: Any, default: float = 0.0) -> float:
    if isinstance(value, bool):
        return float(int(value))
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str) and value != "":
        return float(value)
    return default


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def row_for_eval(report_path: Path, report: dict[str, Any], index: int, evaluation: dict[str, Any]) -> dict[str, Any]:
    board = evaluation.get("scoreboard") or {}
    dist = evaluation.get("bigStarDistance") or {}
    return {
        "report": str(report_path),
        "policy": evaluation.get("policy") or report.get("policy"),
        "evalIndex": index,
        "input": evaluation.get("input"),
        "source": evaluation.get("source"),
        "player": evaluation.get("player"),
        "rows": evaluation.get("rows"),
        "gameplayRows": evaluation.get("gameplayRows"),
        "aliveRows": evaluation.get("aliveRows"),
        "aliveRatio": evaluation.get("aliveRatio"),
        "deathTransitions": evaluation.get("deathTransitions"),
        "playerStarsEnd": board.get("playerStarsEnd"),
        "opponentStarsEnd": board.get("opponentStarsEnd"),
        "starDiffEnd": board.get("starDiffEnd"),
        "winner": board.get("winner"),
        "starPickups": evaluation.get("starPickups"),
        "bigStarApproachDelta": dist.get("approachDelta"),
        "bigStarMin": dist.get("min"),
        "bigStarFinal": dist.get("final"),
        "nonzeroInputRatio": evaluation.get("nonzeroInputRatio"),
        "itemVisibleRows": evaluation.get("itemVisibleRows"),
        "projectileVisibleRows": evaluation.get("projectileVisibleRows"),
        "fireballActiveRows": evaluation.get("fireballActiveRows"),
        "hazardVisibleRows": evaluation.get("hazardVisibleRows"),
        "blockCandidateRows": evaluation.get("blockCandidateRows"),
        "heuristicScore": evaluation.get("heuristicScore"),
    }


def aggregate_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_policy: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        by_policy.setdefault(str(row.get("policy")), []).append(row)
    result: list[dict[str, Any]] = []
    for policy, items in sorted(by_policy.items()):
        count = len(items)
        result.append(
            {
                "policy": policy,
                "evaluationCount": count,
                "avgHeuristicScore": sum(num(item.get("heuristicScore")) for item in items) / count,
                "avgAliveRatio": sum(num(item.get("aliveRatio")) for item in items) / count,
                "avgDeathTransitions": sum(num(item.get("deathTransitions")) for item in items) / count,
                "avgStarDiffEnd": sum(num(item.get("starDiffEnd")) for item in items) / count,
                "avgStarPickups": sum(num(item.get("starPickups")) for item in items) / count,
                "avgBigStarApproachDelta": sum(num(item.get("bigStarApproachDelta")) for item in items) / count,
                "avgNonzeroInputRatio": sum(num(item.get("nonzeroInputRatio")) for item in items) / count,
            }
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", type=Path, nargs="+", help="closed-loop-eval.json files")
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-csv", type=Path)
    args = parser.parse_args()

    rows: list[dict[str, Any]] = []
    for report_path in args.reports:
        report = load_json(report_path)
        if report.get("schema") != "nsmb_mvl_ai_closed_loop_eval_v1":
            raise ValueError(f"{report_path}: unsupported schema {report.get('schema')!r}")
        for index, evaluation in enumerate(report.get("evaluations") or []):
            rows.append(row_for_eval(report_path, report, index, evaluation))

    summary = {
        "schema": "nsmb_mvl_ai_closed_loop_compare_v1",
        "reportCount": len(args.reports),
        "evaluationCount": len(rows),
        "aggregate": aggregate_rows(rows),
        "rows": rows,
    }

    if args.output_json is not None:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.output_csv is not None:
        args.output_csv.parent.mkdir(parents=True, exist_ok=True)
        fieldnames = list(rows[0].keys()) if rows else []
        with args.output_csv.open("w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
