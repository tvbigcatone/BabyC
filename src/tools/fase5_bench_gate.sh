#!/bin/bash
# F5 gate: IR bench times + global bind hit rate
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BABYC="${BABYC:-$ROOT/babyc}"
echo "=== F5 bench gate $(date -Iseconds) ==="
echo "babyc=$BABYC"
for b in array_sum isort string_proc matmul_small; do
  f="$ROOT/bench/$b.c"
  [ -f "$f" ] || { echo "SKIP $b"; continue; }
  times=()
  for i in 1 2 3; do
    t=$(/usr/bin/time -f '%e' "$BABYC" "$f" >/dev/null 2>/tmp/t_wall)
    # time writes to stderr of time, captured wrong - fix:
    t=$( { /usr/bin/time -f '%e' "$BABYC" "$f" >/dev/null; } 2>&1 )
    times+=("$t")
  done
  min=${times[0]}
  for t in "${times[@]}"; do
    awk -v a="$t" -v b="$min" 'BEGIN{exit !(a+0<b+0)}' && min=$t
  done
  echo "IR $b  min=${min}s  runs=${times[*]}"
done
echo "=== bind stats (one run each) ==="
for b in array_sum isort string_proc; do
  f="$ROOT/bench/$b.c"
  [ -f "$f" ] || continue
  out=$(BABYC_GLOBAL_BIND_STATS=1 "$BABYC" "$f" 2>&1 >/dev/null)
  echo "$b: $out"
done
