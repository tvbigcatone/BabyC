#!/bin/bash
# Generate a csmith program restricted for BabyC and try to run it.
# Focus: bitfield subset when CSMITH_BITFIELDS=1.
#
# Usage:
#   tools/csmith_babyc.sh [seed]
#   CSMITH_BITFIELDS=1 tools/csmith_babyc.sh [seed]
#   BABYC=/path/to/babyc tools/csmith_babyc.sh ...
#
# Exit 0 on clean run or intentional div/mod-by-zero (ProgramFail).
# Exit 1 on unexpected failure.
set -e
SEED="${1:-$RANDOM}"
OUT="/tmp/csmith_babyc_$$.c"
PREP="$OUT.prep"

if ! command -v csmith >/dev/null; then
  echo "csmith not installed (apt install csmith)"; exit 1
fi

if [ "${CSMITH_BITFIELDS:-0}" = 1 ]; then
  # Tighter limits when bitfields are enabled (layout + IR path stress)
  CSMITH_FLAGS=(
    --seed "$SEED"
    --bitfields
    --no-argc --no-float --no-longlong
    --max-funcs 2
    --max-block-depth 2
    --max-expr-complexity 3
    --no-checksum
    --no-safe-math
    --no-volatiles --no-consts
    --no-arrays
    --no-jumps
    --concise
  )
else
  CSMITH_FLAGS=(
    --seed "$SEED"
    --no-bitfields
    --no-argc --no-float --no-longlong
    --max-funcs 3
    --max-block-depth 3
    --max-array-dim 1
    --max-expr-complexity 3
    --no-checksum
    --no-safe-math
    --no-consts
    --no-volatiles
    --no-volatile-pointers
    --no-arrays
    --no-jumps
    --concise
  )
fi

csmith "${CSMITH_FLAGS[@]}" -o "$OUT" 2>/dev/null || true
if [ ! -s "$OUT" ]; then
  echo "csmith produced empty file"; exit 1
fi

# Inject minimal stubs; strip csmith.h, NO_LONGLONG redefs, volatile, #pragma pack
{
  echo '/* minimal csmith stubs for BabyC */'
  echo 'typedef unsigned int uint32_t;'
  echo 'typedef int int32_t;'
  echo 'typedef unsigned short uint16_t;'
  echo 'typedef short int16_t;'
  echo 'typedef unsigned char uint8_t;'
  echo 'typedef signed char int8_t;'
  echo '#define NO_LONGLONG'
  echo 'void platform_main_begin(void) {}'
  echo 'void platform_main_end(uint32_t x, int y) { (void)x; (void)y; }'
  sed -e '/csmith\.h/d' \
      -e '/#define NO_LONGLONG/d' \
      -e 's/volatile //g' \
      -e 's/static long __undefined;/static int __undefined;/' \
      -e '/csmith_sink_/d' \
      -e '/#pragma/d' -e 's/\bconst //g' \
      "$OUT"
  echo 'uint32_t csmith_sink_;'
} > "$PREP"
mv "$PREP" "$OUT"

BABYC="${BABYC:-./src/babyc}"
if [ ! -x "$BABYC" ]; then
  # common fallback when built in src/
  if [ -x "./src/babyc" ]; then BABYC="./src/babyc"
  elif [ -x "/tmp/babyc" ]; then BABYC="/tmp/babyc"
  fi
fi

echo "=== seed $SEED  (bitfields=${CSMITH_BITFIELDS:-0}) ==="
OUTF="$OUT.out"
set +e
"$BABYC" "$OUT" >"$OUTF" 2>&1
RC=$?
set -e

if [ $RC -eq 0 ]; then
  echo "RUN OK"
  # suppress empty platform noise
  if [ -s "$OUTF" ]; then cat "$OUTF"; fi
  echo "file: $OUT"
  exit 0
fi

# Intentional ProgramFail (div/mod 0) counts as soft success for fuzzing
if grep -qE 'modulo by zero|division by zero' "$OUTF"; then
  echo "SOFT OK (div/mod zero — expected ProgramFail)"
  head -5 "$OUTF"
  echo "file: $OUT"
  exit 0
fi


echo "RUN FAIL:"
head -20 "$OUTF"
echo "file: $OUT"
exit 1
