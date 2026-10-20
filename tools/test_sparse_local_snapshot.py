#!/usr/bin/env python3
"""Regression test for compact VM rollback snapshots on sparse, large frames."""
import os
from pathlib import Path
import re
import subprocess
import sys

COUNTERS = re.compile(r"\[babyc-bytecode-vm-prod\] attempts=(\d+) runs=(\d+)")


def main():
    root = Path(__file__).resolve().parents[1]
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else "src/babyc")
    if not binary.is_absolute():
        binary = (Path.cwd() / binary).resolve()
    source = root / "src/tests/850_vm_sparse_local_frame.c"
    expected = source.with_suffix(".expect").read_text().strip()
    modes = (
        ("linear", {"BABYC_PATH": "linear", "BABYC_EXEC": "vm"}),
        ("bytecode", {"BABYC_PATH": "bytecode", "BABYC_EXEC": "vm", "BABYC_BYTECODE_VM": "1", "BABYC_VM_PROFILE": "1"}),
        ("strict", {"BABYC_PATH": "bytecode", "BABYC_STRICT": "1", "BABYC_EXEC": "vm_strict", "BABYC_BYTECODE_VM": "1", "BABYC_VM_PROFILE": "1"}),
    )
    for name, extra in modes:
        env = os.environ.copy()
        env.pop("BABYC_BYTECODE_VM", None); env.pop("BABYC_EXEC", None); env.pop("BABYC_VM_STRICT", None)
        env.pop("BABYC_VM_PROFILE", None)
        env.update(extra)
        proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                              capture_output=True, text=True, timeout=20)
        if proc.returncode != 0 or proc.stdout.strip() != expected:
            print(f"FAIL sparse-local-frame {name}: rc={proc.returncode}, "
                  f"stdout={proc.stdout.strip()!r}, expected={expected!r}", file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1
        if name == "bytecode":
            match = COUNTERS.search(proc.stderr)
            attempts, runs = (int(match.group(1)), int(match.group(2))) if match else (0, 0)
            if attempts < 1 or runs < 1:
                print(f"FAIL sparse-local-frame {name}: compact path not verified; "
                      f"attempts={attempts}, runs={runs}", file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
        if name == "strict" and "vm_strict: tree fallback" in proc.stderr:
            print("FAIL sparse-local-frame strict unexpectedly used tree fallback", file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1
    print("sparse-local-snapshot: PASS (linear/compact/strict output, compact execution confirmed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
