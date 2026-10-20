#!/usr/bin/env python3
"""Unified BabyC bench runner — 6 columns for every case.

Usage:
  python3 runners/run_bench.py --set all
  python3 runners/run_bench.py --set micro
  python3 runners/run_bench.py --gate shape
  python3 runners/run_bench.py --only isort,fib_rec
  python3 runners/run_bench.py --set classic --runs 3

Columns: ATS_no  ATS_acc  VM_no  VM_acc  python  gcc
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# allow `python3 runners/run_bench.py` from any cwd
sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib_bench import (  # noqa: E402
    filter_cases,
    find_babyc,
    format_table,
    load_manifest,
    measure_case,
    write_results,
    bench_root,
)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--set",
        default="all",
        help="micro|classic|variant|real|stress|all (default all)",
    )
    ap.add_argument(
        "--gate",
        default="all",
        help="shape|perf|real|vm|honest|all (default all)",
    )
    ap.add_argument(
        "--only",
        default="",
        help="comma-separated case ids or short names",
    )
    ap.add_argument("--runs", type=int, default=0, help="median-of-N (0=manifest default)")
    ap.add_argument("--timeout", type=float, default=0, help="per-run timeout seconds")
    ap.add_argument("--babyc", default="", help="path to babyc binary")
    ap.add_argument("--skip-gcc", action="store_true")
    ap.add_argument("--skip-python", action="store_true")
    ap.add_argument("--tag", default="latest", help="results file tag")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()

    manifest = load_manifest()
    defaults = manifest.get("defaults") or {}
    runs = args.runs or int(defaults.get("runs") or 5)
    timeout = args.timeout or float(defaults.get("timeout") or 120)
    only = [x.strip() for x in args.only.split(",") if x.strip()] or None

    cases = filter_cases(
        manifest.get("cases") or [],
        set_name=args.set,
        gate=args.gate,
        only=only,
    )
    if not cases:
        print("no cases matched", file=sys.stderr)
        return 2

    try:
        babyc = find_babyc(args.babyc or None)
    except FileNotFoundError as e:
        print(e, file=sys.stderr)
        return 2

    if not args.quiet:
        print(f"babyc={babyc}")
        print(f"set={args.set} gate={args.gate} runs={runs} cases={len(cases)}")
        print()

    rows = []
    for i, case in enumerate(cases, 1):
        if not args.quiet:
            print(f"[{i}/{len(cases)}] {case['id']} ...", flush=True)
        row = measure_case(
            case,
            babyc,
            runs=runs,
            timeout=timeout,
            skip_gcc=args.skip_gcc,
            skip_python=args.skip_python,
        )
        rows.append(row)
        if not args.quiet and row["status"] != "OK":
            print(f"  !! {row['status']}: {row.get('error', '')}", flush=True)

    print()
    print(format_table(rows))
    write_results(rows, bench_root() / "results", tag=args.tag)

    bad = sum(1 for r in rows if r.get("status") != "OK")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
