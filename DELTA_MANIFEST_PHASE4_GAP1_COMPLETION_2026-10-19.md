# Cumulative BabyC delta — Phase 4 gap 1 semantic closure + warning cleanup

Cumulative overlay relative to `babyc_baseline_2026-10-18_loop_promotion.zip` (SHA-256 `8f0917a6f716c3310af0f4c93d60a213572b34628d2a93b6bd7c16ad5ede1c81`). Includes Phase 3 integer promotions, Phase 4 fixes, the BabyPOSIX session-3 overlay, and MSVC-warning source cleanup. `DELETE_FILES.txt` is included.

- Tree fused relational evaluation applies the shared usual arithmetic conversion decision.
- Linear integer type inference resolves declared bitfield width/signedness before lowering metadata is stamped.
- Removed the uninitialized debug trace (`ld`/`rd`) and unused `one`/`uns` locals in `src/ir_lin_emit.c`.
- Linux/GCC clean build: exit 0, 0 emitted warnings, 0 errors.
- Numbered matrix: all six configurations pass 1019/1019; non-STRICT Linear no-fallback gate reports `tree_body=0` at ACCEL 0 and 1.
- Tests 1024–1027 match `gcc -std=c11 -O0` byte-for-byte.
- BabyPOSIX session-3 smoke results: Tree 29/29, NO_IR 29/29, Linear 28/29, Linear STRICT 23/29. Residuals are documented in `docs/reports/phase4_gap1/WARNINGS_AND_POSIX_INTEGRATION_2026-10-19.md`.

The semantic Phase 4 gap-1 matrix is closed. The broader performance gate remains open because previous seven-pair A/B medians include regressions (largest +14.88%). MSVC was not available, so the source warning fixes are applied but a native MSVC build is not claimed. POSIX Linear residuals, full once-per-lowering canonical type metadata for every expression form, and Windows validation remain open.
