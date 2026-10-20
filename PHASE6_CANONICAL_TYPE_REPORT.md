# Phase 6 — canonical expression type is the sole IR type-decision source

Date: 2026-10-19

## Implementation

- Extended `IrExpr` with `CanonicalOpBase` and `CanonicalOpFlags`. `CanonicalBase/CanonicalFlags` record the expression result type; the operation pair records the promoted/common type used by arithmetic, relational/equality, shift, ternary, and compound-assignment operations. Both are assigned bottom-up in `IrAnnotateExpr` after child annotations and the declaration metadata pass. A completed node is not re-annotated, and `IrCompileFunctionBody` no longer invokes a second annotation pass after `IrCompileBody`; each node is annotated once per body compilation.
- Added type recovery for nested `.`, `->`, dereference, indexing, array locals represented as element type plus pointer flags, direct calls, function-pointer calls, assignment/comma/ternary result types, casts, and pointer arithmetic. Narrow unsigned bitfields promote using their declared width and `BABYC_VALUE_BITS_INT`.
- Tree evaluation, Linear emission, and `IrApplyBinOp` now consume canonical flags for signedness and width. Removed `IrExprBinaryUsesUnsigned`, `IrExprBinaryUsesWide`, `IrExprDeclaredBitField`, `IrExprIntegerBase`, and the legacy type-inference fallback from `ir_ops.c`. Removed remaining Tree/Linear operator decisions that called `IrExprIsUnsigned` / `IrExprIsWideInt`; those helpers and their prototypes were deleted.
- Synthetic address/member nodes created during Linear emission are stamped from their known source node/type at construction. This closed the only invalid-node gate hits found during bring-up (`091`, `260`, `395`, `491`, `874`, `923`), rather than allowing the emitter to infer type on demand.
- Added `BABYC_CANONICAL_GATE=1` runtime gate. The runner enables it, collects the last per-process invalid-node counter, prints `CANONICAL_TYPE_GATE`, and fails if any case lacks a report or if `invalid_nodes > 0`. Instrumentation is inactive by default outside the gate, so benchmarks do not pay per-node reporting overhead.
- Target ABI width decisions continue to use `platform.h` macros (`BABYC_VALUE_BITS_INT`, `BABYC_VALUE_BITS_LONG`, `BABYC_VALUE_BITS_LLONG`, `BABYC_VALUE_BITS_POINTER`); no new scattered platform `#ifdef` branches were added.

## New differential tests

- `src/tests/1030_canonical_nested_bitfields_fnptr.c/.expect`: nested signed/unsigned bitfields through `->`, function pointers returning `unsigned char`, `unsigned short`, and `unsigned int`, mixed comparisons, indexed unsigned-short arrays, and mixed ternary.
- `src/tests/1031_canonical_mixed_arrays_ternary.c/.expect`: array indexing, mixed signed/unsigned comparisons, ternary conversions, compound assignments, increments, casts, and comma expressions.
- Both `.expect` files were generated with `gcc -std=c11 -O0`; BabyC output matches exactly in the full matrix.

## Clean build and matrix

Clean build: `make clean && make -j8`, Linux/GCC; zero warnings and zero errors.

| Configuration | Result | Canonical invalid nodes | Tree fallback |
|---|---:|---:|---:|
| Tree ACCEL=0 | 1023/1023 | 0 | n/a |
| Tree ACCEL=1 | 1023/1023 | 0 | n/a |
| Linear STRICT ACCEL=0 | 1023/1023 | 0 | n/a |
| Linear STRICT ACCEL=1 | 1023/1023 | 0 | n/a |
| Linear non-STRICT ACCEL=0 | 1023/1023 | 0 | `tree_body=0` |
| Linear non-STRICT ACCEL=1 | 1023/1023 | 0 | `tree_body=0` |

The runner reports `CANONICAL_TYPE_GATE: reports=1023/1023 invalid_nodes=0` in all six configurations. Full logs are in `docs/reports/phase6/`.

## A/B against Phase 5

Seven alternating pairs per benchmark/ACCEL setting, Linear STRICT, same host; stdout had to match exactly. Metric is child user+system CPU milliseconds. Medians are in `docs/reports/phase6/perf_ab7_phase5_vs_phase6.json`.

| Benchmark | ACCEL | Phase 5 median ms | Phase 6 median ms | Change |
|---|---:|---:|---:|---:|
| `string_scan` | 0 | 377.163 | 405.139 | +7.42% |
| `string_scan_bounded` | 0 | 459.283 | 515.532 | +12.25% |
| `struct_score` | 0 | 81.163 | 84.290 | +3.85% |
| `dfs_rec` | 0 | 33.829 | 32.177 | -4.88% |
| `string_scan` | 1 | 6.920 | 7.014 | +1.36% |
| `string_scan_bounded` | 1 | 8.335 | 7.480 | -10.26% |
| `struct_score` | 1 | 2.511 | 1.765 | -29.71% |
| `dfs_rec` | 1 | 14.675 | 15.767 | +7.44% |

The correctness gate is green, but these seven-pair timings are noisy and mixed: notably `string_scan_bounded` ACCEL=0 has a +12.25% median and `dfs_rec` ACCEL=1 +7.44%. These are possible regressions, not evidence to dismiss as noise; the large negative medians on other configurations also show substantial host/run variance. Do not treat this as proof of either a stable regression or no regression; a controlled longer benchmark run remains open. A 31-pair attempt was interrupted by the execution timeout after its first benchmark and is not used in the reported measurements.

## Remaining limitations

- Linux/GCC was verified; Windows/MSVC was not built in this turn.
- The canonical source-of-truth path is now enforced for the targeted Tree/Linear operator decisions and all tested emitted/evaluated nodes. The suite is not a proof of full ISO C type-system conformance: unsupported frontend forms (e.g. some aggregate conditional expressions, compound literals, GNU statement expressions) remain outside the existing Body-IR model and need separate design/test work.
- The no-invalid-node gate covers nodes actually evaluated or emitted during this numbered corpus; it does not claim all parse-only or never-selected AST nodes in arbitrary programs were executed.

## Historical `tree_a0_retry*.log` files in Phase 5

Those four retry logs are successive diagnostic snapshots, not timeout evidence or an assertion that the same test was flaky. `tree_a0_retry.log` still had the earlier batch of failures; `tree_a0_retry2.log` and `tree_a0_retry3.log` each narrowed to `1027_integer_type_matrix`; `tree_a0_retry4.log` reached 1021/1021. Each of those logs reports `TIMEOUT=0`. The changing failure set is consistent with reruns while the source/fix state was being updated, not a sandbox timeout.
