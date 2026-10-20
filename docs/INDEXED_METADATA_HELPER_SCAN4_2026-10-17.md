# Indexed metadata helper scan 4 — 2026-10-17

## Finding fixed: pointer classification must not be inferred from byte size

`IdxMetaPush()` classified a local array as pointer-element when `IrCompLocalLeafEsOrFrame(slot)` equaled `sizeof(void *)` **or the literal value `8`**. Element stride and element kind are distinct metadata. In particular, on a 32-bit target, a non-pointer 4-byte element could be confused with a pointer merely because it matched `sizeof(void *)`; the literal `8` also conflated 8-byte objects with pointer arrays on 32-bit targets.

Changed the classification to use the explicit pointer-element metadata helper `IrCompLocalElemIsPtrOrFrame(slot)` and the live frame's `LocalIsFptr` flag. The element-size fallback now remains responsible for stride only. This also consults both compile-time and active-frame metadata because the declaration's runtime `DECL_ARRAY` operation may stamp the frame after compile-time emission.

## Build and targeted regression checks

- GCC build (`make -j2`): passed, no new warnings shown in the build output.
- Focused test set: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` remains an explicit strict-coverage failure (`strict: tree fallback (body)`); it is not counted as passing.
- Full suite not run; MSVC/Windows and 32-bit builds not run.

## Performance assessment

The change is in metadata classification during Linear IR emission/compilation, not in the per-element execution loop. It adds an explicit metadata lookup in place of a size heuristic. No dedicated before/after benchmark was run, so a measured zero-cost claim would be unjustified. No runtime performance improvement is claimed. The focused outputs show no newly observed regression in Tree or token-only modes; this does not replace a full regression suite.

## Next audit steps

1. Continue classifying direct `CompLocalFromType[]` reads in `ir_lin_emit.c`, preserving authoritative compile-slot precedence.
2. Audit `LocalElemSize[]` consumers by semantic role: array leaf stride, pointer pointee stride, or storage size.
3. Complete the `IdxMeta*` lifecycle audit: initialization, reuse, invalidation, and function-slot snapshot/restore.
4. Inspect all writers of `CompLocalElemIsPtr` and related frame fields for consistent declaration/parameter stamping.
