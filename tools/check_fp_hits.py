#!/usr/bin/env python3
"""Verify Body-IR fastpaths fire on the expected shapes.

Runs each case with BABYC_FP_STATS=1 and checks stderr [fp] counters.
Also checks stdout matches the .expect file (correctness).

Exit 0 only if all cases pass.
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BABYC = os.environ.get("BABYC", os.path.join(ROOT, "src", "babyc"))
TESTDIR = os.path.join(ROOT, "src", "tests")

# name -> dict of counter -> minimum expected hits
# counter names match IrPrintFpStats fields after [fp]
CASES = [
    # Dispatch order: Iter → Kernel → Dense → ArrAccum → ArrFill → Leaf → Score → Tight
    # Kernel often wins before ArrFill/Tight when body is kernel-ok.
    ("649_fp_array_fill_accum.c", {"kernel": 2}),           # fill + accum both kernel
    ("650_fp_tight_sum_i.c", {"kernel": 1}),                # s+=i accepted by kernel
    ("652_fp_iter_fib_call.c", {"iter": 1}),                # s += fib(const)
    ("653_fp_kernel_bound_var.c", {"kernel": 1}),           # BoundIsVar i < n
    ("654_fp_kernel_nested_index.c", {"kernel": 3}),        # nested A[i][k]
    ("655_fp_score_struct.c", {"correct_only": 1}),         # residual: score shape not matched yet
    ("656_fp_miss_ptr_walk.c", {"miss": 1, "correct_only": 1}),
    ("657_fp_leaf_call_accum.c", {"leaf": 1}),
]

FP_RE = re.compile(
    r"\[fp\] for=(?P<for>\d+) iter=(?P<iter>\d+) kernel=(?P<kernel>\d+) "
    r"dense=(?P<dense>\d+) arraccum=(?P<arraccum>\d+) arrfill=(?P<arrfill>\d+) "
    r"leaf=(?P<leaf>\d+) score=(?P<score>\d+) tight=(?P<tight>\d+) "
    r"ptrscan=(?P<ptrscan>\d+) ptrfor=(?P<ptrfor>\d+) ptrbound=(?P<ptrbound>\d+) "
    r"loopvars=(?P<loopvars>\d+) mono=(?P<mono>\d+) miss=(?P<miss>\d+)"
)

def run(src):
    env = os.environ.copy()
    env["BABYC_FP_STATS"] = "1"
    env["BABYC_NO_MEMO"] = "1"
    r = subprocess.run([BABYC, src], capture_output=True, text=True, env=env, timeout=30)
    out = (r.stdout or "").strip()
    err = r.stderr or ""
    m = FP_RE.search(err)
    stats = m.groupdict() if m else None
    return out, err, stats, r.returncode

def expect_path(src):
    base = os.path.splitext(src)[0] + ".expect"
    return os.path.join(TESTDIR, base) if not os.path.isabs(src) else base

def check(name, rules):
    path = os.path.join(TESTDIR, name)
    exp_file = os.path.splitext(path)[0] + ".expect"
    with open(exp_file) as f:
        expected = f.read().strip()
    out, err, stats, rc = run(path)
    ok = True
    msgs = []
    if out != expected:
        ok = False
        msgs.append(f"stdout mismatch got={out!r} want={expected!r}")
    if stats is None and "correct_only" not in rules:
        ok = False
        msgs.append(f"no [fp] line in stderr:\n{err[-400:]}")
    elif stats is not None:
        def n(k):
            return int(stats.get(k, 0))
        specialized = (n("iter")+n("kernel")+n("dense")+n("arraccum")+n("arrfill")+
                       n("leaf")+n("score")+n("tight")+n("ptrscan")+n("ptrfor")+
                       n("ptrbound")+n("loopvars")+n("mono"))
        for key, need in rules.items():
            if key == "correct_only":
                continue
            if key == "any_hit":
                if specialized < need:
                    ok = False
                    msgs.append(f"any_hit specialized={specialized} < {need}")
            elif key == "tight_or_kernel_or_loopvars":
                if n("tight")+n("kernel")+n("loopvars") < need:
                    ok = False
                    msgs.append(f"tight/kernel/loopvars={n('tight')+n('kernel')+n('loopvars')} < {need}")
            elif key == "kernel_or_tight":
                if n("kernel")+n("tight") < need:
                    ok = False
                    msgs.append(f"kernel+tight={n('kernel')+n('tight')} < {need}")
            elif key == "miss":
                if n("miss") < need:
                    ok = False
                    msgs.append(f"miss={n('miss')} < {need}")
            elif key == "score_or_kernel_or_leaf":
                if n("score")+n("kernel")+n("leaf") < need:
                    ok = False
                    msgs.append(f"score/kernel/leaf={n('score')+n('kernel')+n('leaf')} < {need}")
            else:
                if n(key) < need:
                    ok = False
                    msgs.append(f"{key}={n(key)} < {need}")
        msgs.append("stats " + " ".join(f"{k}={v}" for k,v in stats.items() if int(v)>0))
    return ok, msgs

def main():
    if not os.path.isfile(BABYC):
        print("babyc not found:", BABYC, file=sys.stderr)
        return 1
    failed = 0
    for name, rules in CASES:
        ok, msgs = check(name, rules)
        status = "PASS" if ok else "FAIL"
        print(f"{status} {name}")
        for m in msgs:
            print(f"  {m}")
        if not ok:
            failed += 1
    print(f"\n{len(CASES)-failed}/{len(CASES)} passed")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
