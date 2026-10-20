# Body-IR parse-failure cleanup fixtures

These files are intentionally invalid C. They are sanitizer regression inputs,
not part of the normal `src/tests/*.expect` suite.

Run from the repository root with an ASan/UBSan build:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=halt_on_error=0 \
  ./src/babyc tools/ir_verify/parse_failure_lsan/bad_expr.c
```

Each input should fail parsing/compilation (non-zero exit is expected) without
an AddressSanitizer, UndefinedBehaviorSanitizer, or LeakSanitizer report.

`bad_assignment.c` covers a semantic assignment error after entering a nested
function call. Run it once through the default IR path and once through the
legacy token-only path; the latter exercises cleanup after `ProgramFail()`
longjmps past active local stack frames:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  ./src/babyc tools/ir_verify/parse_failure_lsan/bad_assignment.c

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 BABYC_NO_IR=1 \
  ./src/babyc tools/ir_verify/parse_failure_lsan/bad_assignment.c
```

Both invocations should emit the expected type-mismatch diagnostic and exit
non-zero, with no sanitizer report.
