#!/usr/bin/env python3
"""Guard shared compile-metadata accessors in active Linear consumers."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
checks = {
    "src/ir_lin_emit.c": ("CompLocalNDims", "CompLocalDims", "CompLocalLeafEs", "CompLocalElemIsPtr"),
    "src/ir_lin_ptr.c": ("CompLocalNDims", "CompLocalDims", "CompLocalLeafEs", "CompLocalElemIsPtr"),
}
errors = []
for rel, fields in checks.items():
    path = ROOT / rel
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("extern "):
            continue
        for field in fields:
            # A direct table subscript in a consumer is a bypass; writes belong
            # only in storage stamping/metadata initialization, not these files.
            if re.search(rf"\b{field}\s*\[", line):
                errors.append(f"{rel}:{number}: direct {field}[] access: {stripped}")
if errors:
    print("Indexed metadata helper audit FAILED")
    print("\n".join(errors))
    raise SystemExit(1)
print("Indexed metadata helper audit OK: Linear emit/pointer consumers use shared accessors for dims, leaf size and pointer-element classification")
