# BabyC FP IR — W46 status (2026-10-14)

## Baseline and scope

This delta is cumulative relative to `babyc(20261014-194337).zip` (W36), incorporating the intervening W37–W45 FP/IR work and the W46 change in `ExpressionAssign()`.

## W46 source change

The generic `ExpressionAssign()` floating-point branch now rounds a destination tagged `FPStorageBinary32` through `float`, just as `ExpressionAssignFP()` does. This covers forced assignments such as function return slots that can use the generic assignment route. The conversion is applied at the semantic destination boundary; the execution temporary remains double-sized.

The frontend mapping of `TokenFloatType` remains unchanged in this delta. A direct attempt to map it to `FloatType` was reverted because it caused the existing `881_fp_user_func_return.c` token-mode regression and, together with an IR local declaration experiment, broke FP array values. No failing experimental edits are retained.

## Verification

- Clean GCC build: PASS, exit 0.
- Compiler warnings: 0 in the clean build log.
- Compiler errors: 0 in the clean build log.
- `python3 tools/test_fp_modes.py --babyc src/babyc`: PASS, 28/28.
- `python3 tools/audit_fp_ir_arch.py --babyc src/babyc`: expectedly FAIL, 0/4 modes match GCC. Remaining mismatches include `sizeof(float)`, float array/object layout, binary32 rounding, and struct layout.
- `python3 -u tools/run_suite.py src`: attempted but did not complete within the 300-second execution limit. It reported `string-scan-fastpath`, `sparse-local-snapshot`, and FP modes passing before timeout; this is not a full-suite pass.
- MSVC has not been rerun for W46.

## Architectural conclusion

W46 is a safe boundary-correctness step, not closure of FP support. The central unresolved issue is that the frontend, Body-IR local declarations, and runtime object-memory accesses do not yet consistently preserve binary32 as a distinct semantic/storage format. The next implementation should proceed end-to-end: type identity and `sizeof`/alignment, local/array/struct storage, load/store conversions, call arguments and returns, then all four execution modes. Do not enable the frontend split until differential and existing regression tests pass, especially `881_fp_user_func_return.c` and FP array/pointer cases.
