#!/usr/bin/env python3
"""Regression: an empty .expect must not hide a nonzero program exit."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "src" / "tests" / "run_suite_tranches.py"
DEFAULT_BABYC = ROOT / "src" / "babyc"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--babyc", type=Path, default=DEFAULT_BABYC)
    args = parser.parse_args()
    binary = args.babyc.resolve()
    if not binary.is_file():
        parser.error(f"BabyC binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="babyc-empty-expect-") as tmp:
        tests = Path(tmp)
        (tests / "000_nonzero.c").write_text(
            "int main(void) { return 1; }\n", encoding="utf-8"
        )
        (tests / "000_nonzero.expect").write_text("", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(RUNNER), "--babyc", str(binary),
             "--tests", str(tests), "--mode", "IR", "--timeout", "5"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=20,
        )
        if result.returncode == 0 or "FAIL=1" not in result.stdout:
            print(result.stdout, end="")
            print("FAIL: nonzero exit with empty expectation was accepted", file=sys.stderr)
            return 1
        print("PASS: empty expectation + nonzero exit is rejected")
        return 0


if __name__ == "__main__":
    sys.exit(main())
