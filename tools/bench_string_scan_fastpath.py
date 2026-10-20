#!/usr/bin/env python3
"""Paired end-to-end benchmark and differential check for char-pointer scans."""
import argparse
import json
import os
import pathlib
import statistics
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = (ROOT / "bench" / "cases" / "micro" / "string_scan.c", ROOT / "bench" / "cases" / "micro" / "string_scan_bounded.c")


def run(argv, env, timeout=30):
    t0 = time.perf_counter()
    p = subprocess.run(argv, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, timeout=timeout)
    ms = (time.perf_counter() - t0) * 1000.0
    if p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {argv}\n{p.stderr[-1200:]}")
    return p.stdout.strip(), p.stderr, ms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--babyc", default=str(ROOT / "src" / "babyc"))
    ap.add_argument("--runs", type=int, default=9)
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    if args.runs < 3:
        ap.error("--runs must be >= 3")
    base_env = os.environ.copy()
    base_env.pop("BABYC_NO_SHAPE_FP", None)
    base_env["BABYC_FP_STATS"] = "1"
    results = []
    with tempfile.TemporaryDirectory(prefix="babyc-string-scan-bench-") as td:
        for source in SOURCES:
            native = pathlib.Path(td) / (source.stem + "_native")
            cc = subprocess.run(["gcc", "-O2", str(source), "-o", str(native)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if cc.returncode:
                raise RuntimeError(f"GCC reference failed for {source.name}: {cc.stderr}")
            expected, _, _ = run([str(native)], base_env)
            modes = {"enabled": {"BABYC_ACCEL": "1"}, "disabled": {"BABYC_ACCEL": "0", "BABYC_NO_SHAPE_FP": "1"}}
            samples = {k: [] for k in modes}
            hits = {}
            for mode, extra in modes.items():
                env = base_env.copy(); env.update(extra)
                out, err, ms = run([args.babyc, str(source)], env)
                if out != expected:
                    raise RuntimeError(f"{source.name}/{mode}: output {out!r} != GCC {expected!r}")
                marker = "ptrscan_pred_hit="
                hitline = next((line for line in err.splitlines() if marker in line), "")
                if not hitline:
                    raise RuntimeError(f"{source.name}/{mode}: missing FP stats")
                hit = int(hitline.split(marker, 1)[1].split()[0])
                if (mode == "enabled" and hit <= 0) or (mode == "disabled" and hit != 0):
                    raise RuntimeError(f"{source.name}/{mode}: unexpected fast-path hit count {hit}")
                hits[mode] = hit
                samples[mode].append(ms)
            for i in range(args.runs):
                order = list(modes.items())
                if i % 2:
                    order.reverse()
                for mode, extra in order:
                    env = base_env.copy(); env.update(extra)
                    out, _, ms = run([args.babyc, str(source)], env)
                    if out != expected:
                        raise RuntimeError(f"{source.name}/{mode}: output changed during timed run")
                    samples[mode].append(ms)
            med = {k: statistics.median(v) for k, v in samples.items()}
            results.append({
                "source": str(source.relative_to(ROOT)),
                "reference_output": expected,
                "runs_per_mode_including_warmup": args.runs + 1,
                "fastpath_hits": hits,
                "median_ms": med,
                "min_ms": {k: min(v) for k, v in samples.items()},
                "max_ms": {k: max(v) for k, v in samples.items()},
                "samples_ms": samples,
                "disabled_over_enabled_speedup": med["disabled"] / med["enabled"],
            })
    result = {"method": "paired alternating end-to-end wall clock; native GCC output oracle; timing is descriptive, not a CI gate", "results": results}
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.json:
        pathlib.Path(args.json).write_text(rendered + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        print("string-scan-benchmark: FAIL: " + str(exc), file=__import__("sys").stderr)
        raise SystemExit(1)
