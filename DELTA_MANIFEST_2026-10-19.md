# BabyC cumulative delta — 2026-10-19 (Linear coverage audit, phase 1)

## Baseline and current turn

- Fixed baseline: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`.
- Incoming cumulative delta: `babyc_cumulative_delta_2026-10-19_linear_126_modulo_closure.zip`, SHA-256 `4e3d6b70706c114daec41ae0c8a44c98700a27e5c75e5023aa7dfd62f5d2e8b5`.
- This phase adds the code-derived map `docs/LINEAR_COVERAGE_MAP.md` and runtime counter report/CSV. No lowering/runtime source files or existing `.expect` files were modified.
- Clean Linux/GCC build: 0 `warning:` diagnostics and 0 `error:` diagnostics.
- Matrix: Tree 1008/1008 at ACCEL=0 and 1; Linear STRICT 1008/1008 at ACCEL=0 and 1.
- Separate non-STRICT profiled pass over all 1008 numbered cases: output compares to `.expect` with 0 differences. Existing counters record 9,669 Linear body runs and 36 actual Tree-body fallback executions distributed across 34 test processes. Rejection attempts are reported separately and are not equated with actual fallback executions.
- Artifacts: `docs/LINEAR_COVERAGE_MAP.md`, `docs/reports/LINEAR_COVERAGE_RUNTIME_COUNTERS_2026-10-19.md`, corresponding CSV, four matrix logs and GCC build log. The map marks unsupported/unverified C features explicitly and orders the follow-up roadmap; no feature gaps are closed in this phase.
- MSVC/Windows and an external, representative C application corpus were not tested. The dynamic fallback counts describe only the numbered regression corpus, not complete ISO C conformance.

## Earlier cumulative history

# BabyC cumulative delta — 2026-10-19 (Linear STRICT `047_grep` closure)

## Baseline and incoming cumulative delta

- Exclusive baseline: `babyc_baseline_2026-10-18_loop_promotion.zip`, SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`.
- Incoming cumulative delta: `babyc_cumulative_delta_2026-10-19_linear_651_varargs_closure.zip`, SHA-256 `f77e4ef698b4c83df642ecf01a3e3a35f821a0ad0913e9796ba65acbb95a92e6`.
- Reconstruct by deleting paths listed in `babyc/DELETE_FILES.txt`, then overlaying the cumulative ZIP while preserving `babyc/...` paths.

## Cumulative changes and latest closure

This delta retains earlier cumulative changes through `058_macro_bug`, `062_initializers`, and `651_sqlite_exec_null_varargs`, and closes `047_grep` in Linear STRICT. The fix generalizes nested indexing through `char **` parameters; defers resolution of unresolved global names until the load instruction actually executes so an unreachable conditional arm cannot abort lowering; and accepts only the emitted wide pointer-plus-offset form in the pointer-parameter safety proof. No special-case for `047`/`grep`, no fallback, and no rollback of earlier fixes.

New regression: `src/tests/1015_param_ptrptr_index.c/.expect`, actively reading `text[0][1]` through `char **`. All earlier `.expect` files remain unchanged.

## Verification

Clean-state Linux/GCC build completed with 0 warnings/errors. Matrix: Tree 1007/1007 for ACCEL=0 and 1; Linear STRICT 1006/1007 for ACCEL=0 and 1. The only Linear residual is `126_csmith_modulo_zero`, whose compile rejection is on a separate Csmith binary-expression path and is not shared with the `047` cause. `047` and `1015` pass on Tree and Linear STRICT at both ACCEL settings.

Performance reports and rows: `docs/reports/LINEAR_GREP_047_CLOSURE_2026-10-19.md`; all eight benchmark/configuration pairs have at least seven alternating AB/BA pairs, with repeat series extended to 36 for initially adverse configs. Output hashes match and linear IR dumps are unchanged. Same-binary controls show large wall-time variation; no stable runtime-instruction change is observed, but exact zero overhead is not claimed as proven. MSVC/Windows and compact bytecode remain unverified/out of scope.

## Phase 6 addendum — canonical type source of truth (2026-10-19)

The cumulative archive now includes `PHASE6_CANONICAL_TYPE_REPORT.md` and `PHASE6_DELTA_MANIFEST_2026-10-19.md`. Phase 6 adds `CanonicalOpBase/CanonicalOpFlags`, removes the old binary unsigned/wide inference and bitfield fallback helpers, migrates Tree/Linear operator decisions to canonical metadata, and adds a runner-enforced `CANONICAL_TYPE_GATE`. Tests 1030/1031 are generated against GCC `-std=c11 -O0`. Latest clean Linux/GCC build: 0 warnings/errors; six matrices 1023/1023, canonical invalid count 0, Linear non-STRICT `tree_body=0`. A/B versus Phase 5 has seven alternating pairs per benchmark/ACCEL with identical stdout, but timing medians are mixed/noisy; Windows/MSVC and longer controlled performance measurement remain open. See the Phase 6 report for exact data.
