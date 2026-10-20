# Linear loop CSE and frame specialization review — 2026-10-18

## Frame-size experiment

Compared exact per-function register frames against four frame classes (8/16/32/64), seven alternating repetitions, ACCEL=0:

| Benchmark | Exact-size frame median ms | Tiered frame median ms | Change |
|---|---:|---:|---:|
| string_scan | 260.572 | 305.780 | +17.4% |
| string_scan_bounded | 291.235 | 362.713 | +24.5% |
| struct_score | 64.174 | 73.150 | +14.0% |
| dfs_rec | 23.021 | 25.367 | +10.2% |

Decision: rejected the four frame classes because all four measurements regressed materially. Exact `RegCount` frames remain. The wrappers share the same execution body, but are specialized for the exact register count. The unused `BABYC_LIN_FRAME_PER_FUNCTION` toggle was removed; the pinning and stack-reuse toggles were also removed, leaving the correctness-validated behavior unconditional.

## Shape-specific while CSE A/B

Compared the same exact-frame source with the two shape-specific CSE lowerings enabled vs disabled, seven alternating repetitions, ACCEL=0:

| Benchmark | Specialized median ms | Generic median ms | Change |
|---|---:|---:|---:|
| string_scan | 270.542 | 307.814 | +13.8% |
| string_scan_bounded | 305.801 | 374.837 | +22.6% |
| struct_score | 64.892 | 62.968 | -3.0% |
| dfs_rec | 25.851 | 24.321 | -5.9% |

The specializations cannot yet be removed without a clear regression in the two string scans. The experiment was done in a separate build; the delivered build keeps the specializations enabled, so performance is not knowingly sacrificed.

## Dump comparison and generic CSE improvement

`string_scan` IR: specialized = 70 instructions; generic = 77. `string_scan_bounded` IR: specialized = 80; generic = 88. The generic path now structurally keys unary dereference (`*p`) expressions as well as indexed expressions, and the dump shows one `IrLinLoadIndex` in the hot loop instead of separate reloads for the condition and repeated comparisons. A loop-body pre-invalidation helper preserves the condition's expression cache for the body when only local slots are written; regular invalidation still drops it on local writes at statement boundaries and on any memory write/call. This is correctness-checked against the full suite.

Remaining gap: generic lowering still emits 7 extra instructions for `string_scan` and 8 extra for `string_scan_bounded`, with separate loop control/condition handling. Those extra instructions and resulting control-flow overhead explain why the generic version remains 14–23% slower. Do not remove the specialized lowerings until a general loop transformation removes this gap and the same A/B gate passes.

## Final baseline A/B

Seven alternating repetitions against the fixed baseline, median milliseconds:

| Benchmark | ACCEL | Baseline | Current | Change |
|---|---:|---:|---:|---:|
| string_scan | 0 | 280.575 | 263.171 | -6.2% |
| string_scan_bounded | 0 | 306.645 | 304.851 | -0.6% |
| struct_score | 0 | 63.758 | 64.377 | +1.0% |
| dfs_rec | 0 | 26.911 | 26.140 | -2.9% |
| string_scan | 1 | 5.600 | 5.326 | -4.9% |
| string_scan_bounded | 1 | 6.116 | 6.173 | +0.9% |
| struct_score | 1 | 1.574 | 1.621 | +3.0% |
| dfs_rec | 1 | 10.021 | 10.062 | +0.4% |

Small +1–3% deltas on the shorter ACCEL=1 tests are treated as noise, consistent with the user's instruction. ACCEL=0 is at baseline within measurement noise, with three of four medians faster.

## Correctness matrix

- Tree ACCEL=0: 996 pass, 0 fail, 0 timeout
- Tree ACCEL=1: 996 pass, 0 fail, 0 timeout
- Linear STRICT ACCEL=0: 979 pass, 17 historical fail, 0 timeout
- Linear STRICT ACCEL=1: 979 pass, same 17 historical fail, 0 timeout

Historical failure labels before canonical renumbering: 046→047, 057→058, 061→062, 121→126, 280→288, 441→454, 610→606, 613→609, 622→618, 661 (SQLite)→651, 785→789, 942→873, 943→874, 959→893, 1033→947, 1034→948. Old label 379 was ambiguous after the duplicated prefixes were discovered; the two new IDs are `395_array_fn_loop_local_global` and `396_complex_inventory` and both pass. The final unresolved list is 047, 058, 062, 126, 454, 606, 609, 618, 651, 873, 893, 947, 948.

Build verified with Linux/GCC. MSVC not run in this environment.
