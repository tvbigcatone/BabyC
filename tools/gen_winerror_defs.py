#!/usr/bin/env python3
"""Generate BabyC-visible numeric Win32 error macros from libWINAPI's werror.h."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = root / "src" / "winapi" / "include" / "werror.h"
target = root / "src" / "cstdlib_ext" / "winerror_defs.h"
pattern = re.compile(r"^\s*#define\s+((?:ERROR_[A-Za-z0-9_]+)|NO_ERROR)\s+([^/\s]+)")
rows = []
for line in source.read_text(encoding="utf-8").splitlines():
    match = pattern.match(line)
    if not match:
        continue
    literal = match.group(2).rstrip("uUlL")
    rows.append((match.group(1), int(literal, 0)))
if len({name for name, _ in rows}) != len(rows):
    raise SystemExit("duplicate Win32 error macro names in werror.h")
with target.open("w", encoding="utf-8", newline="\n") as output:
    output.write("/* Generated from winapi/include/werror.h; keep Win32 error constants visible to BabyC. */\n")
    output.write("static const char WinErrorDefs[] =\n")
    for name, value in rows:
        output.write('    "#define %s %u\\n"\n' % (name, value))
    output.write("    ;\n")
print("generated %d Win32 error constants: %s" % (len(rows), target))
