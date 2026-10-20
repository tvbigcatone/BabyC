# Linear STRICT closure — `058_macro_bug` — 2026-10-19

## Baseline and reconstruction

The exclusive source baseline is `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. The incoming cumulative delta `babyc_cumulative_delta_2026-10-19_initializers_062.zip` has SHA-256 `73267686fc663005e23e1208732f11a3cd1c794795459b99519065db29f173f2`. Reconstruction applied all 2,023 paths from `babyc/DELETE_FILES.txt` (zero missing paths) and overlaid the delta.

## Root-cause proof from the input delta

Command (reproduced from the unmodified incoming cumulative delta):

```sh
cd src/tests
env -i PATH="$PATH" HOME="$HOME" BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0 \
  BABYC_DUMP_LIN=1 BABYC_VM_TRACE=1 BABYC_VM_PROFILE=1 \
  ../babyc 058_macro_bug.c
```

The actual diagnostics were:

```text
[babyc-exec-profile] lower_reject stmt_kind=1 line=10 expr_kind=4 expr_op=0
[babyc-exec-profile] compile_reject=lower body_kind=2 n=0 ok=0 skipped_call=0 calls=0
[vm-rej] compile-null
058_macro_bug.c:1:6 strict: tree fallback (body)
```

Line 10 is `z = MIN(y, 13.5);`. `MIN` expands to `((y) < (13.5) ? (y) : (13.5))`. The lowering failed inside this ordinary scalar assignment, before code was produced for its body. The underlying general mismatch was that the Linear FP gate trusted `IR_TF_FP` as if it always meant “this expression's value is FP”: current expression annotation propagates that flag through relational predicates, whose result is integer, and the gate excluded FP-valued conditional expressions. The macro combines both cases: a mixed integer/double relation and a conditional whose arms need a common FP value type.

The before/after console and IR dumps are preserved in `linear_macro058_before_a0.log`, `linear_macro058_before_a1.log`, `linear_macro058_after_a0.log`, and `linear_macro058_after_a1.log`.

## General fix

- `src/ir_lin_emit.c`: `LinExprHasFpValue()` now distinguishes an expression's value type from propagated operand annotations. Relational/logical operations are integer-valued; a ternary is classified from its value arms; a comma expression uses its right operand. Linear STRICT permits a conditional FP expression and emits int-to-double conversions on non-FP arms before the result is merged.
- `src/ir_lin_emit.c` and `src/ir_lin_exec.c`: a mixed FP relational operator promotes its integer operand to the Linear double-bit representation, sets the FP operand flag, and compares the two IEEE double values for `==`, `!=`, `<`, `>`, `<=`, `>=`, returning the integer predicate. Both the production and light executor loops implement the same comparison semantics.
- `src/ir_lin_emit.c` / `src/ir_lin_emit.h` / `src/ir_lin_compile.c`: shared `IrLinEmitScalarConversion()` applies implicit scalar conversion to plain scalar local/parameter assignments and scalar local initializers: integer-to-FP uses the existing double-bit representation and FP-to-integer truncates toward zero. Pointer, array, and aggregate declarations are left unchanged. The conversion is compile-time emitted only when Linear STRICT is enabled and the declared destination type actually requires it.
- New test `1013_conditional_fp_mixed.c/.expect` verifies the two branches (`low=13.5`, `high=14.0`), integer-to-double initialization and double-to-integer truncation. No existing `.expect` is modified and no test-specific shortcut/fallback is present.

## Targeted results

`058_macro_bug.c` output matching the existing `.expect` after normalizing CRLF to LF; the content and trailing space are unchanged:

```text
Macro test: 1 13 13.500000 3 
```

It now runs in Linear STRICT at `BABYC_ACCEL=0` and `BABYC_ACCEL=1`. The after-dump contains no `lower_reject`, `compile_reject`, `vm-rej`, or `strict: tree fallback`. Test 1013 outputs `13.500000 14.000000 2.000000 3` and matches Tree in all four matrix configurations.

## Clean build and full numbered matrix

Final reconstructed bundle: 1,013 contiguous numbered C sources (`001..1013`), 1,005 `.expect` tests and eight source-only cases. Linux/GCC clean build uses the project `-Wall -O2` Makefile and completes with zero warnings/errors.

```text
SUMMARY tree_accel0: PASSED=1005 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1005
FAILURES: none
SUMMARY tree_accel1: PASSED=1005 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1005
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1002 FAILED=3 TIMEOUT=0 SKIPPED=0 TOTAL=1005
FAILURES: 047_grep.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c
SUMMARY linear_accel1_STRICT: PASSED=1002 FAILED=3 TIMEOUT=0 SKIPPED=0 TOTAL=1005
FAILURES: 047_grep.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c
```

The three Linear STRICT failures are the same non-target residuals previously present except for the closed `058_macro_bug`; no timeouts or additional failures. Exact command output is in `matrix_macro058_tree_a0.log`, `matrix_macro058_tree_a1.log`, `matrix_macro058_linear_a0.log`, and `matrix_macro058_linear_a1.log`; the matrix is also rerun after a fresh extraction of the final ZIP as part of this turn's final validation.

## A/B performance checks

All runs use separately built binaries, one CPU affinity where available, alternating AB/BA order, and output/exit-code verification. `BABYC_PATH=linear` is used without STRICT for these four benchmark workloads: `string_scan` is itself rejected by Linear STRICT today, so measuring it under STRICT would benchmark the error/fallback path rather than the requested workload. ACCEL=0 and ACCEL=1 each have seven alternating pairs for all four benchmarks. Results are wall-clock medians, not performance guarantees.

### Original baseline vs cumulative candidate — 7 AB/BA pairs per configuration

| Benchmark | ACCEL | Baseline ms | Candidate ms | Raw median delta | Paired median delta | Paired IQR |
|---|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 342.077 | 307.576 | -10.09% | -10.09% | -10.46%..-7.72% |
| `string_scan` | 1 | 6.818 | 7.232 | +6.07% | +12.62% | -20.15%..+17.05% |
| `string_scan_bounded` | 0 | 392.575 | 376.783 | -4.02% | -1.24% | -3.57%..+1.38% |
| `string_scan_bounded` | 1 | 7.084 | 7.007 | -1.09% | -1.90% | -3.40%..+0.18% |
| `struct_score` | 0 | 70.336 | 63.876 | -9.19% | -9.19% | -10.57%..-8.61% |
| `struct_score` | 1 | 1.434 | 1.434 | +0.05% | -0.08% | -5.52%..+32.26% |
| `dfs_rec` | 0 | 26.112 | 25.476 | -2.44% | -1.28% | -2.39%..+4.13% |
| `dfs_rec` | 1 | 10.194 | 9.954 | -2.36% | -1.79% | -6.57%..+0.91% |

This comparison includes all earlier cumulative changes and cannot attribute its deltas to the 058 fix.

### Input cumulative delta vs this-turn patch — 7 AB/BA pairs per configuration

| Benchmark | ACCEL | Input ms | Patched ms | Raw median delta | Paired median delta | Paired IQR |
|---|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 304.647 | 326.265 | +7.10% | +8.46% | -0.61%..+21.05% |
| `string_scan` | 1 | 6.217 | 6.568 | +5.63% | +6.04% | +1.60%..+8.67% |
| `string_scan_bounded` | 0 | 362.193 | 377.403 | +4.20% | +3.55% | -4.20%..+5.44% |
| `string_scan_bounded` | 1 | 7.818 | 7.945 | +1.62% | +3.36% | +0.83%..+4.49% |
| `struct_score` | 0 | 66.800 | 66.638 | -0.24% | -1.05% | -1.73%..-0.34% |
| `struct_score` | 1 | 1.621 | 1.564 | -3.54% | -2.07% | -6.03%..+6.51% |
| `dfs_rec` | 0 | 24.835 | 24.334 | -2.02% | -2.06% | -2.91%..+1.01% |
| `dfs_rec` | 1 | 10.256 | 9.900 | -3.46% | +0.34% | -3.79%..+1.13% |

The initial paired series has positive short-workload medians; they must not be hidden. Targeted 21-pair repeats on the two scan benchmarks give:

| Benchmark | ACCEL | Input median ms | Patched median ms | Raw delta | Paired median delta | Paired IQR |
|---|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 307.605 | 311.762 | +1.35% | +1.49% | -2.46%..+4.80% |
| `string_scan` | 1 | 6.172 | 6.294 | +1.99% | +0.88% | -11.82%..+9.58% |
| `string_scan_bounded` | 0 | 364.949 | 368.382 | +0.94% | +0.41% | -0.63%..+1.41% |
| `string_scan_bounded` | 1 | 7.787 | 7.767 | -0.25% | +4.15% | -4.48%..+4.89% |

Outputs were identical in every timed run. The 21-pair series does not establish a reproducible slowdown beyond the observed timing dispersion, but the short ACCEL=1 paired deltas remain noisy and are not proof of zero overhead. Full raw samples/summary are in `linear_macro058_perf_ab_samples_2026-10-19.csv`, `linear_macro058_perf_ab_summary_2026-10-19.csv`, `linear_macro058_incremental_perf_ab_samples_2026-10-19.csv`, `linear_macro058_incremental_perf_ab_summary_2026-10-19.csv`, and `linear_macro058_incremental_perf_repeat21_2026-10-19.csv`. Ancillary Tree-path timings are separately saved with `*_tree_*` filenames and are not used to assess the Linear change.

## Limits and residuals

- Tree is the correctness oracle and stays the default.
- Remaining Linear STRICT residuals: `047_grep`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`.
- A clean Linux/GCC build and full matrix are verified. No MSVC build was available in this execution environment; the code introduced here uses standard C and no POSIX headers or GCC-only extensions, but that is not a claim of a tested MSVC build.
- Compact bytecode VM was not in scope; its `attempts=0 runs=0` counter is unrelated to whether the Linear STRICT body ran.
