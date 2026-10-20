# Linear STRICT closure: 062_initializers — 2026-10-19

## Baseline and scope

Exclusive base: `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`). This delta accumulates all previously verified changes through `alignment_extended`, plus this initializers fix. No pre-existing `.expect` was modified. New regression test: `1012_global_array_size_row_decay.c/.expect`.

## Root-cause proof: dump before the fix

Command:
```
cd src/tests
env BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0 BABYC_VM_TRACE=1 BABYC_DUMP_LIN=1 BABYC_VM_PROFILE=1 ../babyc 062_initializers.c
```
Before the fix, the body had already lowered to 386 Linear instructions. The dump ended the eligibility check with:
```
[lin-ir] function=main ... instructions=386
[vm-rej] ptr-indexed
062_initializers.c:1:18 strict: tree fallback (body)
[babyc-bytecode-vm-prod] attempts=0 runs=0
```
There was no `lower_reject` or `compile_reject` before this first rejection: the actual cause of the initial STRICT fallback was `FnCodeIndexedPointerParamsSafe()` in `src/ir_lower.c`. It required every pointer parameter to satisfy the indexed-pointer safety rule, even when that parameter was never referenced. `main`'s `char **argv` was unused, but the gate rejected the body based solely on its type. The body dump has no `IrLinLoadLocal` of `argv` and no indexed metadata referencing that parameter.

## General fix and latent semantic defects exposed

1. `src/ir_lower.c`: `FnCodeIndexedPointerParamsSafe()` now applies its stricter pointee/index constraints only when the lowered body actually references that pointer parameter (local-load metadata or an indexed operation tied to that parameter). An unused parameter does not make an otherwise valid body unsafe; referenced pointers still go through the original checks.
2. Once the preflight allowed the body to run, `sizeof(global_array)` was found to remain as a runtime unary node with `ConstVal=0`. Both `sizeof(array)` and `sizeof array` now fold global object size from its declared type via `VariableGet()` and `TypeSize()` in `src/ir_compile.c`.
3. `grid[i]` for a global multidimensional array was also treated as an access to its first scalar rather than array-to-pointer row decay. `src/ir_lin_emit.c`, `src/ir_lin.h`, and `src/ir_lin_exec.c` now use a dedicated Linear load mode (`IR_LIN_IDX_ROW_DECAY`) to compute the selected row address using the declared outer count and row stride, retaining runtime null/bounds checks. Ordinary global scalar indexing and the existing multidimensional indexing path remain unchanged.

These are general compiler corrections exposed after removing the earlier false rejection, not test-name or source-shape fast paths.

## Before/after evidence

Before: `ptr-indexed`, fallback, 386 instructions; test output was therefore Tree fallback output, not proof of Linear support.

After: `main` lowers to 387 instructions and the called function to 72 instructions. `062_initializers` runs in Linear STRICT with ACCEL=0 and 1, exit code 0, output exactly 1270 bytes and byte-for-byte equal to the existing `062_initializers.expect`. The final dump contains no `ptr-indexed`, `lower_reject`, `compile_reject`, `strict: tree fallback`, or VM production-bytecode execution.

The new `1012_global_array_size_row_decay` regression test prints `8 cat a dog o` in all four Tree/Linear STRICT x ACCEL=0/1 configurations. Existing `.expect` files are byte-for-byte unchanged; only the new test's expect file was added. Numbered source IDs are unique and contiguous from `001` through `1012`.

## Build and complete matrix

Commands:
```
make -C src clean
make -C src -j2
python3 tools/run_numbered_matrix.py --path tree --accel 0
python3 tools/run_numbered_matrix.py --path tree --accel 1
python3 tools/run_numbered_matrix.py --path linear --strict --accel 0
python3 tools/run_numbered_matrix.py --path linear --strict --accel 1
```

Final Linux/GCC clean build: exit code 0, zero warnings/errors.

```
SUMMARY tree_accel0: PASSED=1004 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1004 SECONDS=4.46
FAILURES: none
SUMMARY tree_accel1: PASSED=1004 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1004 SECONDS=4.19
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1000 FAILED=4 TIMEOUT=0 SKIPPED=0 TOTAL=1004 SECONDS=3.84
FAILURES: 047_grep.c 058_macro_bug.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c
SUMMARY linear_accel1_STRICT: PASSED=1000 FAILED=4 TIMEOUT=0 SKIPPED=0 TOTAL=1004 SECONDS=3.70
FAILURES: 047_grep.c 058_macro_bug.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c
```

The same clean build and matrix were run again on a fresh copy reconstructed by applying the final cumulative ZIP (after processing `DELETE_FILES.txt`); the final reported counts and residual identities are unchanged.

## Performance A/B

Baseline and candidate binaries were built separately from the original `loop_promotion` base and final candidate, respectively. Seven alternating AB/BA pairs were run per benchmark/configuration, pinned to one available CPU, with a warm-up. Each output/exit code was checked against the known result. The initial complete A/B has seven alternating pairs in all eight configurations. The full raw samples are in `initializers_perf_ab_samples_2026-10-19.csv`; its summary is in `initializers_perf_ab_summary_2026-10-19.csv`. Short ACCEL=1 workloads showed substantial timing noise, so I retained the initial sample and performed targeted 21-pair repeats for `string_scan_bounded` ACCEL=1 and `dfs_rec` ACCEL=1, plus a 21-pair incremental comparison of the current patch against the immediately previous cumulative build.

| Benchmark | ACCEL | Pairs | Baseline median ms | Candidate median ms | Median delta | Median paired delta | IQR of paired delta |
|---|---:|---:|---:|---:|---:|---:|---:|
| string_scan | 0 | 7 | 312.169 | 292.843 | -6.19% | -6.37% | -6.61%..-5.88% |
| string_scan | 1 | 7 | 5.710 | 5.674 | -0.63% | -1.20% | -7.19%..+2.01% |
| string_scan_bounded | 0 | 7 | 378.058 | 366.728 | -3.00% | -4.02% | -4.71%..-0.84% |
| string_scan_bounded | 1 | 7 | 7.319 | 7.171 | -2.03% | -1.20% | -5.96%..+2.33% |
| struct_score | 0 | 7 | 71.664 | 64.897 | -9.44% | -9.92% | -13.52%..-8.15% |
| struct_score | 1 | 7 | 1.526 | 1.483 | -2.88% | -6.14% | -31.78%..+2.30% |
| dfs_rec | 0 | 7 | 25.225 | 24.763 | -1.83% | -3.68% | -3.79%..+0.49% |
| dfs_rec | 1 | 7 | 10.033 | 10.095 | +0.62% | +0.03% | -5.21%..+3.91% |

The initial seven-pair block has only one positive raw median, `dfs_rec` ACCEL=1 (+0.62%); its paired median is +0.03% with a wide IQR spanning zero.

Supplementary repeated measurements (all outputs still match) are recorded separately:

| Comparison | Pairs | Baseline/prior median ms | Candidate median ms | Raw median delta | Paired median delta | IQR paired |
|---|---:|---:|---:|---:|---:|---:|
| Original baseline vs candidate: `string_scan_bounded`, ACCEL=1, repeat A | 21 | 8.193 | 8.495 | +3.69% | -2.41% | -6.31%..+8.59% |
| Original baseline vs candidate: `string_scan_bounded`, ACCEL=1, repeat B | 21 | 7.248 | 6.995 | -3.49% | -2.74% | -8.22%..-0.78% |
| Original baseline vs candidate: `struct_score`, ACCEL=1 | 21 | 1.624 | 1.574 | -3.07% | -3.59% | -9.28%..+7.81% |
| Original baseline vs candidate: `dfs_rec`, ACCEL=1 | 21 | 9.719 | 9.617 | -1.05% | -0.29% | -2.16%..+0.94% |
| Prior cumulative vs current patch: `string_scan_bounded`, ACCEL=1 | 21 | 6.991 | 7.011 | +0.29% | -0.12% | -5.41%..+9.47% |

The two original-baseline repeat blocks for `string_scan_bounded` disagree on raw medians (+3.69% vs -3.49%) while both paired medians are negative; their broad intervals show strong machine/scheduling noise at ~7-8 ms. The direct comparison of the patch against the immediately previous cumulative build has +0.29% raw delta and -0.12% paired delta, also with a broad IQR. This does not provide evidence of a persistent slowdown caused by this turn's patch, but it does mean the short ACCEL=1 wall-time measurements cannot establish that every sample is faster. I keep both unfavourable and favourable series in the report/CSV and do not suppress outliers. The partial extended all-config runner stopped before its last configuration; the two targeted 21-pair repeats completed independently and are the complete supplementary data.

## Current residuals and limits

Linear STRICT now has four residuals: `047_grep`, `058_macro_bug`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`; Tree is fully passing. There is no evidence that those remaining failures share the `062` root cause, so they were left untouched. MSVC/Windows was not run in this Linux turn; the new code uses standard C and no POSIX headers or GCC-only extensions. Bytecode remains outside scope.
