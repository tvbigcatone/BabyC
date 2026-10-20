# BabyC performance tranche 2 — pointer-indexed Light path

Date: 2026-10-17  
Fixed source baseline: `babyc(20261017-175902).zip`  
Build: GCC, `-Wall -O2`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`, `BABYC_NO_MEMO=1` for the measured VM runs.

## Summary

This tranche extends the existing Linear VM Light executor for a narrow, statically proven class of pointer-parameter indexed accesses. It is not a new engine and does not allow arbitrary local arrays or member/index AST fallbacks into the Light path.

- `FnCodeLightFrameSafe()` accepts `IrLinLoadIndex` / `IrLinStoreIndex` only when `IdxMetaKind` proves the base is a formal pointer parameter and the parameter slot is in range.
- `LightRunBody()` routes those opcodes through the existing `LinTryParamIndexedAccess()` helper, so pointer bounds/initialization tracking and typed loads/stores stay on the shared implementation.
- The Light indexed helper recovers the formal parameter's declared pointee type from the active `FnCodeSlot`. This is required to preserve signedness: without it, `unsigned char *` reads returned `-128` instead of the correct unsigned-byte sum.
- `IrLinBrLT` is implemented directly in the Light dispatch, with the same signed comparison as the existing linear executor.
- The indexed Light path is limited to same-body recursive functions (`CallN > 0`, combined with the existing `only_self` proof). A non-recursive indexed helper would pay a table snapshot/install/restore on every cross-function call; an experimental version regressed `heapsort`, so that case stays on the full-frame path.
- The Makefile now emits and includes GCC dependency files (`-MMD -MP`), removes them on `clean`, and pins `.DEFAULT_GOAL := all` so included `.d` files cannot accidentally change the default target.

## Alternating A/B performance measurements

Five runs per build, alternated baseline/new to reduce drift; medians shown. Times are wall-clock seconds on this sandbox, not universal guarantees.

| Benchmark | Baseline median | New median | Change | Output |
|---|---:|---:|---:|---|
| `i06_tak` | 0.8254 | 0.8108 | -1.8% | `2400` / `2400` |
| `a02_qsort` | 0.4606 | 0.2651 | -42.4% | `1 6553811808` / same |
| `a03_heapsort` | 0.5603 | 0.5809 | +3.7% | `1 3930663163` / same |

`qsort` is a clear win. `tak` is effectively unchanged. `heapsort` has no demonstrated improvement in this tranche; the +3.7% median difference is within the observed run-to-run spread, but it is not reported as a win. The non-recursive indexed helper remains a performance target for a later tranche.

## Correctness / regression checks

- GCC `make -B -j2`: success, zero compiler warnings/errors.
- Tree suite, 10 tranches: **973/973**.
- Linear suite, 10 tranches: **972/973**. The single mismatch is `270_ptr_inc_for.c`; it reproduces identically on the unmodified fixed baseline (extra `123` line), so it is not introduced by this patch.
- `787_vm_indexed_pointer_types.c`: passes after preserving declared pointee type; output `384 254 255 0`.
- `952_tree_pointer_param_read_uninit.c` and `953_tree_pointer_alias_partial_init.c`: expected uninitialized-read diagnostics match in both tree and linear modes.
- `test_string_scan_fastpath.py`: PASS.
- `test_sparse_local_snapshot.py`: PASS.
- qsort/heapsort output and exit status match between baseline and patched builds.

## Remaining item

This tranche materially improves recursive `qsort`, but does not solve the remaining `heapsort` overhead. The next investigation should profile the repeated cross-function `sift()` call and reduce its full-frame/table-switch cost without allowing an indexed non-recursive Light path that regresses performance. Do not describe the overall W30 recovery as complete based on this tranche alone.
