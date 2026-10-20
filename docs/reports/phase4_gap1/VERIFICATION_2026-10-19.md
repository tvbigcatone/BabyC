# Phase 4 gap 1 — targeted closure verification (2026-10-19)

Baseline: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. Cumulative overlay includes Phase 3 integer promotions and Phase 4 partial.

## Root-cause fixes

1. **Tree mixed signed/unsigned relops (1026):** the fused relational evaluator `IrTryRelop` in `src/ir_eval.c` used `IrRelopLL` directly on sign-extended values, bypassing the shared usual-arithmetic-conversion decision. It now asks `IrExprBinaryUsesUnsigned(Parser, E->Op, E->Left, E->Right)` and performs the comparison in the unsigned domain when the canonical common integer type is unsigned. This fixes the generic `int` versus `unsigned int` case, not a test-specific pattern.
2. **Linear unsigned bitfield promotion (1027):** `IrExprIntegerBase` could not see the bitfield width before lowering stamped `IrExpr.BitWidth`; `IrExprType` on the member's base identifier could also be unavailable in the compile context. `IrExprDeclaredBitField` now resolves the declared aggregate layout and member metadata, including width and signedness, and promotes a bitfield to `int` whenever its value range fits using `BABYC_VALUE_BITS_INT`. The width model remains centralized in `src/platform.h`; no new scattered ABI `#ifdef`s were added.

The shared common-type model in `src/ir_ops.c` remains the source of usual arithmetic conversion semantics. No test-specific fast path was added.

## Build and correctness

Clean Linux/GCC build: `make clean && make -j2`, exit code 0, 0 warnings, 0 errors.

All six numbered-matrix configurations passed **1019/1019**, zero timeout, zero skipped:

- Tree ACCEL=0 and ACCEL=1.
- Linear STRICT ACCEL=0 and ACCEL=1.
- Linear non-STRICT with `--require-no-tree-fallback`, ACCEL=0 and ACCEL=1; both report `tree_body=0`, zero fallback violations.

Tests `1024`–`1027` were compiled and run independently with GCC `-std=c11 -O0`; BabyC output matched GCC byte-for-byte for all four.

## A/B performance against Phase 3

Seven alternating paired runs per benchmark and ACCEL value, Linear STRICT, same host. The saved metric is child user+system CPU time (`RUSAGE_CHILDREN` delta), in milliseconds:

| Benchmark | ACCEL | Phase 3 median | Candidate median | Change |
|---|---:|---:|---:|---:|
| string_scan | 0 | 360.376 | 374.201 | +3.84% |
| string_scan_bounded | 0 | 482.968 | 481.300 | -0.35% |
| struct_score | 0 | 79.176 | 80.554 | +1.74% |
| dfs_rec | 0 | 32.020 | 33.869 | +5.77% |
| string_scan | 1 | 6.921 | 7.573 | +9.42% |
| string_scan_bounded | 1 | 9.171 | 10.536 | +14.88% |
| struct_score | 1 | 2.176 | 2.164 | -0.55% |
| dfs_rec | 1 | 15.939 | 16.730 | +4.96% |

This performance gate is **not clean**: several medians regress, with the largest increases on ACCEL=1 string scans. Given short sub-10-ms cases and substantial run-to-run variation, this result alone cannot distinguish real regressions from timing noise. It would be dishonest to claim “no performance regression”; further pinned/controlled profiling is open. Full samples are retained in `perf_ab7_phase3_vs_phase4.json`.

## Remaining scope

The two targeted failures are closed. This does not establish that every expression kind has canonical type metadata computed exactly once during lowering, nor does this Linux/GCC run validate MSVC/Windows LLP64 behavior. Broader operator×type coverage, Windows build/runtime validation, and a less noisy performance gate remain follow-up work. No new numbered tests were added; existing tests 1024–1027 are the differential probes.
