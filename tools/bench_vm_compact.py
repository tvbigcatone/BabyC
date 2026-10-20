#!/usr/bin/env python3
"""Repeatable throughput smoke benchmark for an actually executed compact VM loop.

This measures end-to-end interpreter startup + parse/lower + execution. It does
not claim to isolate execution-only cost. Every compact sample is accepted only
when the production compact VM reports runs >= 1 and output matches the oracle.
"""
import argparse, os, re, statistics, subprocess, time
from pathlib import Path

COUNTERS = re.compile(r"\[babyc-bytecode-vm-prod\] attempts=(\d+) runs=(\d+)")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--babyc', default='src/babyc')
    ap.add_argument('--rounds', type=int, default=9)
    ap.add_argument('--source', default='src/tests/849_vm_compact_loop_bench.c')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = Path(args.babyc).resolve()
    source = (root / args.source).resolve()
    expected = source.with_suffix('.expect').read_text().strip()
    modes = [('tree', {'BABYC_PATH':'tree'}), ('linear', {'BABYC_PATH':'linear'}),
             ('compact', {'BABYC_PATH':'bytecode','BABYC_VM_PROFILE':'1'})]
    samples = {name: [] for name, _ in modes}
    order = list(modes)
    for r in range(args.rounds + 1):
        # First pass is warm-up; rotate order to reduce systematic bias.
        rotated = order[r % len(order):] + order[:r % len(order)]
        for name, extra in rotated:
            env = os.environ.copy()
            env.pop('BABYC_BYTECODE_VM', None); env.pop('BABYC_EXEC', None); env.pop('BABYC_VM_PROFILE', None)
            env.update(extra)
            start = time.perf_counter()
            p = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
            elapsed = time.perf_counter() - start
            if p.returncode != 0 or p.stdout.strip() != expected:
                raise SystemExit(f'FAIL {name}: rc={p.returncode} output={p.stdout.strip()!r} expected={expected!r}\n{p.stderr}')
            if name == 'compact':
                m = COUNTERS.search(p.stderr)
                attempts, runs = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
                if attempts < 1 or runs < 1:
                    raise SystemExit(f'FAIL compact benchmark did not execute compact VM: attempts={attempts}, runs={runs}\n{p.stderr}')
            if r:
                samples[name].append(elapsed)
    print(f'source={source.relative_to(root)} rounds={args.rounds} (plus one warm-up) expected={expected!r}')
    print('metric=end-to-end wall time; includes startup, parsing/lowering, and execution')
    for name, values in samples.items():
        print(f'{name}: median_ms={statistics.median(values)*1000:.3f} min_ms={min(values)*1000:.3f} max_ms={max(values)*1000:.3f} n={len(values)} samples_ms=' + ','.join(f'{v*1000:.3f}' for v in values))
if __name__ == '__main__': main()
