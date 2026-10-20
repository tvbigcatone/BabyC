#!/bin/sh
# Run parse/compile failure fixtures against an ASan/UBSan BabyC binary.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BABYC=${1:-"$ROOT/src/babyc"}
if [ ! -x "$BABYC" ]; then
    echo "usage: $0 /path/to/asan-babyc" >&2
    exit 2
fi
TMP=${TMPDIR:-/tmp}/babyc-lsan-$$
mkdir "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}

run_failure() {
    label=$1
    shift
    log="$TMP/output.log"
    if "$@" >"$log" 2>&1; then
        cat "$log"
        echo "FAIL: $label unexpectedly exited successfully" >&2
        exit 1
    fi
    if grep -Eq 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:|SUMMARY: UndefinedBehaviorSanitizer' "$log"; then
        cat "$log"
        echo "FAIL: sanitizer diagnostic in $label" >&2
        exit 1
    fi
    if [ ! -s "$log" ]; then
        echo "FAIL: $label emitted no diagnostic" >&2
        exit 1
    fi
    echo "PASS: $label (expected nonzero exit, no sanitizer report)"
}

for fixture in "$ROOT"/tools/ir_verify/parse_failure_lsan/*.c; do
    run_failure "$(basename "$fixture") / IR" "$BABYC" "$fixture"
done
run_failure 'bad_assignment.c / token-only' env BABYC_NO_IR=1 "$BABYC" \
    "$ROOT/tools/ir_verify/parse_failure_lsan/bad_assignment.c"
