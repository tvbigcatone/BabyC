# W32 — Strict signed 2D globals and compact terminal-call preflight

**Date:** 2026-10-11  
**Baseline:** user-supplied `babyc(3).zip`; this delta is cumulative relative to that baseline.

## Changes

1. Fixed strict lowering of global multidimensional arrays whose row stride collides with `sizeof(void *)`. The old heuristic treated `ElemSize == sizeof(void *)` as proof of an array-of-pointers row. For `int signed_grid[2][2]` on this target, the row size is 8 bytes, so ordinary 2D indexing was misrouted to the pointer-array path and lowering rejected the body. `IrLowerIndexRowIsPointer` now consults the declared array element type when metadata is available; unknown metadata is not inferred from stride alone.
2. Updated the `831_vm_multidim_array` regression fixture to cover signed-int 2D global loads, stores, compound assignment, and copying a value between differently typed multidimensional arrays. The strict check requires no tree fallback. The ordinary compact-mode test also checks output parity.
3. Promoted `843_vm_narrow_incdec_edges.c` from explicit compact non-admission to actual compact execution (`attempts=1, runs=1`). Root cause: the compact terminal-call preflight inspected local initialization state before execution and therefore rejected later `printf` calls that loaded locals initialized by earlier straight-line instructions. Preflight now seeds definite writes from a straight-line prefix. If branches/jumps/returns occur before the call, it does not infer initialization from lexical order.
4. Kept the post-call tail fail-closed for branches, indexed memory, and potentially trapping arithmetic, so a failure after observable output cannot trigger fallback and duplicate side effects.
5. Reduced an avoidable hot-path readback in Tree/IR prefix increment: converted values are re-read only for narrow and unsigned integer types where the conversion can change the result, not for every signed full-width increment.

## Verification

- Clean GCC build: PASS, zero warnings.
- Standard suite: 841 passed, 0 failed, 23 skipped.
- `tools/test_bytecode_vm.py`: PASS.
- Strict fixture `831_vm_multidim_array.c`: PASS with no tree fallback; expected output `65535 32768 -17 14`.
- Compact fixture `831`: `attempts=1, runs=1`.
- Compact fixture `904`: `attempts=1, runs=1`; exact output matches the expected file.
- Existing pointer-array fixture `832`, plus 3D/4D fixtures `833`/`834`, still pass in strict mode. Pointer-array fixture 832 remains an explicitly expected compact fallback (`runs=0`) in the generic compact gate.

## Benchmark method and findings

The raw measurements are in `W32_BENCH_RAW.json`. Baseline and current builds were both rebuilt cleanly with GCC; six identical benchmark source files were run with one warm-up and nine alternating measurements per version/mode. Medians use wall-clock time and stdout was compared across baseline/current and execution modes.

This is **not a performance PASS**:

- All six benchmark outputs match across baseline/current and Tree/linear/compact-requested modes.
- In the generic workload set, compact execution counters were `runs=0` for all six programs: these timings measure rejection/fallback overhead, not compact bytecode throughput, and must not be presented as compact-VM speed.
- Tree/linear timings remain noisy and mixed. This run includes apparent slowdowns on `isort` and `matmul_small` alongside improvements on other cases. The sample is insufficient to attribute those changes confidently, but they are retained as open performance risks rather than hidden.
- The two compact-positive fixtures (`904` and `831`) each report `attempts=1, runs=1`; their short wall-clock timings are dominated by startup and are not a meaningful throughput benchmark.

## Remaining priorities

1. Investigate the mixed Tree/linear timing deltas with longer, workload-specific measurements and separate parse/compile/execute costs.
2. Add representative long-running programs whose counters prove actual compact execution; do not benchmark a fallback as if it were VM throughput.
3. Continue strict/compact coverage expansion only with exact output/differential tests and fail-closed gates.
