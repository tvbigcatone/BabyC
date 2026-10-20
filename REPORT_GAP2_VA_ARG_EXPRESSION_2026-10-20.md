# BabyC — GAP 2 variadic follow-up (2026-10-20)

## Packaging / reproducibility

This is a cumulative source delta from the fixed `babyc_baseline_2026-10-18_loop_promotion(4).zip`, carrying forward the attempt8 intrinsic-identity changes. Apply `DELETE_FILES.txt` first, then overlay the ZIP. The ZIP deliberately excludes every path listed in `DELETE_FILES.txt` and excludes build products (`*.o`, `*.obj`, `*.exe`, `*.pyc`, `__pycache__`). `CHANGED_FILES_SHA256.txt` hashes every packaged changed/new file except the manifest itself.

## Build and focused reproduction

Clean overlay build command: `make clean && make -j8 babyc` in `babyc/src`.

Observed result: build succeeded; compiler output contained 0 `warning:` lines. The new test `src/tests/1048_va_arg_expression_width.c` was run in all six requested path/ACCEL combinations (Tree ACCEL 0/1; Linear STRICT ACCEL 0/1; Linear non-STRICT ACCEL 0/1). All six runs reproduced the truncation, printing:

```
705032704
5000000000
705032704
2.75
1705032704
```

Expected GCC `-O0` output in `.expect` is:

```
5000000000
5000000000
5000000000
2.75
6000000000
```

Therefore test 1048 is intentionally retained as a failing regression reproducer, not reported as passing. This turn did not complete the full 1036+ test matrix, canonical gate, no-fallback matrix, seven-run A/B benchmark, or MSVC build. Those results must not be inferred from attempt8 because this delta changes parser state/type handling.

## Objective status

### (a) `va_arg` 32-bit truncation as expression operand — OPEN

Attempted to preserve the requested type by pushing a `Value` allocated with `RequestedType` even during non-run parsing, instead of pushing an `int` placeholder. The clean-overlay reproduction still fails identically in Tree and Linear. This is not a fix; the remaining root cause is likely further down the expression/IR typing or value conversion path. Cases `s = s + va_arg(...)`, `s += ...`, `* 1`, and `*=` are not closed.

### (b) Typed variadic payload on IR/Linear — OPEN

`IrCallFrame.CallAbiArguments` remains a `long long *`; caller-side default argument promotions and type-tagged payloads are not implemented. The `raw IR fallback payload` branch in `StdargVaArgBits` remains. Pointer/struct payload correctness is not established.

### (c) `StdargMacroValues` per-instance state — SOURCE CHANGE APPLIED, VALIDATION OPEN

Moved the macro identity array into `Picoc` state and changed lookup/registration to use the current `Picoc *`. Build passes with zero warnings, but the full matrix and multi-instance isolation test were not run.

## Test additions

- `src/tests/1048_va_arg_expression_width.c`
- `src/tests/1048_va_arg_expression_width.expect`

No claim is made that the requested objectives are complete. The delta is delivered now with the failure preserved visibly for follow-up.
