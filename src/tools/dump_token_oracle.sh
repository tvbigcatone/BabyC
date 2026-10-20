#!/bin/bash
# Fase 0: generate token dumps for test sources (oracle).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BABYC="${BABYC:-$ROOT/babyc}"
OUTDIR="${1:-$ROOT/token_oracle}"
mkdir -p "$OUTDIR"
export BABYC_TOKEN_DUMP=1
export BABYC_NO_IR=1
# dump without running main when possible: still parses
n=0
for f in "$ROOT"/tests/*.c; do
  base=$(basename "$f" .c)
  # capture only tok lines
  "$BABYC" --analyze "$f" 2>"$OUTDIR/$base.tok" >/dev/null || true
  # keep only tok lines
  if grep -q '^tok ' "$OUTDIR/$base.tok" 2>/dev/null; then
    grep '^tok ' "$OUTDIR/$base.tok" > "$OUTDIR/$base.tok.tmp" && mv "$OUTDIR/$base.tok.tmp" "$OUTDIR/$base.tok"
    n=$((n+1))
  else
    rm -f "$OUTDIR/$base.tok"
  fi
done
echo "dumped $n token streams to $OUTDIR"
