# BabyC cumulative delta manifest — loop-local register promotion

- Fixed baseline archive: `babyc(20261018-120233).zip`
- Baseline SHA-256: `1390f9fe427093dab518e6f87877ba9979557855b6f09f0f5714e5adc56a3b0d`
- Contents: cumulative source/documentation/test delta from the fixed baseline, including the earlier condition-branch lowering delta and the current loop-local register promotion implementation.
- Main report: `docs/reports/LINEAR_LOOP_LOCAL_REGISTER_PROMOTION_2026-10-18.md`
- New tests: `src/tests/1001_loop_reg_break.c` through `src/tests/1008_loop_reg_for.c` and their `.expect` files.
- Validation: targeted matrix 102/102; string-scan fast-path gate PASS; sparse-local compact VM gate PASS with `BABYC_ACCEL=0`; full suite did not complete within the session tool timeout.
- Build platform validated: Linux/GCC only. MSVC was not run in this session.
