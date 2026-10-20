# Attempt21 — report onesto

Baseline: `babyc_baseline_2026-10-18_loop_promotion` + cumulative attempt20. The archive contains cumulative attempt20 files plus the partial attempt21 changes; no executable or object files are included.

## Changes made
- `StdargVaTypeCompatible`: removed broad integer-family acceptance; only identical base types and same-rank signed/unsigned pairs are accepted. Struct/union require same base and identical `ValueType*`.
- Linear `IrLinVaArg` carries `E->VaType` as exact metadata instead of reconstructing it only from `Imm`/Base.
- Added full `CallArgType` side table for Linear call arguments and copied it through `FnCodeSlot` snapshots; struct/union argument type is captured where the expression's cached value or identifier binding provides it.
- `IrCallPromotedArgType` attempts to preserve exact struct/union type from the expression's cached value or identifier binding. `IrBuildCallArgTypes` now fails explicitly if capacity is exceeded or an argument's static type cannot be resolved.
- Added tests 1075–1077.

## Verification performed
- GCC build with `-O0` and project warning flags: completed successfully. The build output showed no warnings.
- Ran the new tests directly in Tree mode:
  - 1075 struct/union by-value: FAILS with `va_arg type mismatch`.
  - 1076 long literal / expressions: FAILS with `va_arg type mismatch`.
  - 1077 int passed but long long requested: emits expected `va_arg type mismatch`.
- The full `run_suite.py src` invocation exceeded the available execution window and was terminated. No claim is made that smoke 1032–1077 passed; the requested smoke blocks and Linear checks remain unverified.
- No full matrix, canonical gate, A/B, MSVC build, or GCC differential run was performed.

## Incomplete / known defects
- Exact struct/union type propagation into every IR call argument is not complete; test 1075 proves this remains broken.
- The static type of `L`-suffixed integer constants and expressions is not fixed; test 1076 proves this remains broken.
- The Linear struct/union path is not validated and should be considered incomplete.
- `ir_fast.c` / `ir_lower.c` call sites that pass `ArgTypes=NULL` have not been audited/fixed in this attempt.
- The self-check for extra variadic slots with NULL `Type` remains absent.
- This is a partial attempt, not a completed fix for the requested 1+2+3 work.
