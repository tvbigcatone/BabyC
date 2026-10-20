#!/usr/bin/env python3
"""Rigenera MANIFEST_SHA256.txt: header invariato, poi sha256 di ogni file sorgente
(src/**, tools/**, docs/**, bench/** ; esclusi binari, .o, __pycache__).
Verifica:  sha256sum -c MANIFEST_SHA256.txt  (le righe '#' vanno filtrate: grep -v '^#')."""
import hashlib, os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(root, 'MANIFEST_SHA256.txt')
header = []
if os.path.exists(out):
    for l in open(out, encoding='utf-8'):
        if l.startswith('## Critical SHA256'):
            break
        header.append(l)
SKIP_EXT = ('.o', '.a', '.exe', '.pyc', '.so')
SKIP_BIN = {'src/babyc', 'babyc'}
rows = []
for top in ('src', 'tools', 'docs', 'bench'):
    for d, ds, fs in os.walk(os.path.join(root, top)):
        ds[:] = [x for x in ds if x != '__pycache__']
        for f in fs:
            p = os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/')
            if f.endswith(SKIP_EXT) or p in SKIP_BIN:
                continue
            rows.append((p, hashlib.sha256(open(os.path.join(root, p), 'rb').read()).hexdigest()))
rows.sort()
with open(out, 'w', encoding='utf-8', newline='\n') as fh:
    fh.writelines(header)
    fh.write('## Critical SHA256\n')
    for p, h in rows:
        fh.write('%s  %s\n' % (h, p))
print('manifest: %d file' % len(rows))
