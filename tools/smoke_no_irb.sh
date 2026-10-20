#!/bin/bash
# Smoke: token + Body-IR path only (Body-IR off / pure token). Critical dual-path cases.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BABYC:-${BABYC_BIN:-}}"
if [ -z "$BIN" ]; then
  for c in "$ROOT/src/babyc" /tmp/babyc /tmp/picoc; do
    if [ -x "$c" ]; then BIN="$c"; break; fi
  done
fi
if [ ! -x "$BIN" ]; then
  echo "no babyc binary"; exit 2
fi
export BABYC_NO_IR=1
TD="$ROOT/src/tests"
# subset: assignment, loops, multi-dim, bitfields, matrix-like
TESTS=(
  001_assignment.c
  011_pointer.c
  103_return_in_for.c
  492_bitfield_layout.c
  493_bitfield_rw.c
  508_bitfield_index_inc.c
  513_bitfield_array_loop.c
  514_bitfield_union_init.c
)
# optional bench matrix if present
FAIL=0
PASS=0
cd "$TD"
for t in "${TESTS[@]}"; do
  exp="${t%.c}.expect"
  [ -f "$exp" ] || continue
  out="$("$BIN" "$t" 2>&1 || true)"
  # normalize
  got=$(printf '%s' "$out" | tr -d '\r' | grep -v '\[babyc-ir' | sed 's/[[:space:]]*$//')
  want=$(tr -d '\r' < "$exp" | sed 's/[[:space:]]*$//')
  if [ "$got" = "$want" ]; then
    echo "PASS $t"
    PASS=$((PASS+1))
  else
    echo "FAIL $t"
    FAIL=$((FAIL+1))
  fi
done
# matrix 2D (no expect file in tests/)
if [ -f "$ROOT/tools/bench/matrix.c" ]; then
  o=$("$BIN" "$ROOT/tools/bench/matrix.c" 2>&1) || true
  if echo "$o" | grep -q 696000; then
    echo "PASS matrix.c (NO_IR)"
    PASS=$((PASS+1))
  else
    echo "FAIL matrix.c (NO_IR): $o"
    FAIL=$((FAIL+1))
  fi
fi
echo "smoke_no_ir: PASS=$PASS FAIL=$FAIL"
exit $FAIL
