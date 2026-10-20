#!/bin/bash
# Fase 4: token-path cost snapshot (NO_IR) after Fase 3b
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BABYC="${BABYC:-$ROOT/babyc}"
export BABYC_NO_IR=1 BABYC_TOKEN_FAST=0
echo "=== Fase4 measure $(date -Iseconds) ==="
echo "babyc=$BABYC"
if [[ -n "${BABYC_LEX_STATS:-}" ]] || true; then
  :
fi
# Build note: compile with -DBABYC_LEX_STATS for GetToken counts
for b in isort string_proc array_sum fib dfs_rec struct_score; do
  f="$ROOT/bench/$b.c"
  [[ -f "$f" ]] || { echo "SKIP $b"; continue; }
  times=()
  for i in 1 2 3; do
    t=$( { time -p "$BABYC" "$f" >/dev/null; } 2>&1 | awk '/^real/{print $2}')
    times+=("$t")
  done
  # min
  min=${times[0]}
  for t in "${times[@]}"; do
    awk -v a="$t" -v b="$min" 'BEGIN{if(a<b) exit 0; exit 1}' && min=$t
  done
  echo "$b  min=${min}s  runs=${times[*]}"
done
echo "=== size ==="
size "$BABYC" 2>/dev/null || ls -la "$BABYC"
