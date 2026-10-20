# Linear register liveness follow-up — 2026-10-18

## Scope and status

This checkpoint deliberately does **not** ship the attempted eager operand-release allocator. That approach treated an operand's use in the current instruction as its last use, which is not generally valid in a linear IR containing branches, loops, cached values and deferred call arguments. An experiment using it caused 119 suite failures and was reverted in full. Shipping it would violate the correctness gate.

The current runtime source therefore remains the last validated register-pinning implementation, with `IR_LIN_REGS=64`; it does not yet satisfy the requested intra-expression last-use allocator, and the special-form loop CSE paths remain enabled. No performance improvement from liveness is claimed.

## Matrix

Executed the complete `.expect` suite (996 tests) in bounded tranches with a 10-second per-test timeout:

| Profile | Pass | Fail | Timeout | Total |
|---|---:|---:|---:|---:|
| Tree, ACCEL=0 | 996 | 0 | 0 | 996 |
| Tree, ACCEL=1 | 996 | 0 | 0 | 996 |
| Linear STRICT, ACCEL=0 | 979 | 17 | 0 | 996 |
| Linear STRICT, ACCEL=1 | 979 | 17 | 0 | 996 |

Historical pre-renumbering labels map as follows: 046→047, 057→058, 061→062, 121→126, 280→288, 441→454, 610→606, 613→609, 622→618, 661 (SQLite)→651, 785→789, 942→873, 943→874, 959→893, 1033→947, 1034→948. Old label 379 was ambiguous because the source prefix was duplicated; the mapped cases are `395_array_fn_loop_local_global` and `396_complex_inventory`, both passing. Cases 288, 789 and 874 were fixed later. The final 13 Linear STRICT residuals are 047, 058, 062, 126, 454, 606, 609, 618, 651, 873, 893, 947 and 948.

## A/B benchmark (ms, median of 3 runs)

These are measured against the local `baseline_build` binary and current tree; output and exit code were successful for every run. The baseline binary predates `BABYC_DUMP_LIN`, so a baseline IR dump was not available from that executable.

| Benchmark | ACCEL | Baseline | Current | Change |
|---|---:|---:|---:|---:|
| string_scan | 0 | 270.738 | 276.360 | +2.1% |
| string_scan_bounded | 0 | 313.328 | 327.649 | +4.6% |
| struct_score | 0 | 65.457 | 65.457 | 0.0% |
| dfs_rec | 0 | 23.788 | 26.527 | +11.5% |
| string_scan | 1 | 5.118 | 5.396 | +5.4% |
| string_scan_bounded | 1 | 6.116 | 6.895 | +12.7% |
| struct_score | 1 | 1.561 | 1.699 | +8.8% |
| dfs_rec | 1 | 9.382 | 11.325 | +20.7% |

These results do **not** pass the no-performance-regression gate. The short samples are noisy, but the direction is consistently adverse for most current measurements and needs revalidation after the allocator/frame change.

## `dfs_rec` IR evidence

The current dump identifies the recursive function as `dfs`: 40 linear instructions, highest touched register 9 (registers 0–9), with one `IrLinCall` at instruction 34 and three branches plus three jumps. The `main` function has 105 instructions. The dump is retained in `dfs_rec_ir_current_2026-10-18.txt`.

`IR_LIN_REGS=64` reserves 512 bytes for a `long long[64]` register array where the 48-register baseline reserves 384 bytes, a 128-byte increase per frame. This is a plausible contributor to recursive overhead, but the dump alone does not prove it is the root cause. A valid baseline/current instruction comparison still requires building the baseline with the permanent dump hook, then comparing op counts, move/copy count and frame size under the same compiler and flags.

## Correct implementation direction

The rejected eager-release experiment demonstrates why `Emit()` cannot simply free all source operands: syntactic operand use is not equivalent to last use across CFG edges, and CSE/deferred call-argument references extend live ranges. The safe implementation is CFG-aware liveness over basic blocks (including loop backedges), with interference/coloring or an equivalent allocator; CSE entries and call-argument registers must be explicit roots. Lowering should use virtual registers independently of the physical frame limit, then allocate physical registers before execution. Until that pass exists and passes differential tests, retain the validated 64-register setting rather than pretending 48 is safe.

## Validation limits

- Linux/GCC build succeeds without errors.
- Full four-profile matrix completed; no new suite failures relative to the validated current tree.
- MSVC build was not run here.
- The requested CSE-form A/B with only `while(*p)` / `while(p<end)` specializations disabled was intentionally not run because the prerequisite intra-expression liveness work is not complete; doing it now would violate the requested order.
