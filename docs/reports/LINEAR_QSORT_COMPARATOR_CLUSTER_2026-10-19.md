# Linear STRICT — comparator `const void *`, qsort/bsearch (2026-10-19)

## Baseline and scope

Source of truth: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`. This update follows the already verified cumulative renumbering/performance/member-call deltas. No existing `.expect` file was changed. Added test `1010_qsort_voidptr_member_offset.c/.expect`, extending the sequence contiguously from `1009` to `1010`.

## Root cause proven by before/after trace and IR

The callbacks have ordinary C comparator signatures, for example:

```c
int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}
int cmp_rec(const void *a, const void *b)
{
    return ((const rec *)a)->k - ((const rec *)b)->k;
}
```

Before the fix, the strict call-body preflight in `IrTryExecBodyVm()` rejected `TypePointer` whose `FromType->Base` was `TypeVoid`, before the comparator body was eligible for Linear lowering. After allowing `TypeVoid` through that *first* preflight, a second rejection remained: `FnCodeIndexedPointerParamsSafe()` only understood pointer parameters used as index roots (and listed scalar/struct/union/array/pointer pointees), so it rejected the same parameters as `ptr-indexed`. The previous traces show this early body rejection rather than emitted comparator IR:

- `qsort_comparator_pre_fix_947_2026-10-19.log`: repeated `947_qsort_bsearch.c:1:0 strict: tree fallback (body)`; later `bsearch` reaches `947_qsort_bsearch.c:23:15 uninitialized variable '?'`.
- `qsort_comparator_pre_fix_948_2026-10-19.log`: repeated `948_qsort_stable.c:1:0 strict: tree fallback (body)` and fallback while processing the output loop.

The general fix has two parts:

1. The existing pointer-type preflight accepts a `void *` pointee so later analysis can prove whether the body is safe, instead of rejecting based on the declared type alone.
2. `FnCodeVoidParamTypedAccessSafe()` walks the comparator AST and validates every use of that particular `void *` parameter. A use is accepted only if it appears under an explicit cast to a typed pointer with known element size, and that cast is immediately consumed as a dereference, member access (`->field`), or index. Unsupported escape, untyped arithmetic, or other use-shapes remain rejected. This is type/use based; it does not inspect the callback's name, test ID, or `qsort`/`bsearch` identity. All non-`void *` pointer parameters still pass through the existing stricter analysis unchanged.

The post-fix dump now contains comparator bodies, not just the main function and fallback messages. For `cmp_int`:

```text
[lin-ir] function=cmp_int function_ptr=... instructions=7
[lin-ir] 0001 op=2 dest=1 ... imm=-1
[lin-ir] 0002 op=11 dest=2 a=1 ... tok=2 imm=4
[lin-ir] 0003 op=2 dest=3 ... imm=-2
[lin-ir] 0004 op=11 dest=4 a=3 ... tok=2 imm=4
[lin-ir] 0005 op=4 dest=0 a=2 b=4 ...
```

The parameter loads are followed by typed 4-byte index/load operations (`tok=2`, `imm=4`), then the subtraction and return. `cmp_rec` and `cmp_k` likewise have emitted 7-instruction bodies and typed member loads. This was the divergence: before, the body was rejected and `bsearch` read an uninitialised result; after, each callback is admitted and run under Linear, with no tree fallback.

Post-fix runtime trace summary from `947_qsort_bsearch`:

```text
[babyc-exec-profile] attempts=32 body_runs=32 body_run_fail=0 reject_depth=0 reject_ptr=0 reject_small=0 compile_fail=0 ineligible=0 tiny_tree=0 run_nested=31 run_top=1 ptr_index_admit=31
[babyc-bytecode-vm-prod] attempts=0 runs=0
```

The complete before/after dumps are preserved as `qsort_comparator_pre_fix_947_2026-10-19.log`, `qsort_comparator_pre_fix_948_2026-10-19.log`, `qsort_comparator_post_fix_947_2026-10-19.log`, and `qsort_comparator_post_fix_948_2026-10-19.log`.

## Regression test and actual output

Added `1010_qsort_voidptr_member_offset` with a comparator that casts `const void *` to `const Item *` and reads `->key` at a non-zero struct-member offset. This covers the member offset, not only scalar `*(const int *)p` or first-field access. Its expected output is `3:1 3:3 7:2 9:0 `.

Actual outputs and exit codes were compared directly in Tree and Linear STRICT for ACCEL=0/1 and match byte-for-byte:

```text
947_qsort_bsearch:
0 1 2 3 5 7 8 9 
5
-1
0:0 1:5 2:4 3:3 4:2 5:1 

948_qsort_stable:
0:0 0:3 0:6 0:9 1:1 1:4 1:7 1:10 2:2 2:5 2:8 2:11 

1010_qsort_voidptr_member_offset:
3:1 3:3 7:2 9:0 
```

The command and all four configurations for each targeted test are recorded in `qsort_target_outputs_2026-10-19.txt`.

## Build and full matrix

Commands:

```sh
make -C src clean
make -C src -j2
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path tree --accel 0 --log docs/reports/matrix_qsort_tree_accel0_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path tree --accel 1 --log docs/reports/matrix_qsort_tree_accel1_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path linear --strict --accel 0 --log docs/reports/matrix_qsort_linear_accel0_STRICT_2026-10-19.log
env -i PATH="$PATH" HOME="$HOME" python3 tools/run_numbered_matrix.py --path linear --strict --accel 1 --log docs/reports/matrix_qsort_linear_accel1_STRICT_2026-10-19.log
```

Build result: exit code 0, zero warnings/errors. The exact matrix summaries were:

```text
SUMMARY tree_accel0: PASSED=1002 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=4.13
SUMMARY tree_accel1: PASSED=1002 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=3.93
SUMMARY linear_accel0_STRICT: PASSED=995 FAILED=7 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=3.54
SUMMARY linear_accel1_STRICT: PASSED=995 FAILED=7 TIMEOUT=0 SKIPPED=0 TOTAL=1002 SECONDS=3.32
```

Tree is fully green. The exact remaining Linear STRICT failures, identical for ACCEL=0/1, are `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`, `873_alignment_extended`, `893_vm_struct_return_effect_fallback`. No new failures and no timeouts. The 9→7 reduction comes from closing `947` and `948`; the extra test `1010` passes.

## Performance A/B

Both executables were built separately from the original baseline ZIP and the cumulative candidate using the same GCC flags. Each benchmark/configuration got one warm-up and at least seven alternating baseline/candidate AB/BA pairs, pinned to CPU 0, `BABYC_PATH=linear`, `BABYC_STRICT=1`; output and exit status were identical. Raw data is in `qsort_perf_ab_samples_2026-10-19.csv`. Four ACCEL=1 workloads were additionally sampled for 21 more alternating pairs each (28 total), in `qsort_perf_ab_accel1_repeat_samples_2026-10-19.csv`. CSV summaries are adjacent.

### Initial required seven pairs

| Benchmark | ACCEL | Baseline median (ms) | Candidate median (ms) | Delta of medians | Median paired delta |
|---|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 323.865 | 312.717 | −3.44% | −1.75% |
| `string_scan_bounded` | 0 | 380.221 | 368.001 | −3.21% | −4.25% |
| `struct_score` | 0 | 71.120 | 67.966 | −4.43% | −4.95% |
| `dfs_rec` | 0 | 24.885 | 24.197 | −2.76% | −3.02% |
| `string_scan` | 1 | 6.009 | 5.949 | −1.01% | −3.63% |
| `string_scan_bounded` | 1 | 6.476 | 6.427 | −0.76% | −1.48% |
| `struct_score` | 1 | 2.318 | 2.370 | +2.26% | +3.83% |
| `dfs_rec` | 1 | 10.186 | 10.127 | −0.58% | +0.12% |

### ACCEL=1 extended (21 additional pairs; 28 total)

| Benchmark | Baseline median (ms) | Candidate median (ms) | Delta of medians | Median paired delta | IQR of paired deltas |
|---|---:|---:|---:|---:|---:|
| `string_scan` | 5.872 | 5.811 | −1.04% | −3.07% | −10.24% … +5.37% |
| `string_scan_bounded` | 6.912 | 6.724 | −2.71% | −1.10% | −4.07% … +12.50% |
| `struct_score` | 2.199 | 2.175 | −1.10% | +0.35% | −10.82% … +6.49% |
| `dfs_rec` | 10.115 | 10.144 | +0.29% | −0.17% | −2.55% … +3.88% |

The apparent `struct_score` ACCEL=1 regression in the first seven-pair sample does not persist in the additional pairs (combined paired median is +0.35%, while the median of raw times improves by 1.10%, with a wide IQR). `dfs_rec` ACCEL=1 is +0.29% on raw medians and −0.17% on paired deltas. Timing dispersion for the ACCEL=1 cases in the 2–10 ms range is substantial; all observations are retained rather than dropping outliers. No benchmark exhibits a persistent >1% slowdown distinguishable from the measured noise. Linux/GCC only; MSVC/Windows was not available in this turn.

## Separate open issue: 893

`893_vm_struct_return_effect_fallback` remains open. Its current trace reports `compile_reject=lower body_kind=2 n=19 ok=1 skipped_call=1 calls=2`, followed by `[vm-rej] compile-null`. That is an aggregate-return/call-containing lowering path; it does not involve `TypeVoid` comparator parameters, nor does it enter `FnCodeVoidParamTypedAccessSafe()`. The cause is therefore distinct, and this turn deliberately leaves it untouched rather than widening the qsort fix speculatively. Evidence: `qsort_893_still_open_2026-10-19.log`.

No POSIX-only header or OS-specific symbol was introduced. Existing `.expect` files are unchanged; `1010_qsort_voidptr_member_offset.expect` is new. Bytecode was not changed. The remaining seven STRICT failures are documented above; Tree remains the default until they are closed.


## Aggiornamento successivo — 2026-10-19: residuo 893 chiuso

Il report fu scritto quando `893_vm_struct_return_effect_fallback` era ancora aperto. Il turno successivo ha dimostrato e corretto la causa nella gestione di `struct S v[1]` non inizializzato: il lowering array richiedeva `DeclArraySize > 1`, quindi una lunghezza valida pari a uno veniva trattata come dichiarazione saltata. Il fix finale passa `893` in Linear STRICT con ACCEL=0/1; i residui attuali sono sei (`047`, `058`, `062`, `126`, `651`, `873`). Dettagli, dump e benchmark nel report `LINEAR_STRUCT_RETURN_ARRAY_CLUSTER_2026-10-19.md`.
