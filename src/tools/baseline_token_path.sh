#!/bin/bash
# Fase 0 baseline: wall times for token path kernels (no -pg)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BABYC="${BABYC:-$ROOT/babyc}"
BENCH="$ROOT/bench"
RUNS="${RUNS:-5}"
export BABYC_NO_IR=1
export BABYC_TOKEN_FAST=0
# clear IR stats noise
unset BABYC_IR_STATS BABYC_FP_STATS 2>/dev/null || true

kernels=(isort string_proc dfs_rec fib struct_score array_sum)
echo "=== TOKEN_PATH_V2 Fase 0 baseline ==="
echo "babyc=$BABYC  RUNS=$RUNS  BABYC_NO_IR=1 TOKEN_FAST=0"
echo

for k in "${kernels[@]}"; do
  src="$BENCH/${k}.c"
  if [[ ! -f "$src" ]]; then
    echo "SKIP $k (missing $src)"
    continue
  fi
  times=()
  for i in $(seq 1 "$RUNS"); do
    # wall time via bash SECONDS is coarse; use date +%s.%N
    t0=$(date +%s.%N)
    out=$("$BABYC" "$src" 2>/dev/null)
    t1=$(date +%s.%N)
    dt=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.4f", b-a}')
    times+=("$dt")
  done
  # min / avg
  min=$(printf '%s\n' "${times[@]}" | sort -n | head -1)
  avg=$(printf '%s\n' "${times[@]}" | awk '{s+=$1} END{printf "%.4f", s/NR}')
  echo "$k  min=${min}s  avg=${avg}s  runs=${times[*]}"
done
echo
echo "=== size (full vs micro if present) ==="
size "$BABYC" 2>/dev/null || true
[[ -x "$ROOT/picoc" ]] && size "$ROOT/picoc" 2>/dev/null || true
