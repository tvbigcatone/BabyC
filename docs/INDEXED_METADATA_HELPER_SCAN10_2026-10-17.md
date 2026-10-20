# Indexed metadata helper scan 10 — 2026-10-17

## Change

Continued the direct type-metadata audit in `src/ir_lin_emit.c` without changing runtime instruction execution:

- In the `&local` lowering path, pointer-cell classification now uses the already-resolved `CT` type rather than re-reading `CompLocalFromType[slot]` independently. `CT` is selected with the active `CompileSlot->SlotFromType[]` precedence, so a stale legacy entry can no longer independently force the local into pointer-cell classification after the active slot type was resolved.
- Added `IrLinEmitPointerHasSizedPointee()` and used it in the indexed pointer-load gate. It routes type resolution through `IrLinEmitLocalType()` rather than maintaining duplicated direct checks against both `CompLocalFromType[]` and `CompileSlot->SlotFromType[]`.
- The array-of-pointer classification's multidimensional pointer check now uses the same resolver. The check still distinguishes a pointer local from an array type and retains the existing pointer-cell width / pointee-stride behavior.

This is a lowering-time metadata consistency change; it adds no runtime VM instruction and no per-element execution check. Other direct reads remain intentionally untouched pending semantic classification (array leaf stride, pointer-pointee stride, pointer-cell width, aggregate-copy size, and frame-state access are not interchangeable).

## Build and focused checks

- GCC `make -j2` (`-O2`): passed; no warnings emitted by the build.
- Focused tests: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` still fails with `strict: tree fallback (body)`; this remains an unresolved strict-coverage gap and is not counted as passing.
- Full suite, MSVC/Windows, and 32-bit build: not run.

## Performance check

Compared the pre-SCAN10 SCAN9 binary against the SCAN10 build using 35 process launches per binary/test in randomized interleaved order. Environment: `BABYC_PATH=linear`, `BABYC_ACCEL=0`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`. Timings are end-to-end process wall time, including startup and harness overhead. Output bytes and return codes matched for each test.

| Test | SCAN9 median | SCAN10 median | Change |
|---|---:|---:|---:|
| `961_pointee_stride_large_struct` | 1.217 ms | 1.246 ms | +2.3% |
| `443_array_fptr_dispatch` | 1.315 ms | 1.336 ms | +1.6% |
| `901_vm_dynamic_struct_array_copy_padding` | 1.249 ms | 1.268 ms | +1.5% |

The observed median differences are small relative to the broad, overlapping process-level ranges (including outliers up to 4.082 ms in SCAN10). These measurements do not prove a performance regression or a gain, and do not certify zero cost. Since the change is limited to lowering/classification, it introduces no new runtime per-element work; further claims require a more stable benchmark harness.

## Remaining audit points

Continue classifying the remaining direct reads by their semantic role before replacing them. In particular:

1. `LocalElemSize[]` is sometimes array leaf stride, sometimes a frame-stamped representation size; verify each site against `IrCompLocalLeafEsOrFrame()` and `IrCompLocalPointeeSizeOrFrame()` rather than applying a global substitution.
2. `CompLocalFromType[]` direct reads in aggregate copies, stores, parameter/local pointer indexing, and function-pointer dispatch may have different active-template vs live-frame precedence requirements.
3. Preserve `454_complex_dispatch` as a strict Linear VM coverage failure until the unsupported body is genuinely implemented; do not mask it with fallback.

## Scope and validation limits

This scan changed one source file and this report. The focused checks are not a substitute for the complete regression suite. No MSVC/Windows or 32-bit result is claimed.
