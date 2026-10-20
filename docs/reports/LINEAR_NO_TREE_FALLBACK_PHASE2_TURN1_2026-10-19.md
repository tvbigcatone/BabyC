# BabyC — Fase 2, turno 1: Linear non-STRICT senza fallback Tree

**Data:** 2026-10-19  
**Baseline cumulativa:** `babyc_baseline_2026-10-18_loop_promotion.zip` + cumulativo Phase 1 verificato dall'utente (`f8ca17cb…2b5b`).  
**Obiettivo:** `tree_body=0` su tutti i test numerati con Linear non-STRICT, ACCEL 0 e 1; nessun `.expect` esistente modificato.

## Esito

Obiettivo funzionale raggiunto sulla ricostruzione finale: tutte e sei le matrici passano **1009/1009**, e il gate permanente Linear non-STRICT riporta **zero fallback Tree** a entrambi i livelli ACCEL. Build Linux/GCC pulita: **0 warning, 0 errori**.

```text
SUMMARY tree_accel0: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=11.48
FAILURES: none
SUMMARY tree_accel1: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=10.71
FAILURES: none
SUMMARY linear_accel0_STRICT: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=10.36
FAILURES: none
SUMMARY linear_accel1_STRICT: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=10.51
FAILURES: none
SUMMARY linear_accel0: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=23.84
TREE_FALLBACK_GATE: enabled violations=0 tree_body=0
COVERAGE_TOTALS: tests_reported=995 lin_inst=365206505 tree_stmt=0 tree_expr=0 fncode_body=1023 tree_body=0
FAILURES: none
SUMMARY linear_accel1: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=9.47
TREE_FALLBACK_GATE: enabled violations=0 tree_body=0
COVERAGE_TOTALS: tests_reported=995 lin_inst=334507512 tree_stmt=0 tree_expr=0 fncode_body=1134 tree_body=0
FAILURES: none
```

Per rispondere al dubbio su STRICT e sull'eventuale invisibilità dei contatori, ho ripetuto anche le due matrici Linear STRICT con `BABYC_COV=1` (senza abilitare il gate riservato a non-STRICT):

```text
SUMMARY linear_accel0_STRICT: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=11.30
COVERAGE_TOTALS: tests_reported=995 lin_inst=368303571 tree_stmt=0 tree_expr=0 fncode_body=1023 tree_body=0
FAILURES: none
SUMMARY linear_accel1_STRICT: PASSED=1009 FAILED=0 TIMEOUT=0 SKIPPED=0 TOTAL=1009 SECONDS=12.25
COVERAGE_TOTALS: tests_reported=995 lin_inst=337560194 tree_stmt=0 tree_expr=0 fncode_body=1134 tree_body=0
FAILURES: none
```

Quindi per i percorsi **eseguiti** dai 1009 test, STRICT e non-STRICT hanno `tree_body=0`, `tree_stmt=0`, `tree_expr=0`. I 995 `tests_reported` sono le esecuzioni che hanno emesso una riga di contatori; le restanti 14 non hanno generato eventi conteggiabili, e la riga non può più essere soppressa se `tree_body` è positivo. Questo è un risultato dinamico del corpus, non una prova statica che ogni funzione mai chiamata del sorgente sia già abbassabile.

The numbered suite grew from 1008 to 1009 cases because of `src/tests/1017_linear_non_strict_fp_calls.c/.expect`. Comparing against the Phase 1 cumulative tree found **one new `.expect` (1017), zero modified, zero deleted**. The numbered identifiers retain the pre-existing gaps; no test was renumbered.

## Root cause 1 — STRICT and non-STRICT selected different execution bodies

`IrVmStrictOn()` was doing two jobs that must remain separate:

1. deciding whether a Tree fallback is permitted;
2. gating semantic Linear support paths for FP conversion, member/index forms, pointer parameters and related lowerer cases.

Consequently, non-STRICT could decline to compile a body that the Linear compiler already knew how to lower, then execute that body on Tree. STRICT rejected fallback and therefore exposed the Linear path instead. This was not a reliable way to decide semantic capability.

`src/ir_lower.c:880` now provides `IrVmLinearFeaturesOn()` for the second role. Semantic gates in `src/ir_lin_emit.c` and `src/ir_lin_compile.c` use the Linear capability predicate independently of fallback policy. `IrVmStrictOn()` remains in policy/optimization-specific decisions only.

Reproduction counters before the change (from the user's reported runs):

- `024_type_coercion`: STRICT `fncode_body=10, tree_body=0`; non-STRICT `fncode_body=7, tree_body=3`.
- `841_real_a08_text`: STRICT `fncode_body=1, tree_body=0`; non-STRICT `fncode_body=6000, tree_body=1`.

After the change, on each STRICT/non-STRICT × ACCEL 0/1 run:

- `024_type_coercion`: `fncode_body=10, tree_body=0`.
- `841_real_a08_text`: `fncode_body=1, tree_body=0`; Linear emitted instruction count is 698393 at ACCEL 0 and 589166 at ACCEL 1.

Full targeted counters are in `phase2_t1_targeted_selection_and_unroll.log`.

## Root cause 2 — diagnostics could hide a lone Tree body

`IrPrintLowerStats()` in `src/ir_lower.c` returned early when instruction, statement/expression and Linear-body counters were all zero. It did not include `BabycTreeBodyRuns` in the condition. Thus, a test whose only body event was a Tree fallback could omit its `[babyc-cov]` line and look like zero fallback to a text-based gate.

The early-return now also requires `BabycTreeBodyRuns == 0`. The runner parses the original merged output and gates every test that reports a positive `tree_body`; it sums those per-test values into `TREE_FALLBACK_GATE`. It does not infer zero from the absence of semantic `.expect` failures.

## Root cause 3 — unsafe const-trip unroll re-lowered array-index AST nodes

Once the gate exposed hidden fallback events, `623_multidim_3d` and `630_global_2d_index` were reproduced with `BABYC_DUMP_LIN=1`/`BABYC_VM_PROFILE=1`. The rejected expressions were `s += a[i][j][k]` and `sum += A[i][k] * B[k][j]`.

The two const-trip unroll implementations in `src/ir_lin_compile.c` lowered the same AST body repeatedly for each trip. Index/member/call expressions carry mutable cached address/index metadata, and that metadata did not have a general clone/reset contract for repeated lowering. Lowering could therefore fail and allow the non-STRICT caller to fall back to Tree.

`IrConstUnrollExprSafe()` / `IrConstUnrollStmtSafe()` now restrict these unrolls to validated scalar-local/parameter bodies, conservatively rejecting `Index`, `Member`, `Call`, address/dereference and unsupported statement shapes. The normal loop lowerer still handles those bodies. It allows scalar local `++`/`--`, preserving the diagnostic of `978_const_local_increment` at line 7. Both `623_multidim_3d` and `630_global_2d_index` now pass with `tree_body=0` for ACCEL 0/1, STRICT on/off.

## Root cause 4 — array-fill fast kernel could execute a loop initializer twice

`945_sibling_for_same_name` exposed an array-fill kernel path that re-executed the `for` initializer in Tree even after Linear had already emitted it. In `src/ir_fast.c`, `InitStmt` now executes only when `BabycLinExecDepth == 0`; calls from Tree preserve the Tree-side initializer semantics. Direct counters for `945` are `fncode_body=1, tree_body=0` for both ACCEL values and both STRICT settings.

## Permanent no-fallback gate

The runner option is:

```sh
python3 tools/run_numbered_matrix.py --path linear --accel 0 --require-no-tree-fallback
python3 tools/run_numbered_matrix.py --path linear --accel 1 --require-no-tree-fallback
```

It enables `BABYC_COV=1`; each numbered case fails if its output has `tree_body > 0`, even if its ordinary output matches `.expect`. The flag rejects `--path tree` and `--strict` because this gate specifically measures the non-STRICT Linear mode. Final logs:

- `phase2_t1_verified_tree_a0.log`
- `phase2_t1_verified_tree_a1.log`
- `phase2_t1_verified_linear_strict_a0.log`
- `phase2_t1_verified_linear_strict_a1.log`
- `phase2_t1_verified_linear_nonstrict_a0.log`
- `phase2_t1_verified_linear_nonstrict_a1.log`

## Harness determinism: `535_main_continue_after_extract`

The fixture prints `clock()` as `Tempo esecuzione : N ms`, but its unchanged `.expect` hardcodes `0 ms`. Repeated full runs showed 0, 6 and 11 ms while all other output was identical. The runner now normalizes only the numeric field of that time line for this one source file, comparing the remaining transcript normally. No existing `.expect` was altered. The `BABYC_COV` line is read from the raw output before comparison, so this normalization cannot hide any Tree fallback.

## Performance A/B vs the pre-turn 047/126 binary

Commands ran with Linear non-STRICT, both ACCEL modes, alternating AB/BA order, matching stdout hashes, and at least seven pairs in all eight benchmark/configuration cells. Unfavorable initial wall-time cells were extended to 21 or 36 pairs. CPU time was also measured using child-process resource accounting. The CSVs preserve individual samples; do not judge from medians alone.

| Benchmark | ACCEL | Wall pairs | Wall median (047→candidate) | Median paired wall delta | CPU pairs | Median paired CPU delta |
|---|---:|---:|---:|---:|---:|---:|
| `string_scan` | 0 | 7 | −1.84% | +2.44% | 7 | −9.96% |
| `string_scan` | 1 | 36 | +1.56% | +4.24% | 21 | +0.70% |
| `string_scan_bounded` | 0 | 7 | −0.38% | +0.62% | 7 | −2.56% |
| `string_scan_bounded` | 1 | 36 | +4.86% | +0.19% | 21 | −2.20% |
| `struct_score` | 0 | 21 | −3.18% | −0.24% | 7 | −0.95% |
| `struct_score` | 1 | 7 | +0.08% | −1.82% | 7 | −4.63% |
| `dfs_rec` | 0 | 7 | −3.09% | −0.35% | 7 | −3.71% |
| `dfs_rec` | 1 | 36 | +9.97% | +8.89% | 36 | −1.86% |

All A/B output hashes match for every pair. The normalized Linear instruction dump is also identical between 047 and the candidate in all eight cells (same instruction counts): `string_scan` 69/79, bounded scan 79/89, `struct_score` 47/51, `dfs_rec` 107/103 for ACCEL 0/1 respectively. Profile counters for all four benchmarks are also unchanged.

The conspicuous case is `dfs_rec` ACCEL 1 wall time: +8.89% paired median even after 36 pairs, while paired CPU time is −1.86% with IQR −9.89% to +18.62%. A same-binary A/A control of 36 pairs on that configuration showed much larger measurement spread (paired wall IQR −14.61% to +14.67%, individual ratios from −87.5% to +318.9%). Other benchmark A/B distributions also have large scheduling outliers. The identical instruction stream and execution counters, plus the A/A spread, make a code-induced slowdown unproven; however, the wall-time series alone cannot certify a literal 0% loss. I report that limitation instead of claiming mathematical proof of no slowdown.

- Wall A/B samples: `phase2_t1_verified_perf_ab_samples.csv`
- CPU + wall A/B samples: `phase2_t1_verified_perf_ab_cpu_samples.csv`
- Same-binary A/A control (`dfs_rec`, ACCEL 1): `phase2_t1_verified_aa_dfs_accel1_samples.csv`
- IR comparison: `phase2_t1_ir_dump_comparison.json`

## Deliverables and remaining verification

- Added test: `1017_linear_non_strict_fp_calls.c/.expect` only.
- Existing `.expect` files: 0 changed, 0 removed.
- Build / matrices: verified on Linux/GCC, clean, 1009/1009 throughout.
- MSVC/Windows compilation was not available in this environment and remains unverified.
- The no-fallback guarantee is established for this 1009-case numbered corpus. It is not a proof of complete ISO C coverage for arbitrary untested translation units.
