#!/usr/bin/env python3
"""Run the numbered src/tests corpus with .expect files under one engine mode.

Examples:
  python3 tools/run_numbered_matrix.py --path tree --accel 0 --log docs/reports/matrix_tree_a0.log
  python3 tools/run_numbered_matrix.py --path linear --strict --accel 1 --log docs/reports/matrix_linear_a1_strict.log

Unlike run_suite.py, this gate is limited to numbered cases with .expect files;
source-only fixtures used by dedicated tests remain outside this comparison matrix.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def norm(text: str, source_name: str = "") -> str:
    ignored = (
        "[babyc-ir]", "[babyc-irb]", "[babyc]", "[babyc-vm]", "[babyc-cov]",
        "[babyc-bytecode-vm]", "[babyc-exec-profile]", "[babyc-canonical]", "[babyc-canonical-invalid]",
    )
    normalized = "\n".join(
        line for line in text.replace("\r\n", "\n").split("\n")
        if not any(tag in line for tag in ignored)
    ).strip()
    # This legacy fixture prints wall-clock CPU ticks as part of its output.
    # That field is inherently nondeterministic and is not semantic behavior;
    # preserve the fixed .expect while comparing the rest of the transcript.
    if source_name == "535_main_continue_after_extract.c":
        normalized = re.sub(
            r"(Tempo esecuzione\s*:\s*)[0-9]+(\s*ms)",
            r"\1<TIME>\2", normalized,
        )
    return normalized


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--path", choices=("tree", "linear"), required=True)
    ap.add_argument("--strict", action="store_true", help="require Linear STRICT (only valid with --path linear)")
    ap.add_argument("--accel", choices=("0", "1"), required=True)
    ap.add_argument("--log", help="save complete failures, actual/expected snippets, and progress to this path")
    ap.add_argument("--require-no-tree-fallback", action="store_true",
                    help="Linear non-STRICT gate: enable BABYC_COV and fail if any case reports tree_body > 0")
    ns = ap.parse_args()
    if ns.strict and ns.path != "linear":
        ap.error("--strict is only valid with --path linear")
    if ns.require_no_tree_fallback and (ns.path != "linear" or ns.strict):
        ap.error("--require-no-tree-fallback requires --path linear without --strict")

    project = Path(__file__).resolve().parent.parent
    tests = project / "src" / "tests"
    binary = project / "src" / "babyc"
    if not binary.is_file():
        print(f"ERROR: binary not found: {binary}", file=sys.stderr)
        return 2

    cases = []
    for source in tests.glob("*.c"):
        if not re.match(r"^\d+_", source.name):
            continue
        expected = source.with_suffix(".expect")
        if expected.is_file():
            cases.append((source, expected))
    cases.sort(key=lambda pair: (int(pair[0].name.split("_", 1)[0]), pair[0].name))

    env = os.environ.copy()
    # Neutralize legacy/experimental selectors, keeping only the requested matrix axes.
    for key in (
        "BABYC_NO_IR", "BABYC_EXEC", "BABYC_VM_STRICT", "BABYC_NO_SHAPE_FP",
        "BABYC_NO_KERNEL", "BABYC_BYTECODE_VM", "BABYC_PATH", "BABYC_STRICT",
        "BABYC_ACCEL", "BABYC_BIN", "BABYC",
    ):
        env.pop(key, None)
    env["BABYC_PATH"] = ns.path
    env["BABYC_ACCEL"] = ns.accel
    env["BABYC_CANONICAL_GATE"] = "1"
    if ns.require_no_tree_fallback:
        env["BABYC_COV"] = "1"
    coverage_gate = (ns.path == "linear" and not ns.strict
                     and env.get("BABYC_COV") == "1")
    if ns.strict:
        env["BABYC_STRICT"] = "1"

    label = f"{ns.path}_accel{ns.accel}" + ("_STRICT" if ns.strict else "")
    lines = [
        f"COMMAND: BABYC_PATH={ns.path} BABYC_STRICT={1 if ns.strict else 0} BABYC_ACCEL={ns.accel} python3 tools/run_numbered_matrix.py --path {ns.path} --accel {ns.accel}" + (" --strict" if ns.strict else "") + (" --require-no-tree-fallback" if ns.require_no_tree_fallback else ""),
        f"CONFIG {label} TOTAL={len(cases)}",
    ]
    passed = failed = timeout = skipped = 0
    failures = []
    fallback_cases = 0
    tree_body_total = 0
    cov_tests_reported = 0
    cov_totals = {"lin_inst": 0, "tree_stmt": 0, "tree_expr": 0,
                  "fncode_body": 0, "tree_body": 0}
    canonical_invalid_nodes = 0
    canonical_reports = 0
    collect_coverage = env.get("BABYC_COV") == "1"
    started = time.time()
    for index, (source, expect) in enumerate(cases, 1):
        argv = [str(binary)]
        if "analyze" in source.name:
            argv.append("--analyze")
        argv.append(source.name)
        if re.match(r"^\d+_args(?:_|\.|$)", source.name):
            argv.extend(("-", "arg1", "arg2", "arg3", "arg4"))
        try:
            tic = time.time()
            result = subprocess.run(
                argv, cwd=tests, env=env, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=(35 if source.name.endswith("_bigblock_bodyir.c") else 20),
            )
            actual = result.stdout.decode("latin1", errors="replace")
            rc = result.returncode
            elapsed = time.time() - tic
        except subprocess.TimeoutExpired:
            actual, rc = "<TIMEOUT>", -1
            elapsed = time.time() - tic
            timeout += 1

        expected = expect.read_text(encoding="latin1")
        canonical_values = re.findall(r"\[babyc-canonical\]\s+invalid_nodes=(\d+)\b", actual)
        canonical_match = re.search(r"\[babyc-canonical\]\s+invalid_nodes=(\d+)\b", actual)
        if canonical_values:
            canonical_reports += 1
            canonical_invalid_nodes += int(canonical_values[-1])
        cov_match = re.search(
            r"\[babyc-cov\]\s+lin_inst=(\d+)\s+tree_stmt=(\d+)\s+tree_expr=(\d+)\s+fncode_body=(\d+)\s+tree_body=(\d+)\b",
            actual)
        tree_body = int(cov_match.group(5)) if cov_match else 0
        if collect_coverage and cov_match:
            cov_tests_reported += 1
            for key, group in zip(("lin_inst", "tree_stmt", "tree_expr", "fncode_body", "tree_body"), cov_match.groups()):
                cov_totals[key] += int(group)
        if coverage_gate:
            tree_body_total += tree_body
        fallback_violation = coverage_gate and tree_body > 0
        output_ok = (norm(actual, source.name) == norm(expected, source.name)
                     and (norm(expected, source.name) or rc == 0))
        canonical_violation = not canonical_values or int(canonical_values[-1]) != 0
        if output_ok and not fallback_violation and not canonical_violation:
            passed += 1
        elif "can't read file" in actual and ".h" in actual and not fallback_violation:
            skipped += 1
        else:
            failed += 1
            failure_name = source.name
            if fallback_violation: failure_name += f"[tree_body={tree_body}]"
            if canonical_violation: failure_name += f"[canonical_invalid={canonical_values[-1] if canonical_values else 'missing-report'}]"
            failures.append(failure_name)
            lines.extend((
                f"FAIL {index}/{len(cases)} {source.stem} rc={rc} elapsed={elapsed:.3f}s" +
                    (f" tree_body={tree_body} (no-Tree-fallback gate)" if fallback_violation else ""),
                "  EXPECT: " + repr(norm(expected, source.name)[:1200]),
                "  ACTUAL: " + repr(norm(actual, source.name)[:1800]),
            ))
            if fallback_violation:
                fallback_cases += 1

        if index % 100 == 0 or index == len(cases):
            lines.append(
                f"CUMULATIVE {index}/{len(cases)}: PASSED={passed} FAILED={failed} TIMEOUT={timeout} SKIPPED={skipped}"
            )

    lines.extend((
        f"SUMMARY {label}: PASSED={passed} FAILED={failed} TIMEOUT={timeout} SKIPPED={skipped} TOTAL={len(cases)} SECONDS={time.time()-started:.2f}",
        f"CANONICAL_TYPE_GATE: reports={canonical_reports}/{len(cases)} invalid_nodes={canonical_invalid_nodes}",
        (f"TREE_FALLBACK_GATE: enabled violations={fallback_cases} tree_body={tree_body_total}" if coverage_gate else "TREE_FALLBACK_GATE: disabled"),
        ("COVERAGE_TOTALS: " + f"tests_reported={cov_tests_reported} "
         + " ".join(f"{k}={v}" for k, v in cov_totals.items()) if collect_coverage else "COVERAGE_TOTALS: disabled"),
        "FAILURES: " + (" ".join(failures) if failures else "none"),
    ))
    for line in lines:
        if line.startswith(("COMMAND:", "CONFIG ", "CUMULATIVE ", "SUMMARY ", "CANONICAL_TYPE_GATE:", "TREE_FALLBACK_GATE:", "COVERAGE_TOTALS:", "FAILURES:", "FAIL ")):
            print(line)
    if ns.log:
        log_path = project / ns.log
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        try:
            display_path = log_path.resolve().relative_to(project.resolve()).as_posix()
        except ValueError:
            # Absolute log paths outside the repository are supported too.
            display_path = str(log_path.resolve())
        print(f"LOG={display_path}")
    return 1 if failed or timeout or canonical_reports != len(cases) or canonical_invalid_nodes != 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
