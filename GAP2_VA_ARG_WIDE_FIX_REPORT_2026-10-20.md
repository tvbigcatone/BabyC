# GAP 2 — va_arg expression width and Tree unsigned-width fix (2026-10-20)

## Closed in this delta

1. `IrAnnotateExpr()` now assigns `IrExVaArg` its requested `E->VaType->Base` (or `TypeInt` only if the type is absent). This preserves the requested result type into parent binary/unary expressions, including `+`, compound assignment and multiplication. Regression `1048_va_arg_expression_width.c` covers long long expression contexts and double.
2. Tree IR unsigned result narrowing now honors a canonical wide-result flag. `IrAnnotateExpr()` sets `IR_CF_WIDE` for long/unsigned long and long long/unsigned long long when their platform width exceeds int; `ir_eval.c` uses that metadata to avoid narrowing 64-bit unsigned results to 32 bits when `ValWidth` is unknown.
3. Added test `1049_tree_unsigned_wide_arithmetic.c` for `unsigned long long` and (LP64) `unsigned long`, with `*=`, `s = s * t`, and `+=` in `main` and a function. Expected output matches GCC `-O0` on the Linux LP64 validation host.
4. `StdargMacroValues` is already per-instance state (`Picoc::StdargMacroValues`) in the inherited attempt9 overlay; lookup/registration accesses `pc->StdargMacroValues`. Source inspection confirms no remaining static global array with that name.

## Verification on the exact clean overlay

Baseline: `babyc_baseline_2026-10-18_loop_promotion(4).zip`, then apply `DELETE_FILES.txt`, then overlay this ZIP. Build command: `cd babyc/src && make clean && make -j8`.

- GCC clean build: exit 0, 0 warnings, 0 errors.
- Full numbered matrix: 1038/1038 in all six configurations:
  - Tree ACCEL=0 and ACCEL=1
  - Linear STRICT ACCEL=0 and ACCEL=1
  - Linear non-STRICT `--require-no-tree-fallback` ACCEL=0 and ACCEL=1
- Canonical gate: 1038/1038 reports, `invalid_nodes=0` in all six configurations.
- Linear non-STRICT fallback gate: `tree_body=0` in both ACCEL settings.
- Targeted 1048 and 1049: 12/12 configurations passed; outputs match the `.expect` files, generated from GCC `-O0` on this host.
- A/B: 7 alternating runs per variant for `string_scan`, `string_scan_bounded`, `struct_score`, `dfs_rec`, each with ACCEL=0 and ACCEL=1. Compared attempt9 vs patched binary, both built with the same GCC `-O2` Makefile flags; output matched between variants. Median patched-vs-attempt9 deltas: string_scan +0.40% / -1.11%; string_scan_bounded +0.76% / -2.46%; struct_score +1.30% / -2.38%; dfs_rec -1.07% / -3.39% (ACCEL 0 / 1 respectively). These are descriptive wall-clock samples, not proof of zero performance impact; the small +1.30% maximum is within the observed run-to-run noise but should not be represented as a guaranteed no-regression result.

## Open items (not claimed as closed)

- **(b) Typed variadic IR/Linear payload remains open.** `IrCallFrame::CallAbiArguments` is still `long long *`; the Linear call boundary does not yet preserve a `ValueType *`/typed payload per extra argument. Default promotions are not yet encoded in the caller-owned ABI vector, and `StdargVaArgBits()` still contains the `raw IR fallback payload` branch. No new 1050+ typed-payload test is claimed in this delta.
- **(c) Per-instance state is implemented in the inherited attempt9 changes and source-inspected, but a dedicated simultaneous-multiple-`Picoc` isolation test was not run.**
- MSVC was not available in this validation session. The code uses `BABYC_VALUE_BITS_*` platform macros, but LLP64 compilation and runtime were not independently tested.
- Full benchmark coverage is limited to the four requested microbenchmarks; no broader performance suite was run.

## Packaging

The ZIP is cumulative from the fixed loop_promotion baseline and carries `DELETE_FILES.txt`. The archive intentionally excludes every path listed by that file and excludes executable/object build artifacts and `__pycache__`. `CHANGED_FILES_SHA256.txt` hashes each other included file.
