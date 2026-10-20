# Indexed metadata helper scan 11 — 2026-10-17

## Change

Continued the direct `CompLocalFromType[]` audit in `src/ir_lin_emit.c` at the strict nested-member lowering path for expressions shaped like `adj[i]->next->m`.

- Replaced the first direct `CompLocalFromType[slot]` lookup with `IrLinEmitLocalType(slot)`.
- The shared resolver gives the active function template's `CompileSlot->SlotFromType[]` precedence while compiling and otherwise follows the shared legacy compile-metadata/live-frame resolution.
- Kept the existing type peeling (`array -> pointer -> pointee` / `pointer -> pointee`) and the later live-frame fallback intact. This is specifically a type-resolution unification, not a stride or pointer-cell-size substitution.
- No VM opcodes, runtime dispatch, or per-element checks were added.

The other direct reads were not mechanically replaced. In particular, a second nested-member path deliberately checks the live frame's `LocalElemIsPtr` / `LocalFromType` first, and aggregate-copy size/type resolution has different precedence requirements. Those need separate semantic analysis.

## Build and focused checks

- GCC `make -j2` (`-O2`): passed; no warnings emitted by the build.
- Focused set: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` remains a strict coverage failure (`strict: tree fallback (body)`), with exit code 1. It is not counted as a pass.
- A direct before/after comparison of the SCAN10 binary and SCAN11 binary found the same output bytes and return codes on the three timed cases.
- Full suite, MSVC/Windows, and 32-bit build: not run.

## Performance check

Compared the pre-SCAN11 SCAN10 binary against SCAN11 with 35 process launches per binary/test in randomized interleaved order. Environment: `BABYC_PATH=linear`, `BABYC_ACCEL=0`, `BABYC_NO_KERNEL=1`, `BABYC_NO_SHAPE_FP=1`. Measurements are end-to-end process wall time, including startup and harness overhead.

| Test | SCAN10 median | SCAN11 median | Change |
|---|---:|---:|---:|
| `961_pointee_stride_large_struct` | 1.270 ms | 1.198 ms | -5.7% |
| `443_array_fptr_dispatch` | 1.321 ms | 1.340 ms | +1.4% |
| `901_vm_dynamic_struct_array_copy_padding` | 1.302 ms | 1.369 ms | +5.1% |

The direction is mixed, and the ranges are broad/overlapping. This does not establish a performance gain or regression, and does not certify zero cost. The code change is lowering-time only and adds no runtime per-element work.

## Remaining audit points

1. Continue classifying `CompLocalFromType[]` accesses in pointer arithmetic, indirect stores, aggregate assignment, and function-pointer dispatch. Do not substitute `SlotFromType` blindly where the legacy slot intentionally stores a pointee type rather than the declared object type.
2. Continue classifying `LocalElemSize[]` by semantic role: array leaf stride, pointee stride, pointer-cell width, or aggregate byte count. These are not interchangeable.
3. Preserve the unresolved `454_complex_dispatch` strict Linear VM failure until its body is supported semantically; do not mask it with fallback.

## Scope and validation limits

This scan changed one source file and added this report. The focused tests are not a substitute for the complete regression suite. No MSVC/Windows or 32-bit result is claimed.
