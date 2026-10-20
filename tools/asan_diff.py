#!/usr/bin/env python3
"""Run the whole suite under the ASan/UBSan build twice (Irb off / on) and list the sanitizer
findings that appear ONLY with Irb on.  (PicoC has many benign pre-existing UBSan reports:
misaligned union access, hash shifts; only the difference matters.)
usage: tools/asan_build.sh && python3 tools/asan_diff.py [/tmp/babyc_asan]"""
import glob, os, re, subprocess, sys
base = sys.argv[1] if len(sys.argv) > 1 else '/tmp/babyc_asan'
exe = os.path.join(base, 'src', 'picoc_asan')
os.chdir(os.path.join(base, 'src', 'tests'))
def run(c, off):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=0')
    if off: env['BABYC_NO_IRB'] = '1'
    args = [exe, c]
    if c.startswith('032_args') or c.startswith('078_args_double_ptr'): args += ['-', 'arg1', 'arg2', 'arg3', 'arg4']
    try:
        out = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60, env=env).stdout.decode('latin1')
    except subprocess.TimeoutExpired:
        return {'TIMEOUT'}
    f = set()
    for l in out.split('\n'):
        m = re.match(r'(\S+?:\d+):\d+: runtime error: (.*)', l)
        if m: f.add(m.group(1) + ' ' + re.sub(r'0x[0-9a-f]+', 'ADDR', m.group(2))[:60])
        if 'ERROR: AddressSanitizer' in l: f.add(re.sub(r'==\d+==|0x[0-9a-f]+|pc .*', '', l).strip()[:80])
    return f
new = []
for c in sorted(glob.glob('*.c')):
    if os.path.exists(c[:-2] + '.expect'):
        extra = run(c, False) - run(c, True)
        if extra: new.append((c, sorted(extra)))
print('tests with sanitizer findings ONLY when Irb is on:', len(new))
for c, e in new: print(' ', c, e)
