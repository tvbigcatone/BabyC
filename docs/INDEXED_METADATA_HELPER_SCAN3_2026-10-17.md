# Indexed metadata helper scan 3 — 2026-10-17

## Finding fixed

The nested Linear compiler frame saved/restored `CompLocalNDims`, `CompLocalDims`, `CompLocalLeafEs`, `CompLocalPointeeSize`, and `CompLocalFromType`, but omitted `CompLocalElemIsPtr`. `IrLowerStmt` clears that table at the start of compilation, so compiling a nested function could overwrite the parent's array-element-is-pointer metadata and the parent metadata was not restored after `CompileFramePop`. This is a real lifecycle bypass: the shared helper `IrCompLocalElemIsPtrOrFrame()` could be correctly wired at all read sites and still receive stale/zeroed compile metadata after nested compilation.

Fix: added `CompElemIsPtr` to `CompileFrame` and save/restored the complete `CompLocalElemIsPtr` table in `CompileFramePush()` / `CompileFramePop()`. The snapshot uses the existing table type and exact `sizeof` pattern used by the neighboring metadata arrays.

## Build and targeted checks

- Clean GCC build: passed.
- Focused test set: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` is rejected with `strict: tree fallback (body)`; this is recorded as an unresolved strict-coverage gap, not counted as a pass.

No full suite was completed in this scan. MSVC/Windows and performance impact have not been verified.

## Remaining scan priorities

1. Continue auditing direct reads of `CompLocalFromType[]` in `ir_lin_emit.c`, preserving compile-slot type precedence where it is semantically authoritative.
2. Audit `LocalElemSize[]` consumers by meaning (array leaf stride versus pointer pointee stride versus storage size); do not substitute one metadata category for another.
3. Verify `IdxMeta*` initialization, slot reuse, invalidation and nested compiler save/restore as a lifecycle, not just individual helper call sites.
4. Audit `CompLocalElemIsPtr` write sites to ensure all declarations and parameters stamp it consistently; the fix above guarantees nested compiler state restoration only.
