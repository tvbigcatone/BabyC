# Indexed metadata helper scan 5 — 2026-10-17

## Fix: prefer active compile-slot type metadata in pointer arithmetic

In the Linear IR pointer-arithmetic emitter, the left and right identifier paths classified pointer-ness using `CompLocalFromType[slot]` even while `LinCompileActive` and `CompileSlot` described the function currently being compiled. That legacy table can retain metadata for a previous slot owner. The code now resolves `CT` from `CompileSlot->SlotFromType[slot]` when that active slot has a type, and uses `CompLocalFromType[slot]` only as a fallback. This preserves the active-slot authority without changing the subsequent `SlotIsPtr`, array-dimension, or live-frame checks.

The change is intentionally narrow: no new execution path, allocation, runtime loop, or per-element check was added. It only changes type selection during IR emission/compilation.

## Build and focused regression checks

- GCC `make -j2`: passed; no warnings emitted in the build output.
- Focused tests: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` still exits with `strict: tree fallback (body)` and is an unresolved strict-coverage gap, not a pass.
- Full suite, MSVC/Windows, and 32-bit build were not run.

## Performance check

Compared the pre-change source from SCAN4 and this change with GCC `-O2`, using 100 process-level repetitions per test under `BABYC_PATH=linear`. Median wall times (milliseconds):

| Test | SCAN4 | SCAN5 | Change |
|---|---:|---:|---:|
| `961_pointee_stride_large_struct.c` | 1.211 | 1.253 | +3.5% |
| `443_array_fptr_dispatch.c` | 1.365 | 1.306 | -4.3% |

These runs are dominated by process startup and have overlapping p10–p90 ranges; the directions disagree. They do not establish a material performance regression or a speedup. Since the code change is compile/emission-time and the absolute difference is about 0.04–0.06 ms, the result is **inconclusive/noise-level**, not proof of zero cost. No execution-loop overhead was added.

## Next steps

1. Continue auditing direct `CompLocalFromType[]` consumers, only changing cases where compile-slot precedence is unambiguous.
2. Audit `LocalElemSize[]` uses by semantic role (array leaf stride vs pointer pointee stride vs storage size).
3. Finish `IdxMeta*` lifecycle review across initialization, cache invalidation, function-slot snapshot/restore, and metadata writers.
4. Preserve the strict-coverage failure in the report until the unsupported Linear VM construct is implemented and tested.
