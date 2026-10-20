#!/usr/bin/env python3
"""Differential fuzz: gcc (oracle, -fwrapv) vs BabyC token path (BABYC_NO_IRB=1) vs BabyC with Irb.
usage: python3 tools/fuzz_run.py FIRST_SEED LAST_SEED_EXCLUSIVE
Programs come from tools/gen_loops.py.  Classification (gcc is the judge):
  all_agree          gcc == token == irb
  irb_ok_token_bad   irb == gcc, token != gcc   (a token-path bug; Irb is right)
  token_ok_irb_BAD   token == gcc, irb != gcc   (an Irb bug -> investigate first)
  both_bad_same      irb == token != gcc
  both_bad_differ    irb != token, both != gcc  (inconclusive until the token-path bugs are fixed)
Failing programs are kept in the temp dir that is printed at the end."""
import os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
P = os.environ.get('BABYC_BIN', os.path.join(HERE, '..', 'src', 'picoc'))
G = os.path.join(HERE, 'gen_loops.py')
a, b = int(sys.argv[1]), int(sys.argv[2])
tmp = tempfile.mkdtemp(prefix='babyc_fuzz_')
cats = {'all_agree': 0, 'irb_ok_token_bad': [], 'token_ok_irb_BAD': [], 'both_bad_same': [],
        'both_bad_differ': [], 'gcc_fail': 0, 'timeout': []}

def run(cmd, env=None):
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20, env=env)
        return r.stdout.decode('latin1')
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'

def strip(o):
    return '\n'.join(l for l in o.split('\n') if '[babyc-ir]' not in l).strip()

for s in range(a, b):
    f = os.path.join(tmp, 'p%d.c' % s)
    open(f, 'w').write(subprocess.run(['python3', G, str(s)], stdout=subprocess.PIPE).stdout.decode())
    exe = f[:-2] + '.exe'
    if subprocess.run(['gcc', '-w', '-O0', '-fwrapv', f, '-o', exe], stdout=subprocess.PIPE,
                      stderr=subprocess.STDOUT).returncode != 0:
        cats['gcc_fail'] += 1
        continue
    exp = run([exe]).strip()
    off = dict(os.environ, BABYC_NO_IRB='1')
    on = dict(os.environ); on.pop('BABYC_NO_IRB', None)
    tok, irb = strip(run([P, f], off)), strip(run([P, f], on))
    if 'TIMEOUT' in (tok, irb):
        cats['timeout'].append(s); continue
    if irb == tok == exp:
        cats['all_agree'] += 1; os.remove(f); os.remove(exe); continue
    if irb == exp: cats['irb_ok_token_bad'].append(s)
    elif tok == exp: cats['token_ok_irb_BAD'].append(s)
    elif irb == tok: cats['both_bad_same'].append(s)
    else: cats['both_bad_differ'].append(s)
print('seeds %d..%d, failing programs kept in %s' % (a, b - 1, tmp))
for k, v in cats.items():
    print(' ', k, ':', v if not isinstance(v, list) else (len(v), v[:20]))
