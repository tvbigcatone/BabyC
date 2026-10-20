#!/usr/bin/env python3
"""Characterize float/double representation across BabyC execution paths.

This is an architecture-gap probe, intentionally separate from run_suite.py:
its first cases are known mismatches in the current baseline. It exits nonzero
when any BabyC mode differs from native GCC.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BABYC = pathlib.Path(sys.argv[sys.argv.index('--babyc') + 1] if '--babyc' in sys.argv else ROOT / 'src' / 'babyc').resolve()
SOURCE = r'''#include <stdio.h>
static float addf(float x) { return x + 1.0f; }
static double addd(double x) { return x + 1.0; }
static float idf(float x) { return x; }
static double idd(double x) { return x; }
struct Mixed { char c; float f; char d; double g; };
int main(void) {
    float f = 16777216.0f;
    double d = 16777216.0;
    float fa[3];
    double da[3];
    struct Mixed m;
    fa[0] = 1.25f; fa[1] = 2.5f; fa[2] = 4.0f;
    da[0] = 1.25; da[1] = 2.5; da[2] = 4.0;
    m.c = 1; m.f = 1.5f; m.d = 2; m.g = 3.5;
    printf("size %u %u %u %u\n", (unsigned)sizeof(float), (unsigned)sizeof(double),
           (unsigned)sizeof(fa), (unsigned)sizeof(da));
    printf("round %d %d %d %d\n", f + 1.0f == f, d + 1.0 == d,
           addf(f) == f, addd(d) == d);
    printf("calls %.2f %.2f %.2f %.2f\n", idf(1.25f), idd(1.25), addf(2.0f), addd(2.0));
    printf("arrays %.2f %.2f %.2f / %.2f %.2f %.2f\n",
           fa[0], fa[1], fa[2], da[0], da[1], da[2]);
    printf("layout %u %.2f %.2f\n", (unsigned)sizeof(m), m.f, m.g);
    return 0;
}
'''
MODES = {
    'tree/default': {},
    'linear-vm': {'BABYC_PATH': 'linear'},
    'token': {'BABYC_PATH': 'token'},
    'compact-enabled': {'BABYC_PATH': 'bytecode'},
}

def main():
    with tempfile.TemporaryDirectory(prefix='babyc-fp-audit-') as td:
        td = pathlib.Path(td)
        cfile = td / 'fp_arch_probe.c'
        native = td / 'native'
        cfile.write_text(SOURCE)
        cc = subprocess.run(['gcc', '-std=c11', '-O0', str(cfile), '-o', str(native)],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if cc.returncode:
            print(cc.stderr, file=sys.stderr)
            return 2
        expected = subprocess.run([str(native)], text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, check=True).stdout
        print('GCC reference:')
        print(expected, end='')
        expected_lines = expected.splitlines()
        sections = ('size/layout', 'binary32 rounding', 'calls/returns', 'array values', 'struct layout')
        failures = 0
        for name, overrides in MODES.items():
            env = os.environ.copy(); env.update(overrides)
            run = subprocess.run([str(BABYC), str(cfile)], env=env, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            ok = run.returncode == 0 and run.stdout == expected
            print(f'[{"PASS" if ok else "FAIL"}] {name}: rc={run.returncode}')
            if run.stdout != expected:
                actual_lines = run.stdout.splitlines()
                for index, section in enumerate(sections):
                    exp_line = expected_lines[index] if index < len(expected_lines) else '<missing>'
                    got_line = actual_lines[index] if index < len(actual_lines) else '<missing>'
                    if exp_line != got_line:
                        print(f'  [{section}] expected={exp_line!r} actual={got_line!r}')
                if len(actual_lines) != len(expected_lines):
                    print(f'  [output shape] expected {len(expected_lines)} lines, got {len(actual_lines)}')
                failures += 1
            if run.returncode != 0 and run.stdout == expected:
                failures += 1
                print('  stderr:', run.stderr[-500:])
        print(f'FP architecture probe: {len(MODES)-failures}/{len(MODES)} modes match GCC')
        return 1 if failures else 0

if __name__ == '__main__':
    raise SystemExit(main())
