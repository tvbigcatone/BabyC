# Indexed metadata helper scan 9 — 2026-10-17

## Change

`src/ir_lin_emit.c` now uses a single lowering-time type resolver for local-slot classification in the pointer-arithmetic gate. `IrLinEmitLocalType()` prefers `CompileSlot->SlotFromType[slot]` while compiling a function template, then delegates to `IrCompLocalFromTypeOrFrame()` for the legacy compile metadata/live-frame sources. `IrLinEmitTypeIsPointerOrArray()` performs the type lookup once per tested operand instead of repeating the lookup for each Base comparison.

The change is deliberately scoped to the pointer-arithmetic classification in `LowerRec()`, plus the two existing left/right `CT` selections in that same lowering path. It does not alter runtime VM instructions or add a per-element execution check. The active compile slot remains authoritative, preventing a stale `CompLocalFromType` entry from overriding the current function's type.

## Build and focused checks

- GCC `make -j2` (`-O2`): passed, no warnings emitted by this build.
- Focused tests: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` still fails with `strict: tree fallback (body)`; this is a known unresolved strict coverage gap and is not counted as passing.
- Full suite, MSVC/Windows, and 32-bit build: not run.

## Performance check

Compared the pre-SCAN9 SCAN8 source/binary with SCAN9 using 35 process launches per binary/test in randomized interleaved order. Environment: `BABYC_PATH=linear`, `BABYC_ACCEL=0`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`. Reported medians are end-to-end process wall time, including startup and harness overhead. Outputs and return codes matched in all paired comparisons.

| Test | SCAN8 median | SCAN9 median | Change |
|---|---:|---:|---:|
| `961_pointee_stride_large_struct` | 1.355 ms | 1.398 ms | +3.2% |
| `443_array_fptr_dispatch` | 1.560 ms | 1.463 ms | -6.2% |
| `901_vm_dynamic_struct_array_copy_padding` | 1.356 ms | 1.373 ms | +1.3% |

The process-level ranges were broad and overlapped substantially (for example, 1049 had ranges 1.066–5.058 ms before and 1.087–3.178 ms after). These short-run measurements are noisy and mixed; they do not establish a real regression or improvement. Performance impact is therefore inconclusive, not certified zero. The change affects lowering/classification rather than runtime per-element operations, which is the main structural reason to expect no new hot-loop cost.

## Why this restructuring helps

- Keeps the active function's type metadata authoritative during nested/function compilation.
- Removes repeated direct reads of the legacy type array from this classification decision.
- Encapsulates the pointer-or-array semantic test in one small inline helper, reducing future drift between left and right operands.
- Preserves runtime instruction shape and avoids adding checks inside indexed-array execution loops.

## Next steps

Continue the direct-read audit in `ir_lin_emit.c` by semantic class, especially indexed element stride vs pointee stride vs pointer-cell width. Do not mechanically replace accesses where CompileSlot precedence or a deliberate compile-only lookup differs. Keep `454_complex_dispatch` explicit as a strict coverage gap and use larger/stabler benchmarks for any future hot-path changes.
