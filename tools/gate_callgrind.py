#!/usr/bin/env python3
"""G1 instruction budget gate: callgrind Ir counts vs W18d +3%.

usage:
  python3 tools/gate_callgrind.py [path/to/babyc]
  BABYC=/tmp/babyc python3 tools/gate_callgrind.py

Exit 0 if all known benches <= W18d*1.03 (or marked WIN).
Exit 1 if any OVER (unless BABYC_GATE_ALLOW_OVER=1).
"""
import os, sys, subprocess, tempfile

# W18d reference (Ir millions) — from W18d callgrind baseline
W18D = {
    'fibc': 768, 'structs': 1386, 'isort': 1114, 'collatz': 1240,
    'gcd': 718, 'sieve': 2222, 'strproc': 654, 'list': 1206,
    'crc': 50, 'mandel': 977, 'matmul': 972,
}
BUDGET = 1.03  # +3%

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
BIN = os.environ.get('BABYC') or os.environ.get('BABYC_BIN') or sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'src', 'babyc')

def find_bench(name):
    for sub in ('serie1', 'serie2', ''):
        p = os.path.join(ROOT, 'bench', sub, name + '.c') if sub else os.path.join(ROOT, 'bench', name + '.c')
        if os.path.isfile(p):
            return p
    return None

def callgrind_ir(binpath, src):
    out = tempfile.mktemp(prefix='cg_', suffix='.out')
    try:
        subprocess.run(
            ['valgrind', '--tool=callgrind', f'--callgrind-out-file={out}', binpath, src],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        with open(out) as f:
            for line in f:
                if line.startswith('summary:'):
                    return int(line.split()[1]) / 1e6
    except Exception as e:
        print('error', src, e, file=sys.stderr)
        return None
    finally:
        try: os.unlink(out)
        except: pass
    return None

def main():
    if not os.path.isfile(BIN) or not os.access(BIN, os.X_OK):
        print('error: binary not executable:', BIN, file=sys.stderr)
        return 2
    allow = os.environ.get('BABYC_GATE_ALLOW_OVER') == '1'
    overs = []
    print(f'{"Bench":12s} {"W18d":>8s} {"Now":>8s} {"Δ%":>8s} status')
    for name, ref in sorted(W18D.items()):
        src = find_bench(name)
        if not src:
            print(f'{name:12s} missing bench'); continue
        m = callgrind_ir(BIN, src)
        if m is None:
            print(f'{name:12s} FAIL measure'); overs.append(name); continue
        delta = (m - ref) / ref * 100
        if m < ref:
            st = 'WIN'
        elif m <= ref * BUDGET:
            st = 'OK'
        else:
            st = 'OVER'
            overs.append(name)
        print(f'{name:12s} {ref:8.1f} {m:8.1f} {delta:+7.1f}% {st}')
    if overs and not allow:
        print('G1 FAIL:', ', '.join(overs), file=sys.stderr)
        return 1
    if overs:
        print('G1 ALLOW_OVER:', ', '.join(overs))
    else:
        print('G1 PASS')
    return 0

if __name__ == '__main__':
    sys.exit(main())
