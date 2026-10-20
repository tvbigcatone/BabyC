#!/bin/bash
# A/B benchmark on the same binary: Irb off (BABYC_NO_IRB=1) vs on.  Best of 5 runs, seconds.
# usage: tools/bench/ab.sh sieve m_for_arr_read ...      (default: all kernels)
HERE="$(cd "$(dirname "$0")" && pwd)"
P="${BABYC_BIN:-$HERE/../../src/picoc}"
K="${@:-sieve m_for_arr_write m_for_arr_read m_for_step m_for_if matrix structs ptr arr_only scalar}"
for k in $K; do
  for mode in off on; do
    best=999
    for i in 1 2 3 4 5; do
      if [ $mode = off ]; then export BABYC_NO_IRB=1; else unset BABYC_NO_IRB; fi
      s=$(date +%s.%N); (cd "$HERE" && "$P" $k.c >/dev/null 2>&1); e=$(date +%s.%N)
      d=$(echo "$e - $s" | bc); best=$(echo "if ($d < $best) $d else $best" | bc)
    done
    echo "$k irb=$mode best=${best}s"
  done
done
echo "reference (python3, same sieve):"
best=999; for i in 1 2 3; do s=$(date +%s.%N); python3 "$HERE/sieve.py" >/dev/null; e=$(date +%s.%N); d=$(echo "$e - $s"|bc); best=$(echo "if ($d < $best) $d else $best"|bc); done; echo "python3 sieve best=${best}s"
