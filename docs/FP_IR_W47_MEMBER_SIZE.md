# BabyC FP/IR — W47 member-size consistency

## Change

`src/ir_compile.c` folds `sizeof(expr.member)` for a non-bitfield member during IR compilation. Previously that path copied `MV->Typ->Sizeof` directly. It now calls `TypeSize(MV->Typ, MV->Typ->ArraySize, TRUE)`, the shared semantic size helper, and uses the same helper to decide whether a positive size is available.

This removes a stale/cached-size shortcut from compile-time member `sizeof` handling. It is important for the FP work because an FP object's storage width can differ from its double-sized execution temporary, and for array members whose derived size must be computed consistently. It does not itself activate distinct frontend `float` identity or claim to solve the overall FP architecture gap.

## Verification

- Clean GCC build: PASS, exit 0; no `warning:` or `error:` lines found in the captured build log.
- `python3 tools/test_fp_modes.py --babyc src/babyc`: PASS, 28/28.
- Targeted tests: `063_float.c` and `666_fp_member_index.c` pass in all four execution modes. `023_floating_point.c`, `028_sizeof.c`, and `036_sizeof.c` pass in default/token; VM output matches expected apart from the experimental-mode banner, which the project suite filters.
- Full `tools/run_suite.py src`: attempted, but exceeded the 300-second execution limit. No full-suite pass is claimed.
- `tools/audit_fp_ir_arch.py`: still 0/4 modes match GCC. This remains an open architecture task, not a W47 regression claim.
- MSVC: not run.

## Apply

From the BabyC repository root, apply `DELTA_W47.patch` with `patch -p1 < DELTA_W47.patch`, or replace `src/ir_compile.c` with the copy in this package. The patch is against `babyc(20261014-194337).zip` / W36 and touches only the W47 hunk in `src/ir_compile.c`; it is compatible with the W46 cumulative work because W46 did not modify this file.
