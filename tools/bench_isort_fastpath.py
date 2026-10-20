#!/usr/bin/env python3
"""Differential correctness and paired timing for the insertion-shift fast path.

Measures end-to-end BabyC execution of bench/isort_v2.c with the shape kernel
enabled and disabled. Both modes must match a native GCC reference exactly.
"""
import argparse
import json
import os
import pathlib
import statistics
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "bench" / "cases" / "micro" / "isort_decoy.c"


def run(argv, env, timeout=30):
    start = time.perf_counter()
    p = subprocess.run(argv, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, timeout=timeout)
    ms = (time.perf_counter() - start) * 1000.0
    if p.returncode != 0:
        raise RuntimeError(f"{argv} failed rc={p.returncode}: {p.stderr[-1000:]}")
    return p.stdout.strip(), ms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--babyc", default=str(ROOT / "src" / "babyc"))
    ap.add_argument("--runs", type=int, default=11)
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    if args.runs < 3:
        ap.error("--runs must be >= 3")

    with tempfile.TemporaryDirectory(prefix="babyc-isort-") as td:
        native = str(pathlib.Path(td) / "isort_v2_native")
        cc = subprocess.run(["gcc", "-O2", str(SOURCE), "-o", native],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
        if cc.returncode:
            raise RuntimeError("GCC reference build failed: " + cc.stderr)
        native_out, _ = run([native], os.environ.copy())

    modes = {
        "shape_enabled": {"BABYC_SHAPE_FP": "1"},
        "shape_disabled": {},
    }
    env0 = os.environ.copy()
    observed = {}
    for name, extra in modes.items():
        env = env0.copy()
        env.update(extra)
        out, _ = run([args.babyc, str(SOURCE)], env)
        if out != native_out:
            raise RuntimeError(f"{name} output mismatch: {out!r} != {native_out!r}")
        observed[name] = out

    samples = {name: [] for name in modes}
    # Alternate order every round to reduce systematic order/cache bias.
    for i in range(args.runs):
        order = list(modes.items())
        if i % 2:
            order.reverse()
        for name, extra in order:
            env = env0.copy()
            env.update(extra)
            out, ms = run([args.babyc, str(SOURCE)], env)
            if out != native_out:
                raise RuntimeError(f"{name} output changed during run: {out!r}")
            samples[name].append(ms)

    med = {name: statistics.median(vals) for name, vals in samples.items()}
    speedup = med["shape_disabled"] / med["shape_enabled"]
    result = {
        "source": str(SOURCE.relative_to(ROOT)),
        "runs_per_mode": args.runs,
        "reference_output": native_out,
        "outputs": observed,
        "median_ms": med,
        "min_ms": {name: min(vals) for name, vals in samples.items()},
        "max_ms": {name: max(vals) for name, vals in samples.items()},
        "samples_ms": samples,
        "disabled_over_enabled_speedup": speedup,
        "interpretation": "end-to-end wall clock; shape-disabled is a control, not a separate implementation",
    }
    print(json.dumps(result, indent=2))
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(result, indent=2) + "\n")
    # Correctness is the hard gate. Timing is reported, not used as a flaky CI gate.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
