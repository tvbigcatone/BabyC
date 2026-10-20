#!/bin/bash
# Dual suite: IR + NO_IR. Exit non-zero on any fail.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BABYC="${BABYC:-$ROOT/babyc}"
TESTS="$ROOT/tests"
cd "$TESTS"

python3 - <<'PY'
import os, re, subprocess, sys
from pathlib import Path
babyc = Path(os.environ.get("BABYC", "../babyc")).resolve()
files = sorted(
    [p for p in Path(".").glob("*.c") if p.with_suffix(".expect").exists()],
    key=lambda p: (int(m.group(1)) if (m := re.match(r"^(\d+)", p.name)) else 99999, p.name),
)

def norm_diag(s):
    out = []
    for ln in s.replace("\r", "").splitlines():
        if re.match(r"^\s+\^\s*$", ln):
            continue
        out.append(re.sub(r"^([^:]+:\d+):\d+\s+", r"\1: ", ln))
    return "\n".join(out).rstrip()

def run_pass(extra, name):
    env = os.environ.copy()
    env.update(extra)
    failed, passed = [], 0
    for f in files:
        want = f.with_suffix(".expect").read_text().replace("\r", "")
        if env.get("BABYC_NO_IR") and (e2 := f.with_suffix(".expect.noir")).exists():
            want = e2.read_text().replace("\r", "")
        cmd = [str(babyc), "--analyze", str(f)] if "analyze" in f.stem.lower() else [str(babyc), str(f)]
        if "args" in f.stem.lower():
            cmd = [str(babyc), str(f), "-", "arg1", "arg2", "arg3", "arg4"]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=45, env=env)
            out = ((r.stdout or "") + (r.stderr or "")).replace("\r", "")
            out = "".join(l for l in out.splitlines(True) if "[babyc-ir]" not in l and "[lex-stats]" not in l)
        except Exception as e:
            out = str(e)
        ok = out == want or out.rstrip() == want.rstrip() or norm_diag(out) == norm_diag(want)
        if ok:
            passed += 1
        else:
            failed.append(f.stem)
    print(f"{name}: PASSED={passed} FAILED={len(failed)} TOTAL={len(files)}")
    if failed:
        print(" ", " ".join(failed[:40]))
    return len(failed)

rc = run_pass({}, "A-IR") + run_pass({"BABYC_NO_IR": "1"}, "B-NO_IR")
# F2: pure-IR without shadow dual-write (BABYC_SUITE_SHADOW0=1 or always)
if os.environ.get("BABYC_SUITE_SHADOW0", "1") == "1":
    rc += run_pass({"BABYC_IR_SHADOW": "0"}, "C-IR-SHADOW0")
sys.exit(1 if rc else 0)
PY
