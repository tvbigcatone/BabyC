# Indexed metadata helper scan 7 — 2026-10-17

## Scope

This pass continued the audit of `CompLocalFromType`, `LocalElemSize`, and `IdxMeta*`, with an explicit performance/regression check. No production-source edit was made in this pass: the remaining direct type reads examined here have different source-precedence requirements, and replacing them mechanically with the existing dual-read helper would change semantics.

## Findings: preserve precedence, do not mass-rewrite

- `IrCompLocalFromTypeOrFrame()` in `ir_storage.h` intentionally resolves `CompLocalFromType` first, then frame `LocalFromType`, then `LocalTyp`. It is suitable only for sites that want exactly that order.
- Several `ir_lin_emit.c` sites intentionally prefer `CompileSlot->SlotFromType` over `CompLocalFromType`, especially during function-body compilation when live-frame state may refer to a caller. Those are not equivalent to `IrCompLocalFromTypeOrFrame()` and must not be converted blindly.
- Other pointer-member paths deliberately prefer the active frame when its `LocalElemIsPtr` flag is set, then fall back to compile metadata. Reversing that precedence could use stale compile metadata.
- `LocalElemSize` has multiple meanings across consumers: array leaf stride, runtime storage element size, and fallback stride for pointer/index operations. A direct read is not automatically replaceable with `PointeeSize`; the latter is for scalar `T* p` pointee stride and must not override array-of-pointer element stride.

## `IdxMeta*` lifecycle audit

- `IdxMetaPush()` initializes all metadata fields for each new slot (`Slot`, `Kind`, `Es`, `N`) and clears `IdxGlobBase[i]` before resolving the expression, avoiding reuse of a previous global base.
- `FnCodeSnapshot`/`LinTabsRestore` copy `IdxTab` and semantic metadata (`Slot`, `Kind`, `Es`, `N`) for the active table window. They intentionally do not snapshot `IdxGlobBase`: that is runtime cache state.
- `IdxRunPush`/`IdxRunPop` own runtime cache save/restore (`GlobBase`, `Addr`, `Save`, `Es`, `Eb`, `Dirty`). The fast path skips work only when no indexed metadata and no dirty cache are live. The nested frame restore path calls `IdxRunClearCache()` during table installation, which prevents a child cache from being mistaken for the restored parent's cache.
- No lifecycle defect was confirmed in this pass. Keep these as separate ownership domains: expression metadata is snapshotted with `FnCodeSlot`; runtime cache state is handled by `IdxRunSave`.

## Build and focused regression tests

A clean rebuild was performed from `src`:

- `make clean && make -j2`: passed (GCC `-O2`, no build warnings emitted).
- Focused tests: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 matched expected output.
- Token-only: 7/7 matched expected output.
- Linear strict: 6/7 matched expected output. `454_complex_dispatch` still fails with `strict: tree fallback (body)` and remains an unresolved strict-coverage gap.
- Full suite, MSVC/Windows, and 32-bit build were not run.

## Performance

No production code changed in this pass, so this pass introduces no new runtime instructions or branches and has no source-level performance delta to benchmark. This is not a claim that the overall metadata system has zero cost. Earlier short timing differences were noisy and inconclusive; a useful before/after benchmark requires a production change and a repeatable benchmark window. Avoid introducing helper calls in hot loops: the existing accessors are `static inline`, and helper adoption should stay on compile/lowering paths unless runtime semantics explicitly require it.

## Next actions

1. Continue reviewing individual `CompLocalFromType` consumers against `CompileSlot`, compile-local, and frame precedence.
2. Trace each `LocalElemSize` consumer's semantic meaning before changing it.
3. Keep `IdxGlobBase` cache ownership separate from `IdxMeta*` snapshot ownership.
4. Close the `454_complex_dispatch` Linear strict gap by implementing the unsupported construct, not by masking it with fallback.
