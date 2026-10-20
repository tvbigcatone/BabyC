# Linear C coverage audit — final ZIP reconstruction validation

The exact distributive contents of `babyc_cumulative_delta_2026-10-19_linear_coverage_map_phase1.zip` were reconstructed from the fixed `loop_promotion` baseline by applying `DELETE_FILES.txt` before overlaying the ZIP.

- Baseline files: 3,080.
- DELETE paths applied: 2,043; missing paths: 0.
- Reconstructed distributive files: 3,262, byte-identical to the candidate working tree after excluding generated binaries, object files, and Python caches.
- `MANIFEST_SHA256.txt`: verified.
- `SHA256SUMS`: verified.
- Fresh Linux/GCC build: return code 0; zero `warning:` or `error:` diagnostics.

## Matrices on the reconstructed tree

| Mode | First clean result | Log |
|---|---|---|
| Tree ACCEL 0 | One transient failure, `535_main_continue_after_extract.c`; 1007/1008 | `coverage_reconstructed_tree_a0_attempt1_2026-10-19.log` |
| Tree ACCEL 0 | Full rerun passed 1008/1008 | `coverage_reconstructed_tree_a0_retry_2026-10-19.log` |
| Tree ACCEL 1 | Passed 1008/1008 | `coverage_reconstructed_tree_a1_2026-10-19.log` |
| Linear STRICT ACCEL 0 | Passed 1008/1008 | `coverage_reconstructed_linear_a0_strict_2026-10-19.log` |
| Linear STRICT ACCEL 1 | Passed 1008/1008 | `coverage_reconstructed_linear_a1_strict_2026-10-19.log` |

The transient 535 failure comes from a timed output compared to a fixed `.expect` value (`0 ms`); the complete Tree ACCEL 0 rerun passes. No `.expect` was changed or excluded. The other three matrices pass on their first run. The map-phase source changes are documentation-only; there are no lowering/runtime edits in this phase.

## Dynamic instrumentation

A separate non-STRICT Linear run with the existing `BABYC_COV=1` and `BABYC_VM_PROFILE=1` counters covered all 1008 numbered tests: the program output compares to `.expect` with zero differences after stripping diagnostics only. Totals: 9,669 Linear body runs and 36 actual Tree body fallback executions across 34 test processes. This is a dynamic count on the numbered regression suite, not static AST edge coverage or proof of ISO C completeness.

MSVC/Windows and an independent real-world C application corpus were not tested.
