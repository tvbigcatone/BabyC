# Linear condition-to-branch lowering — 2026-10-18

## Scope

This delta implements point 1 only. It does not implement local promotion across loop regions (point 2).

- `if`, generic `while`, generic `for`, and `do-while` use a shared false-edge lowering helper.
- `&&` is emitted as a short-circuit chain of false edges.
- `||` emits a local skip jump so the RHS is evaluated only when the LHS is false.
- A ternary used as a condition selects exactly one arm and branches on the selected arm's truth value.
- Signed/unsigned integer comparisons in conditions use `IrLinBrCmp`, an integer compare-and-branch instruction. The false edge is encoded using the inverse comparison so the true path falls through, including the hot path of loops.
- FP comparisons intentionally retain the established value-lowering path; `IrLinBrCmp` currently does not encode IEEE FP comparison semantics.
- The opcode is handled by Linear, VM production/validation, the light-frame executor, branch-target remapping, and liveness/read analysis.

## Targeted tests

Added under `src/tests/`:

- `998_condition_short_circuit.c`: side-effect assignments on skipped and taken `&&`/`||` RHS paths.
- `999_condition_nested_ternary.c`: nested condition, ternary condition and selected-arm side effects.
- `1000_condition_compare_branch.c`: relational/equality comparisons, chained `&&`/`||`, `for`, and `while`.

All 3 pass in the default Tree path and in Linear STRICT (`BABYC_EXEC=vm BABYC_VM_STRICT=1`) with `BABYC_ACCEL=0` and `BABYC_ACCEL=1`.

## Full correctness matrix

Run on 999 expected-output tests, timeout 20 seconds per test, batches of 100:

| Engine | ACCEL | Pass | Fail | Timeout |
|---|---:|---:|---:|---:|
| Tree | 0 | 999 | 0 | 0 |
| Tree | 1 | 999 | 0 | 0 |
| Linear STRICT | 0 | 982 | 17 | 0 |
| Linear STRICT | 1 | 982 | 17 | 0 |

The 17 Linear STRICT failures are unchanged historical baseline failures: `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `288_heap_size_classes`, `395_array_fn_loop_local_global`, `454_complex_dispatch`, `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, `618_inc_member_call_arg`, `651_sqlite_exec_null_varargs`, `789_vm_pointer_fallback`, `873_alignment_extended`, `874_array_member_storage`, `893_vm_struct_return_effect_fallback`, `947_qsort_bsearch`, `948_qsort_stable`. No new failures or timeouts appeared.

Full logs are stored beside this report as `tree_a0.log`, `tree_a1.log`, `linear_a0.log`, and `linear_a1.log`.

## A/B performance

15 alternating paired runs, median wall time in milliseconds; `before` is the immediately preceding source state reconstructed from the prior cumulative delta, `after` is this implementation. All runs returned code 0.

| ACCEL | Benchmark | Before ms | After ms | Delta |
|---:|---|---:|---:|---:|
| 0 | string_scan | 264.886 | 253.283 | -4.4% |
| 0 | string_scan_bounded | 299.431 | 300.386 | +0.3% |
| 0 | struct_score | 62.947 | 62.624 | -0.5% |
| 0 | dfs_rec | 23.765 | 22.335 | -6.0% |
| 1 | string_scan | 5.108 | 4.939 | -3.3% |
| 1 | string_scan_bounded | 6.152 | 6.126 | -0.4% |
| 1 | struct_score | 1.616 | 1.681 | +4.0% |
| 1 | dfs_rec | 10.234 | 10.212 | -0.2% |

The `struct_score` ACCEL=1 case is very short (~1.6 ms), so the initial 15-pair sample showed a noise-sensitive +4.0%. A separate 31-pair alternating confirmation measured 1.548 ms before and 1.489 ms after (-3.8%), so the apparent regression did not persist. Across the confirmed measurements, results are neutral-to-improved. No benchmark-specific fast path was introduced.

## Build and platform

Linux/GCC build succeeded without warnings in the captured build log. MSVC was not run in this environment.
