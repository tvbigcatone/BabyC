#!/bin/bash
# Compare the error text (source line, caret, message) produced by the pure token path
# (failure on iteration 1, "*__first.c") with the one produced by Irb (failure on a later
# iteration, "*__late.c").  The two sources differ only in a data constant, so the failing
# line has the same text and the same columns.  Expected: every pair matches.
# usage: tools/diag_corpus/cmp.sh            (run from anywhere)
HERE="$(cd "$(dirname "$0")" && pwd)"
P="${BABYC_BIN:-$HERE/../../src/picoc}"
TMP="$(mktemp -d)"; ok=0; bad=0
for f in "$HERE"/*__first.c; do
  n=$(basename "$f" __first.c); [ -f "$HERE/${n}__late.c" ] || continue
  (cd "$HERE" && "$P" ${n}__first.c 2>&1 | grep -av '\[babyc-ir\]' | sed "s/${n}__first/X/") > "$TMP/$n.first"
  (cd "$HERE" && "$P" ${n}__late.c  2>&1 | grep -av '\[babyc-ir\]' | sed "s/${n}__late/X/")  > "$TMP/$n.late"
  if cmp -s "$TMP/$n.first" "$TMP/$n.late"; then ok=$((ok+1)); else bad=$((bad+1)); echo "MISMATCH $n"; diff "$TMP/$n.first" "$TMP/$n.late" | head -6; fi
done
echo "match=$ok mismatch=$bad"; rm -rf "$TMP"
