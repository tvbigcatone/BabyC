#!/usr/bin/env python3
"""Suite regression check for the character-pointer scan fast path.

Product default: shape FP ON (must hit and match GCC).
Honest control: BABYC_NO_SHAPE_FP=1 (must match GCC, zero hits).
Timings are not gated here.
"""
import argparse
import os
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = (ROOT / "bench" / "cases" / "micro" / "string_scan.c", ROOT / "bench" / "cases" / "micro" / "string_scan_bounded.c")


def run(argv, env, timeout=30):
    p = subprocess.run(argv, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, timeout=timeout)
    if p.returncode != 0:
        raise RuntimeError(f"command failed ({p.returncode}): {argv}\n{p.stderr[-1200:]}")
    return p.stdout.strip(), p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--babyc", default=str(ROOT / "src" / "babyc"))
    args = ap.parse_args()
    base_env = os.environ.copy()
    # This test owns the environment: inherited honest-benchmark flags must not
    # disable the product fast path whose hit counter is the assertion here.
    base_env.pop("BABYC_NO_KERNEL", None)
    base_env.pop("BABYC_ACCEL", None)
    base_env.pop("BABYC_NO_SHAPE_FP", None)
    base_env.pop("BABYC_SHAPE_FP", None)
    base_env["BABYC_FP_STATS"] = "1"
    checked = []
    with tempfile.TemporaryDirectory(prefix="babyc-string-scan-") as td:
        for source in SOURCES:
            native = pathlib.Path(td) / (source.stem + "_native")
            cc = subprocess.run(["gcc", "-O2", str(source), "-o", str(native)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if cc.returncode:
                raise RuntimeError(f"GCC reference failed for {source.name}: {cc.stderr}")
            expected, _ = run([str(native)], base_env)

            # Product path (accelerations ON)
            enabled_out, enabled_err = run([args.babyc, str(source)], base_env)
            m = re.search(r"ptrscan_pred_hit=(\d+)", enabled_err)
            if enabled_out != expected:
                raise RuntimeError(f"{source.name}: accel output {enabled_out!r} != native {expected!r}")
            if not m or int(m.group(1)) <= 0:
                raise RuntimeError(f"{source.name}: product path must hit shape FP; stderr={enabled_err[-500:]!r}")

            # Honest control (accelerations OFF for measurement)
            disabled_env = base_env.copy()
            disabled_env["BABYC_ACCEL"] = "0"
            disabled_env["BABYC_NO_SHAPE_FP"] = "1"
            disabled_out, disabled_err = run([args.babyc, str(source)], disabled_env)
            md = re.search(r"ptrscan_pred_hit=(\d+)", disabled_err)
            if disabled_out != expected:
                raise RuntimeError(f"{source.name}: no-accel output {disabled_out!r} != native {expected!r}")
            if not md or int(md.group(1)) != 0:
                raise RuntimeError(f"{source.name}: NO_SHAPE control unexpectedly hit; stderr={disabled_err[-500:]!r}")
            checked.append(source.name)
    print("string-scan-fastpath: PASS (native diff, product hit, NO_SHAPE control): " + ", ".join(checked))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        print("string-scan-fastpath: FAIL: " + str(exc), file=__import__("sys").stderr)
        raise SystemExit(1)
