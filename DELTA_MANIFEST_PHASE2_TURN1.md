# Cumulative delta — Linear non-STRICT no Tree fallback (Phase 2, turn 1)

- Fixed reference baseline: `babyc_baseline_2026-10-18_loop_promotion(1).zip`
- Baseline SHA-256: `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`
- Apply `babyc/DELETE_FILES.txt` to the baseline distribution, then overlay this ZIP.
- Modified source/distribution files before metadata: 131
- New source/distribution files before metadata: 2240
- Explicit deletions from baseline: 2042
- Main changes: separate Linear capability from STRICT fallback policy; safe const-trip unroll gate for both unrollers; guard array-fill initializer against duplicate execution in Linear; permanent `--require-no-tree-fallback` runner gate; reliable `[babyc-cov]` reporting; normalization of the nondeterministic clock field in test 535; test 1017; updated coverage map and validation reports.
- Validation: clean Linux/GCC build (0 warnings/errors); Tree 1009/1009 ACCEL 0/1; Linear STRICT 1009/1009 ACCEL 0/1; Linear non-STRICT gate 1009/1009 with `tree_body=0` ACCEL 0/1.
- Existing `.expect` files unchanged; only new expectation is `src/tests/1017_linear_non_strict_fp_calls.expect`.
- A/B output hashes and normalized Linear instruction dumps match; timing limitations are documented in `docs/reports/LINEAR_NO_TREE_FALLBACK_PHASE2_TURN1_2026-10-19.md`.
