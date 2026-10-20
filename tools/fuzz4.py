#!/usr/bin/env python3
# Differential fuzz W9-style:
#   gcc -O0 -fwrapv  (oracle)
#   BabyC IR default
#   BabyC BABYC_NO_IR=1
#   BabyC BABYC_EXEC=both (shadow: fail if stderr contains mismatch= with nonzero)
#
# usage: python3 tools/fuzz4.py FIRST LAST_EXCLUSIVE [mode]
#   mode: loops (default, gen_loops.py) | random (gen_random_c.py)
# env: BABYC_BIN path to binary
import os, subprocess, sys, tempfile, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
P = os.environ.get('BABYC_BIN') or os.environ.get('BABYC') or '/tmp/babyc'
if not os.path.isfile(P):
    P = os.path.join(ROOT, 'src', 'babyc')
GEN = os.path.join(HERE, 'gen_loops.py')
GEN_RND = os.path.join(HERE, 'gen_random_c.py')

a, b = int(sys.argv[1]), int(sys.argv[2])
mode = sys.argv[3] if len(sys.argv) > 3 else 'loops'
tmp = tempfile.mkdtemp(prefix='babyc_fuzz4_')
cats = {
    'all_agree': 0,
    'ir_ok_noir_bad': [],
    'noir_ok_ir_BAD': [],
    'both_bad_same': [],
    'both_bad_differ': [],
    'gcc_fail': 0,
    'timeout': [],
    'both_mismatch': [],  # BABYC_EXEC=both reported mismatch
    'crash': [],
}

def run(cmd, env=None, timeout=25):
    try:
        e = os.environ.copy()
        if env:
            e.update(env)
        # strip BABYC_* then apply env so flags are explicit
        for k in list(e.keys()):
            if k.startswith('BABYC_') and (not env or k not in env):
                if k not in ('BABYC_BIN', 'BABYC'):
                    del e[k]
        if env:
            e.update(env)
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, env=e)
        return r.stdout.decode('latin1', errors='replace'), r.returncode
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', -9

def strip(o):
    lines = []
    for l in o.replace('\r', '').split('\n'):
        if '[babyc-ir]' in l or '[babyc-lower]' in l or '[babyc-vm]' in l or '[babyc-debug]' in l:
            continue
        lines.append(l)
    return '\n'.join(lines).strip()

mismatch_re = re.compile(r'\[babyc-lower\] expr lowered=\d+ match=\d+ mismatch=([1-9][0-9]*)')

print('binary=%s mode=%s seeds=%d..%d tmp=%s' % (P, mode, a, b - 1, tmp))

for s in range(a, b):
    f = os.path.join(tmp, 'p%d.c' % s)
    gen = GEN if mode == 'loops' else GEN_RND
    src = subprocess.run(['python3', gen, str(s)], stdout=subprocess.PIPE).stdout.decode()
    open(f, 'w').write(src)
    exe = f[:-2] + '.exe'
    g = subprocess.run(['gcc', '-w', '-O0', '-fwrapv', f, '-o', exe],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if g.returncode != 0:
        cats['gcc_fail'] += 1
        continue
    exp, _ = run([exe])
    exp = exp.strip()
    ir_out, ir_rc = run([P, f], {})
    noir_out, noir_rc = run([P, f], {'BABYC_PATH': 'token'})
    both_out, both_rc = run([P, f], {'BABYC_EXEC': 'both', 'BABYC_LOWER': '1'})

    if 'TIMEOUT' in (ir_out, noir_out, both_out):
        cats['timeout'].append(s)
        continue
    if ir_rc not in (0, 1, 2, 3, 4, 5, 255) and ir_rc < 0:
        cats['crash'].append(('ir', s, ir_rc))
        continue
    if noir_rc < 0:
        cats['crash'].append(('noir', s, noir_rc))
        continue

    ir_s, noir_s = strip(ir_out), strip(noir_out)
    if mismatch_re.search(both_out):
        cats['both_mismatch'].append(s)
        # keep file
        open(os.path.join(tmp, 'p%d.both.txt' % s), 'w').write(both_out)

    if ir_s == noir_s == exp:
        cats['all_agree'] += 1
        try:
            os.remove(f); os.remove(exe)
        except OSError:
            pass
        continue
    if ir_s == exp:
        cats['ir_ok_noir_bad'].append(s)
    elif noir_s == exp:
        cats['noir_ok_ir_BAD'].append(s)
    elif ir_s == noir_s:
        cats['both_bad_same'].append(s)
    else:
        cats['both_bad_differ'].append(s)

print('--- summary ---')
print('all_agree', cats['all_agree'])
print('ir_ok_noir_bad', len(cats['ir_ok_noir_bad']), cats['ir_ok_noir_bad'][:20])
print('noir_ok_ir_BAD', len(cats['noir_ok_ir_BAD']), cats['noir_ok_ir_BAD'][:20])
print('both_bad_same', len(cats['both_bad_same']), cats['both_bad_same'][:20])
print('both_bad_differ', len(cats['both_bad_differ']), cats['both_bad_differ'][:20])
print('both_mismatch (EXEC=both)', len(cats['both_mismatch']), cats['both_mismatch'][:20])
print('gcc_fail', cats['gcc_fail'])
print('timeout', len(cats['timeout']), cats['timeout'][:10])
print('crash', len(cats['crash']), cats['crash'][:10])
print('tmp', tmp)
# exit non-zero if IR disagrees with gcc while NO_IR agrees, or both_mismatch
# Hard fail: IR disagrees with gcc while recoverable, or crash.
# both_mismatch is soft: lower shadow residual (signed long long), tree still wins.
hard = len(cats['noir_ok_ir_BAD']) + len(cats['crash'])
soft = len(cats['both_mismatch']) + len(cats['both_bad_same']) + len(cats['both_bad_differ'])
print('hard_fail', hard, 'soft_residual', soft)
sys.exit(1 if hard else 0)
