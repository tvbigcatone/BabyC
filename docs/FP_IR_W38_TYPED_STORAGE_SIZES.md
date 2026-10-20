# BabyC FP IR W38 — typed storage-size propagation

## Scope

W38 continues the W36/W37 storage-metadata work without introducing a separate
frontend `float` type prematurely. It removes three remaining `sizeof(double)`
assumptions from IR lowering paths where the concrete member type is available:

- member loads from arrays of structs;
- address/element-size inference for struct members;
- generic member-size inference.

Those paths now call `TypeFPStorageSize(member_type)`. The array-element stride
in the same lowering paths also consults the helper when the resolved element
type itself is FP. This keeps layout decisions attached to type metadata rather
than to an assumed binary64 representation.

## Validation

- Clean GCC build: PASS, no `warning:` or `error:` lines.
- FP mode gate: PASS, 28/28.
- Existing targeted floating-point cases and known IR/VM regressions: checked;
  the FP mode gate covers the W37 FP differential set.
- Full `tools/run_suite.py`: NOT COMPLETED; it exceeded the execution window
  while running the bytecode VM suite. No full-suite total is claimed.
- `tools/audit_fp_ir_arch.py`: EXPECTED FAIL, 0/4 modes match GCC. The frontend
  still maps `float` to the legacy binary64 `FPType`, so `sizeof(float)`,
  binary32 rounding and aggregate layout remain unresolved.

## Next architectural step

Introduce distinct frontend `float` and `double` types only alongside the full
migration of token evaluation, call/return handling, lvalue storage, array
stride, IR type flags, and binary32 rounding boundaries. A parser-only split
causes token-mode regressions and is not acceptable as a standalone change.
