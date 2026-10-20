# Phase 5 — canonical expression type annotation

Date: 2026-10-19

## Implementation

- Added `CanonicalBase` and `CanonicalFlags` to `IrExpr`.
- Centralized bottom-up annotation in the `IrAnnotateExpr` wrapper: children are annotated first, the existing declaration/bitfield/cast/call metadata pass runs, then one canonical base/flags record is stored on the node.
- Integer expression classification consumes the recorded canonical base first. The shared `IrIntegerPromotionBase` and `IrUsualArithmeticCommonIntegerBase` remain the source of integer promotion and usual arithmetic conversion rules; target widths remain sourced from `platform.h` macros.
- Covered binops/relational/logical/shift/comma, compound assignment through assignment nodes, ternary, cast/unary/increment, member/index, enum/char constants, and resolvable call return types.
- Added `src/tests/1028_canonical_type_forms.c` and `src/tests/1029_canonical_comma_enum_char.c`; both `.expect` files were generated using `gcc -std=c11 -O0`.

## Build and test matrix

Clean build: `make clean && make -j2`; GCC output contained zero `warning:` and zero `error:` diagnostics.

| Configuration | Result | Fallback |
|---|---:|---:|
| Tree ACCEL=0 | 1021/1021 | n/a |
| Tree ACCEL=1 | 1021/1021 | n/a |
| Linear STRICT ACCEL=0 | 1021/1021 | n/a |
| Linear STRICT ACCEL=1 | 1021/1021 | n/a |
| Linear non-STRICT ACCEL=0 | 1021/1021 | `tree_body=0` |
| Linear non-STRICT ACCEL=1 | 1021/1021 | `tree_body=0` |

All matrix logs are under `docs/reports/phase5/`.

## A/B vs Phase 4

Seven alternating paired runs per benchmark and ACCEL setting, Linear STRICT, same host; metric is child user+system CPU milliseconds. Results in `docs/reports/phase5/perf_ab7_phase4_vs_phase5.json`.

| Benchmark | ACCEL | Phase 4 median ms | Phase 5 median ms | Change |
|---|---:|---:|---:|---:|
| string_scan | 0 | 411.903 | 408.854 | -0.74% |
| string_scan_bounded | 0 | 514.615 | 531.065 | +3.20% |
| struct_score | 0 | 95.891 | 82.863 | -13.59% |
| dfs_rec | 0 | 31.474 | 30.152 | -4.20% |
| string_scan | 1 | 6.762 | 6.672 | -1.33% |
| string_scan_bounded | 1 | 8.251 | 8.069 | -2.21% |
| struct_score | 1 | 2.439 | 1.995 | -18.20% |
| dfs_rec | 1 | 13.146 | 12.762 | -2.92% |

The single +3.20% median is not enough to establish a regression given the observed spread of the seven samples, but should be rechecked with longer paired runs before a hard performance gate.

## Remaining work — explicit

The canonical record is now assigned once per annotated expression node and is read by the shared integer classification path. This does **not** yet close the entire architectural goal at 100%: `IrExprDeclaredBitField` and `IrExprBinaryUsesUnsigned` remain as fallback inference for nodes lacking sufficient metadata, and nested pointer/member/call-return type recovery needs more coverage before those fallbacks can be removed. This turn does not claim Windows/MSVC validation.
