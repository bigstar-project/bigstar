#!/usr/bin/env python3
"""Compare Python and C++ imitation-policy predictions frame by frame."""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from pathlib import Path


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("runtime_model", type=Path)
    parser.add_argument("cpp_exe", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    py_csv = args.output_dir / "python-predictions.csv"
    cpp_csv = args.output_dir / "cpp-predictions.csv"

    subprocess.run(
        [
            sys.executable,
            "scripts/nsmb_mvl_ai_export_runtime_model.py",
            str(args.model),
            str(args.runtime_model),
        ],
        check=True,
    )
    py_cmd = [
        sys.executable,
        "scripts/nsmb_mvl_ai_predict_imitation.py",
        str(args.model),
        str(args.dataset),
        str(py_csv),
        "--threshold",
        str(args.threshold),
    ]
    cpp_cmd = [
        str(args.cpp_exe),
        str(args.runtime_model),
        str(args.dataset),
        str(cpp_csv),
        "--threshold",
        str(args.threshold),
    ]
    if args.limit > 0:
        py_cmd += ["--limit", str(args.limit)]
        cpp_cmd += ["--limit", str(args.limit)]
    subprocess.run(py_cmd, check=True)
    subprocess.run(cpp_cmd, check=True)

    py_rows = read_rows(py_csv)
    cpp_rows = read_rows(cpp_csv)
    if len(py_rows) != len(cpp_rows):
        raise ValueError(f"row count mismatch: python={len(py_rows)} cpp={len(cpp_rows)}")

    mismatches: list[str] = []
    max_prob_diff = 0.0
    prob_columns = [name for name in py_rows[0].keys() if name.startswith("prob_")] if py_rows else []
    for index, (py_row, cpp_row) in enumerate(zip(py_rows, cpp_rows)):
        frame = py_row.get("frame", str(index))
        if py_row.get("pred_held") != cpp_row.get("pred_held"):
            mismatches.append(
                f"row={index} frame={frame} pred_held python={py_row.get('pred_held')} cpp={cpp_row.get('pred_held')}"
            )
            if len(mismatches) >= 20:
                break
        for column in prob_columns:
            diff = abs(float(py_row[column]) - float(cpp_row[column]))
            max_prob_diff = max(max_prob_diff, diff)
            if diff > 1e-5:
                mismatches.append(
                    f"row={index} frame={frame} {column} python={py_row[column]} cpp={cpp_row[column]} diff={diff:.8f}"
                )
                if len(mismatches) >= 20:
                    break
        if len(mismatches) >= 20:
            break

    summary = {
        "rows": len(py_rows),
        "threshold": args.threshold,
        "max_probability_diff": max_prob_diff,
        "python_csv": str(py_csv),
        "cpp_csv": str(cpp_csv),
        "mismatches": mismatches,
    }
    summary_csv = args.output_dir / "cpp-parity-summary.csv"
    with summary_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary.keys()))
        writer.writeheader()
        writer.writerow(summary)

    if mismatches:
        print(f"FAILED rows={len(py_rows)} max_probability_diff={max_prob_diff:.8f}")
        for mismatch in mismatches:
            print(mismatch)
        return 1

    print(
        "PASS rows={} max_probability_diff={:.8f} python={} cpp={}".format(
            len(py_rows),
            max_prob_diff,
            py_csv,
            cpp_csv,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
