# Phase 4 gap 1 — MSVC warning cleanup and POSIX overlay integration

Date: 2026-10-19
Baseline: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`.

## MSVC warning fixes in `src/ir_lin_emit.c`

- C4700 (`ld`, `rd`, around former line 1045): removed an accidental `MOD_TRACE` diagnostic inserted in an unrelated indexed-address lowering path. It read two locals that were not initialized in that branch; it was not required for code generation.
- C4101 (`one`, former line 3890): removed the unused local from the bitfield increment/decrement lowering declaration.
- C4101 (`uns`, former line 4392): removed the unused local from indirect pointer increment/decrement lowering declaration.

The source-level causes of all three reported diagnostics are removed. A clean Linux/GCC rebuild completed with exit code 0 and no emitted warnings/errors. **MSVC itself was not available in this environment**, so no claim is made that a Windows/MSVC build was executed.

## Phase 4 correctness matrix

After applying the warning fixes and the Phase 4 root-cause fixes, all six numbered test matrices pass 1019/1019 with zero timeout and zero skipped cases:

- Tree ACCEL=0: 1019/1019
- Tree ACCEL=1: 1019/1019
- Linear STRICT ACCEL=0: 1019/1019
- Linear STRICT ACCEL=1: 1019/1019
- Linear non-STRICT `--require-no-tree-fallback` ACCEL=0: 1019/1019, `tree_body=0`, violations=0
- Linear non-STRICT `--require-no-tree-fallback` ACCEL=1: 1019/1019, `tree_body=0`, violations=0

The compiler outputs for tests 1024–1027 were independently compiled with `gcc -std=c11 -O0` and each matched its `.expect` file byte-for-byte. Tests 023, 029, 041, and 1022 passed in the full matrix.

## BabyPOSIX session 3 integration smoke suite

The new POSIX tests and session-3 source changes are included. Results on this combined tree:

- Tree: 29/29
- NO_IR: 29/29
- Linear non-STRICT: 28/29 (`pthread_exit_retval` fails)
- Linear STRICT: 23/29 (six failures: `dup_basic`, `fcntl_basic`, `fd_errors`, `fd_rollback`, `pthread_create_join` segfault, `pthread_exit_retval`)

These are POSIX/Linear residuals outside the numbered Phase 4 gap-1 matrix; they remain open and are not masked or described as passing.

## Performance / broader scope

The previously recorded seven-pair alternating Phase 3-vs-Phase 4 A/B results remain unchanged. Several medians regress (largest recorded +14.88%); the performance non-regression gate is therefore still open. Full canonical type metadata computed once for every expression form, MSVC runtime/build validation, and POSIX Linear residuals also remain open. The semantic Phase 4 gap-1 matrix is closed; these broader gates are not.
