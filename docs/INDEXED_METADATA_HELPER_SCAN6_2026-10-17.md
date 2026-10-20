# Indexed metadata helper scan 6 — 2026-10-17

## Fix: keep extent metadata independent of pointer-element stride

`IdxMetaPush()` populated local-array `IdxMetaN` only in the non-pointer-element branch. If `IrCompLocalElemIsPtrOrFrame()` or `LocalIsFptr[]` classified the local as pointer-element storage, the code set `IdxMetaEs` to `sizeof(void *)` and skipped reading `LocalArraySize[]`. This coupled two independent facts (element stride and known extent) and could leave the indexed-access metadata without the extent even when the active frame knew it.

The local path now resolves `IdxMetaEs` independently and always reads the active frame's extent metadata when a call frame exists. Pointer-element classification remains metadata-based; it does not infer pointer type from a byte size. The debug trace is retained and moved after both values have been established. No runtime-loop check or per-element work was added; the change is in the metadata construction path.

## Build and focused regressions

- GCC `make -j2`: passed; no warnings in build output.
- Focused set: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` remains an unresolved strict-coverage failure (`strict: tree fallback (body)`); it is not counted as pass.
- Full suite, MSVC/Windows, and 32-bit build: not run.

## Performance considerations

The change only runs while constructing indexed-expression metadata; it adds no operation to the per-element execution loop. The prior branch used a frame pointer and extent check only for non-pointer-element locals; this change also obtains extent for pointer-element locals. That is a compile/lowering-time metadata read, not a runtime hot-loop check. No before/after benchmark was run for this small metadata-construction change, so no numerical speedup or zero-cost claim is made. If the indexed metadata path is found to be a material compile-time hotspot, benchmark it separately before further refactoring.

## Remaining audit

1. Continue classifying direct `CompLocalFromType[]` reads by active compile-slot precedence; do not mechanically replace accesses where live-frame or slot semantics differ.
2. Classify `LocalElemSize[]` consumers as leaf stride, pointee stride, or storage size before changing them.
3. Continue `IdxMeta*` lifecycle audit across reset, snapshot/install, cache invalidation, and writers.
4. Preserve the Linear strict failure until the unsupported construct is implemented and verified.
