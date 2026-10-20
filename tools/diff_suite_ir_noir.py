#!/usr/bin/env python3
"""Compare IR vs NO_IR suite outputs; ignore caret-column-only diffs."""
import os, re, subprocess, sys
from pathlib import Path

def norm(s: str) -> str:
    # drop caret lines and column numbers in "file:line:col"
    lines = []
    for ln in s.replace('\r', '').splitlines():
        if re.match(r'^\s*\^\s*$', ln):
            continue
        ln = re.sub(r'(:\d+):\d+\b', r'\1', ln)  # file:line:col -> file:line
        ln = re.sub(r'\s+', ' ', ln).strip()
        if ln:
            lines.append(ln)
    return '\n'.join(lines)

def main():
    babyc = Path(sys.argv[1] if len(sys.argv) > 1 else 'src/babyc').resolve()
    testdir = Path(sys.argv[2] if len(sys.argv) > 2 else 'src/tests').resolve()
    files = sorted(p for p in testdir.glob('*.c') if p.with_suffix('.expect').exists())
    equal = caret = real = 0
    real_list = []
    caret_list = []
    for f in files:
        def run(env):
            e = os.environ.copy(); e.update(env)
            r = subprocess.run([str(babyc), str(f)], capture_output=True, text=True,
                               env=e, timeout=45)
            return (r.stdout or '') + (r.stderr or '')
        try:
            out_ir = run({})
            out_no = run({'BABYC_PATH': 'token'})
        except Exception as ex:
            real += 1
            real_list.append((f.name, f'exception {ex}'))
            continue
        if out_ir == out_no:
            equal += 1
            continue
        if norm(out_ir) == norm(out_no):
            caret += 1
            caret_list.append(f.name)
            continue
        real += 1
        real_list.append((f.name, f'IR={out_ir[:100]!r} | NOIR={out_no[:100]!r}'))
    print(f'compared={len(files)} equal={equal} caret_only={caret} real_diff={real}')
    if caret_list:
        print('caret_only_sample:', ' '.join(caret_list[:20]))
    if real_list:
        print('real_diff:')
        for n, m in real_list:
            print(f'  {n}: {m}')
    return 0 if real == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
