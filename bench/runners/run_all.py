#!/usr/bin/env python3
"""Launcher: run the COMPLETE bench suite (all series) with uniform columns.

  python3 bench/runners/run_all.py
  python3 bench/runners/run_all.py --runs 3
  BABYC=/path/to/babyc python3 bench/runners/run_all.py

Equivalent to:  run_bench.py --set all
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# Re-use run_bench main with forced --set all
import run_bench  # noqa: E402


def main() -> int:
    # Inject --set all unless user already passed --set
    argv = sys.argv[1:]
    if not any(a == "--set" or a.startswith("--set=") for a in argv):
        argv = ["--set", "all"] + argv
    sys.argv = [sys.argv[0]] + argv
    return run_bench.main()


if __name__ == "__main__":
    sys.exit(main())
