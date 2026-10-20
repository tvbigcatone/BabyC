#!/usr/bin/env python3
"""Run the BabyC expected-output suite in bounded, reportable tranches.

Mirrors run_suite_dual.sh command selection/output normalization while making
progress and failures explicit. Exit status is based on expected-output
mismatch/timeouts, not the child process status (some negative tests expect a
non-zero exit while still producing the documented diagnostic).
"""
import argparse
import os
import re
import subprocess
import sys
import time
import tempfile
import shutil
from pathlib import Path


def test_key(path):
    match = re.match(r"^(\d+)", path.name)
    return (int(match.group(1)) if match else 99999, path.name)


def normalize_diagnostics(text):
    lines = []
    for line in text.replace("\r", "").splitlines():
        if re.match(r"^\s+\^\s*$", line):
            continue
        lines.append(re.sub(r"^([^:]+:\d+):\d+\s+", r"\1: ", line))
    return "\n".join(lines).rstrip()


def run_mode(binary, tests_dir, mode, timeout, batch_size):
    # The suite contains tests that create/overwrite files in their working
    # directory (e.g. stdio fixtures). Keep each mode in a fresh snapshot so
    # IR/TOKEN/SHADOW0 runs cannot affect each other's expected outputs.
    with tempfile.TemporaryDirectory(prefix=f"babyc-suite-{mode.lower()}-") as temp_dir:
        isolated_tests = Path(temp_dir) / "tests"
        shutil.copytree(tests_dir, isolated_tests)
        return _run_mode_isolated(binary, isolated_tests, mode, timeout, batch_size)


def _run_mode_isolated(binary, tests_dir, mode, timeout, batch_size):
    env = os.environ.copy()
    if mode == "TOKEN_ONLY":
        env["BABYC_NO_IR"] = "1"
        env.pop("BABYC_IR_SHADOW", None)
    elif mode == "IR_SHADOW0":
        env["BABYC_IR_SHADOW"] = "0"
        env.pop("BABYC_NO_IR", None)
    else:
        env.pop("BABYC_NO_IR", None)
        env.pop("BABYC_IR_SHADOW", None)

    files = sorted(
        [p for p in tests_dir.glob("*.c") if p.with_suffix(".expect").exists()],
        key=test_key,
    )
    failures = []
    timeouts = []
    batch_pass = batch_fail = batch_timeout = 0
    started = time.monotonic()

    for index, source in enumerate(files, 1):
        expected_path = source.with_suffix(".expect")
        if mode == "TOKEN_ONLY" and source.with_suffix(".expect.noir").exists():
            expected_path = source.with_suffix(".expect.noir")
        expected = expected_path.read_text().replace("\r", "")

        if "analyze" in source.stem.lower():
            command = [str(binary), "--analyze", str(source)]
        elif "args" in source.stem.lower():
            command = [str(binary), str(source), "-", "arg1", "arg2", "arg3", "arg4"]
        else:
            command = [str(binary), str(source)]

        try:
            result = subprocess.run(
                command, cwd=str(tests_dir), env=env,
                capture_output=True, text=True, timeout=timeout,
            )
            returncode = result.returncode
            actual = ((result.stdout or "") + (result.stderr or "")).replace("\r", "")
            actual = "".join(
                line for line in actual.splitlines(True)
                if not any(tag in line for tag in
                           ("[babyc-ir]", "[lex-stats]", "[babyc]", "[babyc-vm]", "[babyc-cov]"))
            )
        except subprocess.TimeoutExpired:
            actual = "TIMEOUT\n"
            returncode = -1
            timeouts.append(source.stem)
            batch_timeout += 1

        matched = (
            actual == expected
            or actual.rstrip() == expected.rstrip()
            or normalize_diagnostics(actual) == normalize_diagnostics(expected)
        )
        # Empty .expect files represent silent success. Output-only matching
        # otherwise lets crashes and early nonzero exits pass as expected.
        if not expected.strip() and returncode != 0:
            matched = False
        if matched:
            batch_pass += 1
        elif source.stem not in timeouts:
            failures.append(source.stem)
            batch_fail += 1

        if index % batch_size == 0 or index == len(files):
            first = index - ((index - 1) % batch_size)
            print(
                f"{mode} tranche {first}-{index}: PASS={batch_pass} FAIL={batch_fail} "
                f"TIMEOUT={batch_timeout}; cumul PASS={index-len(failures)-len(timeouts)}/"
                f"{index}; elapsed={time.monotonic()-started:.1f}s",
                flush=True,
            )
            batch_pass = batch_fail = batch_timeout = 0

    elapsed = time.monotonic() - started
    print(
        f"{mode} SUMMARY PASS={len(files)-len(failures)-len(timeouts)} "
        f"FAIL={len(failures)} TIMEOUT={len(timeouts)} TOTAL={len(files)} "
        f"SECONDS={elapsed:.1f}", flush=True,
    )
    if failures:
        print(f"{mode} FAILURES: {' '.join(failures)}", flush=True)
    if timeouts:
        print(f"{mode} TIMEOUTS: {' '.join(timeouts)}", flush=True)
    return bool(failures or timeouts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--babyc", type=Path, default=Path(__file__).resolve().parents[1] / "babyc")
    parser.add_argument("--tests", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--batch-size", type=int, default=100)
    parser.add_argument("--mode", choices=("IR", "TOKEN_ONLY", "IR_SHADOW0"), default="IR")
    args = parser.parse_args()
    binary = args.babyc.resolve()
    tests_dir = args.tests.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f"BabyC executable not found or not executable: {binary}")
    return 1 if run_mode(binary, tests_dir, args.mode, args.timeout, args.batch_size) else 0


if __name__ == "__main__":
    sys.exit(main())
