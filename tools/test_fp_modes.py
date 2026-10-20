#!/usr/bin/env python3
"""W37 FP differential: every src/tests/9[5-9]*_fp_*.c must match its gcc-derived
.expect in all execution modes (default, linear VM, no-kernel/no-shape, token path)."""
import glob, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
babyc = sys.argv[sys.argv.index('--babyc') + 1] if '--babyc' in sys.argv else os.path.join(root, 'src', 'babyc')
MODES = {
    'default': {},
    'vm': {'BABYC_PATH': 'linear', 'BABYC_EXEC': 'vm'},
    'honest': {'BABYC_ACCEL': '0', 'BABYC_NO_SHAPE_FP': '1', 'BABYC_NO_KERNEL': '1'},
    'token': {'BABYC_PATH': 'token', 'BABYC_NO_IR': '1'},
}
bad = 0; n = 0
for c in sorted(glob.glob(os.path.join(root, 'src', 'tests', '95*_fp_*.c'))):
    exp = open(c[:-2] + '.expect').read()
    for m, env in MODES.items():
        e = dict(os.environ); e.update(env)
        r = subprocess.run([babyc, c], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           text=True, env=e, timeout=60)
        n += 1
        if r.stdout != exp:
            bad += 1
            print('FAIL %s [%s]' % (os.path.basename(c), m))
print('fp-modes: %d/%d passed' % (n - bad, n))
sys.exit(1 if bad else 0)
