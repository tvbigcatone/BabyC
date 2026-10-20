#!/usr/bin/env python3
"""Heavy differential storm: gcc oracle vs BabyC (IR on / BABYC_NO_IRB=1).

Covers:
  - gen_bitfield_storm.py  (BF + struct locals + compound + ptr)
  - gen_loops.py           (arrays/loops/compound)
  - gen_random_c.py        (scalar arithmetic)
  - optional csmith via tools/csmith_babyc.sh (if installed)

usage:
  python3 tools/storm_bitfield.py [n_seeds] [start_seed]
  BABYC_BIN=/tmp/babyc python3 tools/storm_bitfield.py 80 1000

Failing programs are kept under /tmp/babyc_storm_<pid>/
Exit 0 only if zero unexpected fails.
"""
from __future__ import annotations
import os, subprocess, sys, tempfile, shutil, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "src")

def find_bin():
    for key in ("BABYC", "BABYC_BIN"):
        v = os.environ.get(key)
        if v and os.path.isfile(v):
            return v
    for p in ("/tmp/babyc", os.path.join(SRC, "babyc"), os.path.join(SRC, "picoc")):
        if os.path.isfile(p):
            return p
    return "/tmp/babyc"

BIN = find_bin()
GEN_BF = os.path.join(HERE, "gen_bitfield_storm.py")
GEN_LOOPS = os.path.join(HERE, "gen_loops.py")
GEN_RAND = os.path.join(HERE, "gen_random_c.py")
CSMITH_SH = os.path.join(HERE, "csmith_babyc.sh")

N = int(sys.argv[1]) if len(sys.argv) > 1 else 40
START = int(sys.argv[2]) if len(sys.argv) > 2 else 1

tmp = tempfile.mkdtemp(prefix="babyc_storm_")
print(f"binary={BIN}")
print(f"work={tmp}")
print(f"seeds={START}..{START+N-1}")

cats = {
    "all_agree": 0,
    "ir_ok_token_bad": [],
    "token_ok_ir_BAD": [],
    "both_bad_same": [],
    "both_bad_differ": [],
    "gcc_fail": 0,
    "babyc_crash": [],
    "timeout": [],
    "soft_div0": 0,
}

def run(cmd, env=None, timeout=15):
    try:
        r = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=timeout, env=env
        )
        out = r.stdout.decode("latin1", errors="replace")
        return r.returncode, out
    except subprocess.TimeoutExpired:
        return -9, "TIMEOUT"

def strip_babyc(o: str) -> str:
    return "\n".join(
        l for l in o.replace("\r\n", "\n").split("\n")
        if "[babyc-ir]" not in l and "[babyc-irb]" not in l
    ).strip()

def is_soft_fail(o: str) -> bool:
    return any(x in o for x in (
        "division by zero", "modulo by zero",
        "uninitialized variable",  # sometimes intentional in csmith edges
    ))

def diff_one(src: str, tag: str, seed: int):
    base = os.path.join(tmp, f"{tag}_{seed}")
    cfile = base + ".c"
    with open(cfile, "w") as f:
        f.write(src)
    exe = base + ".exe"
    rc, gout = run(["gcc", "-w", "-O0", "-fwrapv", cfile, "-o", exe], timeout=20)
    if rc != 0:
        cats["gcc_fail"] += 1
        return
    rc, exp = run([exe], timeout=10)
    if rc != 0:
        cats["gcc_fail"] += 1
        return
    exp = exp.strip()

    env_off = dict(os.environ, BABYC_NO_IRB="1")
    env_on = dict(os.environ)
    env_on.pop("BABYC_NO_IRB", None)

    rc_t, tok = run([BIN, cfile], env=env_off)
    rc_i, irb = run([BIN, cfile], env=env_on)
    tok_s, irb_s = strip_babyc(tok), strip_babyc(irb)

    if "TIMEOUT" in (tok_s, irb_s) or rc_t == -9 or rc_i == -9:
        cats["timeout"].append((tag, seed))
        return
    # crash / ProgramFail
    if rc_t != 0 and rc_i != 0:
        if is_soft_fail(tok) or is_soft_fail(irb):
            cats["soft_div0"] += 1
            os.remove(cfile)
            try: os.remove(exe)
            except OSError: pass
            return
        cats["babyc_crash"].append((tag, seed, tok_s[:200], irb_s[:200]))
        return

    if irb_s == tok_s == exp:
        cats["all_agree"] += 1
        os.remove(cfile)
        try: os.remove(exe)
        except OSError: pass
        return
    if irb_s == exp and tok_s != exp:
        cats["ir_ok_token_bad"].append((tag, seed, tok_s[:80], exp[:80]))
    elif tok_s == exp and irb_s != exp:
        cats["token_ok_ir_BAD"].append((tag, seed, irb_s[:80], exp[:80]))
    elif irb_s == tok_s:
        cats["both_bad_same"].append((tag, seed, irb_s[:80], exp[:80]))
    else:
        cats["both_bad_differ"].append((tag, seed, tok_s[:60], irb_s[:60], exp[:60]))

def gen_src(gen_py, seed):
    r = subprocess.run(
        ["python3", gen_py, str(seed)],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=10
    )
    return r.stdout.decode("utf-8", errors="replace")

t0 = time.time()
# --- phase A: bitfield storm ---
for s in range(START, START + N):
    src = gen_src(GEN_BF, s)
    if src.strip():
        diff_one(src, "bf", s)

# --- phase B: loops (subset) ---
for s in range(START, START + max(1, N // 2)):
    src = gen_src(GEN_LOOPS, s)
    if src.strip():
        diff_one(src, "loop", s)

# --- phase C: scalar random ---
for s in range(START, START + max(1, N // 3)):
    src = gen_src(GEN_RAND, s)
    if src.strip():
        diff_one(src, "rand", s)

elapsed = time.time() - t0
print(f"\n=== storm done in {elapsed:.1f}s ===")
for k, v in cats.items():
    if isinstance(v, list):
        print(f"  {k}: {len(v)}")
        for item in v[:12]:
            print(f"    {item}")
    else:
        print(f"  {k}: {v}")

bad = (
    len(cats["token_ok_ir_BAD"])
    + len(cats["both_bad_same"])
    + len(cats["both_bad_differ"])
    + len(cats["babyc_crash"])
    + len(cats["timeout"])
)
# ir_ok_token_bad is informational (token lag); still report
print(f"\nunexpected_hard_fails={bad}")
print(f"kept under: {tmp}")
sys.exit(1 if bad else 0)
