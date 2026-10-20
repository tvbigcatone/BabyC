#!/usr/bin/env python3
"""Fail if canonical runtime diagnostic literals reappear outside diagnostic.c.

This is a source-level guard for the central message catalog. It intentionally
ignores tests, vendored code, platform headers, and documentation.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1] / "src"
CANONICAL = {
    '"NULL pointer dereference"',
    '"uninitialized variable"',
    '"uninitialized variable \'%s\'"',
    '"division by zero"',
    '"modulo by zero"',
    '"array index out of bounds"',
    '"array index %d out of bounds (size %d)"',
    '"out of memory"',
    '"null function pointer"',
    '"function is undefined"',
}
EXCLUDED = {"tests", "third_party", "winapi"}
failures = []
for path in ROOT.rglob("*.c"):
    if path.name == "diagnostic.c" or any(part in EXCLUDED for part in path.parts):
        continue
    text = path.read_text(errors="replace")
    # Exact C literal matching avoids treating prose/comments as bypasses.
    for literal in CANONICAL:
        if literal in text:
            failures.append(f"{path.relative_to(ROOT.parent)}: raw canonical literal {literal}")
if failures:
    print("Diagnostic catalog violations:")
    print("\n".join(failures))
    sys.exit(1)
print(f"Diagnostic catalog OK: {len(CANONICAL)} canonical literals are confined to src/diagnostic.c")
