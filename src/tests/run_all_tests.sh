#!/bin/bash
# Dual-pass suite: A=default IR, B=BABYC_NO_IR=1 (token path). Irb retired (TOKEN_PATH_V2 P1).
set -u
BABYC="${1:-../babyc}"
TESTDIR="${2:-.}"
cd "$TESTDIR" || exit 1
[[ -x "$BABYC" ]] || BABYC="../babyc"
BABYC="$(cd "$(dirname "$BABYC")" && pwd)/$(basename "$BABYC")"

python3 - "$BABYC" <<'PY'
import os, re, subprocess, sys
from pathlib import Path
babyc = Path(sys.argv[1])
testdir = Path('.').resolve()
def test_key(name):
    m = re.match(r'^(\d+)', name)
    return (int(m.group(1)) if m else 99999, name)
files = sorted([p for p in testdir.glob('*.c') if p.with_suffix('.expect').exists()],
               key=lambda p: test_key(p.name))
def run_pass(env_extra, passname, track_ir_miss=False):
    env = os.environ.copy(); env.update(env_extra)
    if track_ir_miss:
        env['BABYC_IR_STATS'] = '1'
    passed = failed = 0; failed_list = []
    ir_funcs = ir_miss = 0
    print('-'*60); print(f' Pass {passname}'); print('-'*60)
    for f in files:
        base = f.stem; exp = f.with_suffix('.expect')
        # Win32 / 32-bit pointer width: optional .expect.win32
        import struct as _st
        if _st.calcsize('P') == 4:
            alt = f.with_suffix('.expect.win32')
            if not alt.exists():
                alt = Path(str(f.with_suffix('')) + '.expect.win32')
            if alt.exists():
                exp = alt
        cmd = [str(babyc)]
        if 'analyze' in base.lower():
            cmd += ['--analyze', str(f)]
        elif 'args' in base.lower():
            cmd += [str(f), '-', 'arg1', 'arg2', 'arg3', 'arg4']
        else:
            cmd += [str(f)]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=60, env=env)
            err = (r.stderr or '').replace('\r','')
            out = (r.stdout or '').replace('\r','')
            # strip IR stats from compared output; accumulate miss
            kept_err = []
            for line in err.splitlines(True):
                if '[babyc-ir]' in line:
                    m = re.search(r'ir=(\d+)\s+token=(\d+)', line)
                    if m:
                        ir_funcs += int(m.group(1))
                        ir_miss += int(m.group(2))
                else:
                    kept_err.append(line)
            out = (out + ''.join(kept_err))
        except subprocess.TimeoutExpired:
            out = 'TIMEOUT\n'
        want = exp.read_text().replace('\r','')
        if env.get('BABYC_NO_IR') and (e2 := f.with_suffix('.expect.noir')).exists():
            want = e2.read_text().replace('\r','')
        def norm_diag(s):
            lines = s.replace('\r','').splitlines()
            out_l = []
            for ln in lines:
                if re.match(r'^\s+\^\s*$', ln):
                    continue
                ln2 = re.sub(r'^([^:]+:\d+):\d+\s+', r'\1: ', ln)
                out_l.append(ln2)
            return '\n'.join(out_l).rstrip()
        ok = (out == want or out.rstrip() == want.rstrip()
              or norm_diag(out) == norm_diag(want))
        if ok:
            passed += 1
        else:
            failed += 1; failed_list.append(base); print(f'FAIL {base}')
    print(f'Pass {passname}: PASSED={passed} FAILED={failed}')
    if track_ir_miss:
        tot = ir_funcs + ir_miss
        pct = (100.0 * ir_funcs / tot) if tot else 0.0
        print(f' IR body coverage: ir_funcs={ir_funcs}  token_fallback_miss={ir_miss}  ({pct:.0f}% ir)')
        print(' (miss = functions compiled to token path, not Body-IR)')
    if failed_list: print('Failed:', ' '.join(failed_list))
    return failed
fa = run_pass({}, 'A-IR', track_ir_miss=True)
print()
fb = run_pass({'BABYC_NO_IR':'1'}, 'B-NO_IR')
print(); print(f'DUAL-PASS SUMMARY fails={int(fa>0)+int(fb>0)}')
# Benchmarks after suite
import pathlib
bench = pathlib.Path(babyc).resolve().parent.parent / 'bench' / 'run_bench.py'
if not bench.is_file():
    bench = pathlib.Path(babyc).resolve().parent / 'bench' / 'run_bench.py'
print()
print('='*60)
print(' BENCHMARKS')
print('='*60)
bench_rc = 0
if not bench.is_file():
    print(' WARN: bench/run_bench.py not found — skip')
else:
    print(f' babyc : {babyc}')
    print(f' bench : {bench.parent}')
    env = os.environ.copy()
    env['BABYC'] = str(babyc)
    env.pop('BABYC_NO_IR', None)
    r = subprocess.run([sys.executable, str(bench)], cwd=str(bench.parent), env=env)
    bench_rc = r.returncode
    print(' BENCHMARKS done.' if bench_rc == 0 else f' BENCHMARKS rc={bench_rc}')
sys.exit(0 if fa==0 and fb==0 and bench_rc==0 else 1)
PY
