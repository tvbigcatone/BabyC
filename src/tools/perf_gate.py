#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Performance gate — clear engine vs shared-kernel comparison.

Columns (wall-clock median seconds):
  ast_acc   AST product (shape/kernel ON)
  ast_noa   AST honest  (ACCEL=0)
  vm_acc    VM product  (EXEC=vm, shape/kernel ON)  — may CALL same IrTryFast*
  vm_noa    VM honest   (PATH=linear ACCEL=0) — engine only
  python    matching .py if present

Shared fast paths (InsertShift, ArrayAccum, …) run under BOTH ast_acc and
vm_acc via IrTryFast* / IrLinKernel → IrTryFast*.  Compare engines with
ast_noa vs vm_noa only.

Usage:
  BABYC=./babyc python3 tools/perf_gate.py --runs 3
  BABYC=./babyc python3 tools/perf_gate.py --write-baseline ../docs/perf_baseline_W29rc.json
"""
from __future__ import print_function
import argparse, json, os, statistics, subprocess, sys, time, platform, datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BENCH_SERIE1 = ROOT.parent / "bench" / "cases" / "classic"   # was bench/serie1 (moved by the bench reorganization)
BENCH_ROOT = ROOT.parent / "bench" / "cases" / "micro"        # was bench/*.c
PROGS = ["fibc", "isort", "structs", "sieve", "collatz", "tri", "matmul"]
PROG_FILE = {"fibc": "fib_rec_count"}                          # name in the new layout
MISSING = []
MICRO = [
    ("isort_micro", BENCH_ROOT / "isort.c", BENCH_ROOT / "isort.py"),
    ("isort_v2", BENCH_ROOT / "isort_decoy.c", None),
    ("string_proc", BENCH_ROOT / "string_scan.c", BENCH_ROOT / "string_scan.py"),
    ("string_proc_v2", BENCH_ROOT / "string_scan_bounded.c", None),
]
TOL = float(os.environ.get("BABYC_PERF_TOL", "0.05"))
N = int(os.environ.get("BABYC_PERF_RUNS", "3"))

ENV_AST_ACC = {"BABYC_PATH": "tree", "BABYC_ACCEL": "1"}
ENV_AST_NOA = {"BABYC_PATH": "tree", "BABYC_ACCEL": "0"}
ENV_VM_ACC = {"BABYC_PATH": "linear", "BABYC_ACCEL": "1"}
ENV_VM_NOA = {"BABYC_PATH": "linear", "BABYC_ACCEL": "0"}


def median_cmd(argv, runs, extra_env=None, timeout=180):
    env = os.environ.copy()
    env["BABYC_NO_MEMO"] = "1"
    for k in ("BABYC_NO_SHAPE_FP", "BABYC_SHAPE_FP", "BABYC_NO_KERNEL", "BABYC_EXEC",
              "BABYC_PATH", "BABYC_ACCEL", "BABYC_STRICT", "BABYC_VM_STRICT", "BABYC_BYTECODE_VM"):
        env.pop(k, None)
    if extra_env:
        env.update(extra_env)
    try:
        subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env)
    except Exception:
        pass
    ts = []
    out = None
    for _ in range(runs):
        t0 = time.perf_counter()
        r = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env)
        if r.returncode != 0:
            raise RuntimeError("rc=%s %s: %s" % (r.returncode, argv, (r.stderr or "")[:200]))
        ts.append(time.perf_counter() - t0)
        out = r.stdout
    return statistics.median(ts), out


def measure_babyc(babyc, src, runs, extra_env):
    return median_cmd([str(babyc), str(src)], runs, extra_env)


def measure_python(py_path, runs):
    if py_path is None or not Path(py_path).is_file():
        return None, None
    return median_cmd(["python3", str(py_path)], runs)


def find_py(name):
    p = BENCH_SERIE1 / (PROG_FILE.get(name, name) + ".py")
    return p if p.is_file() else None


def fmt(x):
    return "     -" if x is None else "%8.3f" % x


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", type=Path, default=None)
    ap.add_argument("--write-baseline", type=Path, default=None)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--runs", type=int, default=N)
    ap.add_argument("--no-micro", action="store_true")
    ap.add_argument("--no-python", action="store_true")
    args = ap.parse_args()
    babyc = Path(os.environ.get("BABYC", str(ROOT / "babyc"))).resolve()
    if not babyc.is_file():
        sys.stderr.write("babyc not found: %s\n" % babyc)
        return 2

    print("perf_gate BABYC=%s runs=%d" % (babyc, args.runs))
    print("  ast_acc / vm_acc = product accelerations ON (shared IrTryFast* kernels)")
    print("  ast_noa / vm_noa = ACCEL=0  ← compare engines here")
    print()
    hdr = "%-14s %8s %8s %8s %8s %8s" % (
        "prog", "ast_acc", "ast_noa", "vm_acc", "vm_noa", "python")
    print(hdr)
    print("-" * len(hdr))

    results = {}

    def one(name, c_src, py_src=None):
        row = {}
        notes = []
        a, oa = measure_babyc(babyc, c_src, args.runs, ENV_AST_ACC)
        n, on = measure_babyc(babyc, c_src, args.runs, ENV_AST_NOA)
        if oa != on:
            notes.append("AST out mismatch")
        row["ast_accel"] = a
        row["ast_noaccel"] = n
        ref_out = oa

        va, ova = measure_babyc(babyc, c_src, args.runs, ENV_VM_ACC)
        vn, ovn = measure_babyc(babyc, c_src, args.runs, ENV_VM_NOA)
        row["vm_accel"] = va
        row["vm_noaccel"] = vn
        if ova != ref_out:
            notes.append("vm_acc out!=AST")
        if ovn != ref_out:
            notes.append("vm_noa out!=AST")

        if not args.no_python:
            pmed, po = measure_python(py_src, args.runs)
            row["python"] = pmed
            if po is not None and (po or "").strip() != (ref_out or "").strip():
                notes.append("py out!=AST")
        else:
            row["python"] = None

        print("%-14s %s %s %s %s %s" % (
            name, fmt(row["ast_accel"]), fmt(row["ast_noaccel"]),
            fmt(row["vm_accel"]), fmt(row["vm_noaccel"]), fmt(row.get("python"))))
        if notes:
            print("               note: %s" % "; ".join(notes))
        results[name] = row

    if BENCH_SERIE1.is_dir():
        for p in PROGS:
            src = BENCH_SERIE1 / (PROG_FILE.get(p, p) + ".c")
            if src.is_file():
                one(p, src, find_py(p))
            else:
                print("%-14s MISSING: %s" % (p, src))
                MISSING.append(str(src))

    if not args.no_micro:
        print("--- micro (shape-sensitive: expect ast_acc≈vm_acc << *_noa) ---")
        for name, csrc, pysrc in MICRO:
            if csrc.is_file():
                one(name, csrc, pysrc)
            else:
                print("%-14s MISSING: %s" % (name, csrc))
                MISSING.append(str(csrc))

    # ratios summary
    print()
    print("%-14s %8s %8s %8s" % ("prog", "ast_a/noa", "vm_a/noa", "vm_noa/ast_noa"))
    print("-" * 50)
    for name, row in results.items():
        aa, an = row["ast_accel"], row["ast_noaccel"]
        va, vn = row["vm_accel"], row["vm_noaccel"]
        r1 = (an / aa) if aa else 0
        r2 = (vn / va) if va else 0
        r3 = (vn / an) if an else 0  # engine ratio: <1 means VM faster without kernels
        print("%-14s %7.1fx %7.1fx %7.2fx" % (name, r1, r2, r3))

    payload = {
        "median_s_accel": {k: v["ast_accel"] for k, v in results.items()},
        "median_s_noaccel": {k: v["ast_noaccel"] for k, v in results.items()},
        "median_s_vm_accel": {k: v["vm_accel"] for k, v in results.items()},
        "median_s_vm_noaccel": {k: v["vm_noaccel"] for k, v in results.items()},
        "median_s_python": {k: v.get("python") for k, v in results.items()},
        "median_s": {k: v["ast_accel"] for k, v in results.items()},
        "meta": {
            "date": datetime.date.today().isoformat(),
            "runs": args.runs,
            "modes": {
                "ast_accel": "product AST",
                "ast_noaccel": "NO_SHAPE+NO_KERNEL",
                "vm_accel": "EXEC=vm product (shared IrTryFast*)",
                "vm_noaccel": "EXEC=vm + NO_SHAPE+NO_KERNEL (engine only)",
                "python": "python3 .py",
            },
            "policy": "Compare engines with *_noa columns only",
            "machine": platform.node(),
            "babyc": str(babyc),
        },
    }
    if args.write_baseline:
        args.write_baseline.write_text(json.dumps(payload, indent=2) + "\n")
        print("wrote", args.write_baseline)

    if args.baseline and args.baseline.is_file():
        base = json.loads(args.baseline.read_text())
        ba = base.get("median_s_accel") or base.get("median_s") or {}
        print("--- vs baseline (ast_acc) ---")
        rc = 0
        for p, med in payload["median_s_accel"].items():
            if p not in ba or ba[p] is None:
                continue
            b = float(ba[p])
            d = (med - b) / b if b else 0
            flag = "OK" if d <= TOL else "REGRESS"
            if d > TOL:
                rc = 1
            print("  %-14s %+6.1f%% %s" % (p, d * 100, flag))
        if args.strict and rc:
            return 1
    if MISSING:
        print("perf_gate: %d program(s) not found -> nothing measured for them (FAIL)" % len(MISSING))
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
