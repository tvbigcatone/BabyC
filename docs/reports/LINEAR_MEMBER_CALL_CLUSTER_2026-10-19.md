# Linear STRICT — cluster member access / call arguments

Date: 2026-10-19

## Baseline and scope

Source baseline: `babyc_baseline_2026-10-18_loop_promotion.zip` (the project was reconstructed from this archive and the previous cumulative delta was applied before working). The three prioritized tests are `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, and `618_inc_member_call_arg`. The shared case `454_complex_dispatch` was also closed. No `.expect` file was modified and no new test was added: the four existing tests directly cover the generalized fixes. The bytecode backend was not changed or validated by this work.

## Root causes and fixes

### 606 — `++*(ps->p)` and indirect member calls

On the pre-fix cumulative build, Tree executed the test but Linear STRICT rejected the function body. In `src/ir_lin_emit.c`, `LinExprValueType()` treated `->` as valid only if the recorded type was `TypePointer`/`TypeArray` with a non-null `FromType`. For address-backed local pointer slots, the live metadata can already be the pointee aggregate (`TypeStruct`/`TypeUnion`) while the AST node still has `IR_TF_PTR`. That caused the resolver to return `NULL` for `ps->p`, so the generic increment lowering could not determine the pointed-to scalar type and rejected the call argument. The fix recognizes this existing metadata representation and still requires the AST pointer flag; it does not accept an arbitrary non-pointer aggregate.

A generalized lvalue lowering path now handles pre/post increment/decrement through a pointer-valued expression, including member expressions, by lowering the pointer expression once, loading the scalar with the typed indirect-load path, applying the C increment step, and storing back through the pointer. The result of prefix increment on narrow integer types is reloaded after assignment conversion. The store uses a separate `IrLinStoreDeref` opcode; line/column packing therefore does not add a condition to the common `IrLinStoreIndex Tok=2` path.

### 618 — `++*(t.p)` and `++ps->v` used as call arguments

This test exercised both an indirect lvalue rooted at a struct member and an arrow-member lvalue inside an indirect call. The first was rejected because the old increment lowering only admitted a direct local-pointer identifier; the second encountered the same `->` metadata mismatch as 606. The generic lvalue path and shared member-type resolver close both cases. Argument evaluation happens once; no Tree evaluation or fallback is introduced.

### 609 — ternary function addresses stored in a union

The `&inc` / `&dec` arm of the ternary was emitted as the address of `GV->Val`. But indirect function-pointer slots and dispatch compare/use the identity of the function `Value` itself, not the address of its metadata payload. The address-of lowering now emits `IrLinImm Tok=5` with the `Value *` identity, matching the existing encoding used by indirect-call dispatch. It is not a name-based special case and covers function designators generally.

### 454 — indexed function-pointer arguments in a complex dispatch

The strict-call filter rejected `IrExIndex` and `IrExMember` by syntax even when the selected element is a valid scalar/pointer rvalue argument. The generalized eligibility check admits values whose resolved type is scalar/pointer, while continuing to reject aggregates that require by-value blob/copy ABI handling. For local arrays of function pointers whose metadata stores pointer-ness on the array root (and only stride/type facts on the index), it uses the root's declared pointer flag and element stride. This permits `ops[0]` / `ops[i]` without broadly accepting arbitrary aggregates.

## Before / after evidence

The pre-fix build was reconstructed from the original baseline plus the previous cumulative delta. With `BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0 BABYC_DUMP_LIN=1`, the strict runner rejected the prioritized tests (`rc=1`, `strict: tree fallback (body)`); the pre-fix test 609 dump ended after emitting the ternary function-address arms and indirect call setup. These were compile/lowering rejections, not runtime output mismatches that a fallback concealed. The exact pre-fix and post-fix `BABYC_DUMP_LIN=1` evidence is preserved in `docs/reports/cluster_member_calls_pre_fix_dump_2026-10-19.log`: for test 609, the relevant instructions changed from `op=1 ... tok=0` to `op=1 ... tok=5` for `&inc` and `&dec`, matching the expected function-Value identity. The old dump has no emitted body IR for 606/618/454 because those bodies are rejected before IR is produced; the code-path divergence is at `LinExprValueType()`'s `TokenArrow` metadata guard and `LowerRecImpl()`'s old local-identifier-only increment path / strict call-argument gate.

Final direct runs used `BABYC_PATH=tree` and `BABYC_PATH=linear BABYC_STRICT=1`, for ACCEL=0 and ACCEL=1. All return code 0; stdout is byte-identical and strict stderr contains neither `tree fallback` nor `uninitialized variable`:

| Test | Tree stdout | Linear STRICT stdout | Result |
|---|---|---|---|
| `606_storm_inc_dec_member_call` | `a -4\nb 4\nc 42\nd 21\ne 24\nf -24\n` | identical | PASS, no fallback |
| `609_storm_enum_union_fptr` | `11\n10\n` | identical | PASS, no fallback |
| `618_inc_member_call_arg` | `-4\n4\n22\n11\n10\n5\n` | identical | PASS, no fallback |
| `454_complex_dispatch` | `14\n60\n5\n22\n` | identical | PASS, no fallback |

The full before/after command output for each test is preserved as `docs/reports/target_<test>_2026-10-19.txt`.

## Build and full matrix

Commands:

```sh
make -C src clean
make -C src -j2

env -i PATH="$PATH" HOME="$HOME" BABYC_PATH=tree BABYC_STRICT=0 BABYC_ACCEL=0 python3 tools/run_numbered_matrix.py --path tree --accel 0
env -i PATH="$PATH" HOME="$HOME" BABYC_PATH=tree BABYC_STRICT=0 BABYC_ACCEL=1 python3 tools/run_numbered_matrix.py --path tree --accel 1
env -i PATH="$PATH" HOME="$HOME" BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=0 python3 tools/run_numbered_matrix.py --path linear --accel 0 --strict
env -i PATH="$PATH" HOME="$HOME" BABYC_PATH=linear BABYC_STRICT=1 BABYC_ACCEL=1 python3 tools/run_numbered_matrix.py --path linear --accel 1 --strict
```

Clean build output: `BUILD_RC=0`, `WARNINGS=0`, `ERRORS=0`.

Real matrix summaries:

```text
SUMMARY tree_accel0: PASSED=1001 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=4.06
FAILURES: none
SUMMARY tree_accel1: PASSED=1001 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=3.83
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=992 FAILED=9 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=3.43
FAILURES: 047_grep.c 058_macro_bug.c 062_initializers.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c 873_alignment_extended.c 893_vm_struct_return_effect_fallback.c 947_qsort_bsearch.c 948_qsort_stable.c
SUMMARY linear_accel1_STRICT: PASSED=992 FAILED=9 TIMEOUT=0 SKIPPED=0 TOTAL=1001 SECONDS=3.46
FAILURES: 047_grep.c 058_macro_bug.c 062_initializers.c 126_csmith_modulo_zero.c 651_sqlite_exec_null_varargs.c 873_alignment_extended.c 893_vm_struct_return_effect_fallback.c 947_qsort_bsearch.c 948_qsort_stable.c
```

The 13 previous residuals fell to 9: three requested tests plus `454_complex_dispatch` now pass. No new failures or timeouts were introduced. The logs are `matrix_member_calls_{tree_accel0,tree_accel1,linear_accel0_STRICT,linear_accel1_STRICT}_2026-10-19.log`.

## Performance A/B

Baseline and candidate were separately built with `make clean && make -j2`; each benchmark was warmed once per binary/configuration, then measured in 7 alternating AB/BA pairs pinned to CPU 0. Environment was cleared of inherited `BABYC_*` variables; both sides used `BABYC_PATH=linear`, `BABYC_STRICT=1`, and the requested `BABYC_ACCEL`. Exit status and stdout were checked for every run; all 56 measured baseline/candidate outputs matched exactly. Raw samples are in `cluster_member_calls_perf_samples_2026-10-19.csv`; summary JSON records paired deltas and IQR.

| Benchmark | ACCEL | Baseline median ms | Candidate median ms | Median-of-pair deltas | Paired IQR |
|---|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 310.347 | 298.640 | −4.44% | −4.87% … −3.04% |
| `string_scan_bounded` | 0 | 381.685 | 376.638 | −0.95% | −3.83% … +1.20% |
| `struct_score` | 0 | 74.277 | 70.934 | −8.09% | −10.04% … −0.24% |
| `dfs_rec` | 0 | 26.927 | 26.458 | −1.79% | −3.25% … −0.15% |
| `string_scan` | 1 | 7.149 | 6.880 | −3.04% | −7.76% … −1.04% |
| `string_scan_bounded` | 1 | 8.116 | 8.185 | +0.86% | −4.11% … +6.09% |
| `struct_score` | 1 | 3.491 | 3.333 | −2.99% | −8.35% … +5.90% |
| `dfs_rec` | 1 | 11.529 | 11.332 | −0.67% | −3.76% … −0.05% |

No combination shows a persistent regression beyond +1%. The only positive median-of-pair result is `string_scan_bounded` ACCEL=1 at +0.86%; its paired interval crosses zero widely and the paired IQR spans −4.11% to +6.09%, so it is not evidence of a stable >1% regression. Short ACCEL=1 tests are noisy; no execution was dropped from the CSV.

A performance-related implementation detail was kept out of the shared hot path: the first implementation encoded source positions in `Imm` and decoded them conditionally in every `IrLinStoreIndex Tok=2`. That condition is now confined to `IrLinStoreDeref`, emitted only for this generalized lvalue case; the established `IrLinStoreIndex Tok=2` takes its element size directly from `Imm` again.

## Remaining work

Nine Linear STRICT failures remain: `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `651_sqlite_exec_null_varargs`, `873_alignment_extended`, `893_vm_struct_return_effect_fallback`, `947_qsort_bsearch`, `948_qsort_stable`. The shared evidence suggests different causes for the last three: struct-return-to-array-element assignment (`893`) and comparator/cast/dereference lowering (`947/948`). They were not hidden or marked as fixed. MSVC/Windows was not run in this Linux/GCC turn; no POSIX-only include was added. Bytecode was intentionally not changed.
