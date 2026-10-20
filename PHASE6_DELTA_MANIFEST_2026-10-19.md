# Cumulative delta manifest — Phase 6 canonical type closure

- Fixed baseline: `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`).
- Incoming cumulative archive: `babyc_cumulative_delta_2026-10-19_phase5_canonical_type.zip` (SHA-256 `9ae43c71c4dfa9aca0fc727e3f83c0059c6ce7ab4bd98f73a50afc5b43cc15fd`).
- This archive carries forward the Phase 5 cumulative overlay and adds the Phase 6 changes listed below. `DELETE_FILES.txt` is retained unchanged from the cumulative archive; apply deletions before overlaying `babyc/` files.

## Phase 6 files changed/added

- Updated: `src/ir.h`, `src/ir_compile.c`, `src/ir_eval.c`, `src/ir_fast.c`, `src/ir_lin_emit.c`, `src/ir_ops.c`, `src/ir_runtime.h`, `src/picoc.c`, `tools/run_numbered_matrix.py`, `docs/LINEAR_COVERAGE_MAP.md`.
- Added: `src/tests/1030_canonical_nested_bitfields_fnptr.c/.expect`, `src/tests/1031_canonical_mixed_arrays_ternary.c/.expect`, `PHASE6_CANONICAL_TYPE_REPORT.md`, `PHASE6_DELTA_MANIFEST_2026-10-19.md`, and `docs/reports/phase6/` matrix/performance logs.

## Verification

- Clean Linux/GCC build (`make clean && make -j8`): 0 warnings, 0 errors.
- Matrix: all six configurations 1023/1023; canonical gate reports 1023/1023 processes and 0 invalid nodes; Linear non-STRICT `tree_body=0` at ACCEL 0/1.
- A/B: 7 alternating pairs for four benchmarks at ACCEL 0/1 against Phase 5; outputs identical. Timings are mixed/noisy, with `string_scan_bounded` ACCEL=0 median +12.25% and `dfs_rec` ACCEL=1 +7.44%; no blanket no-regression claim is made.
- Windows/MSVC and a longer controlled A/B are open.
