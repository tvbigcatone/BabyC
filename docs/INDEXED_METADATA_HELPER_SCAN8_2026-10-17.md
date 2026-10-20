# Indexed metadata helper scan 8 — 2026-10-17

## Change made

`src/ir_lower.c`, `IdxMetaPush()`: when an indexed local is not classified as a pointer-element array and the compile-time path has no live frame, metadata `IdxMetaEs` previously fell directly to the hard-coded default `4`. It now resolves the leaf stride through `IrCompLocalLeafEsOrFrame()` and uses `4` only if the helper has no positive value.

This closes a source-precedence hole: the accessor already expresses the intended compile-local-first, frame-second lookup. Previously, the compile-time leaf stride could exist while `IrCallDepth == 0`, yet `IdxMetaPush()` ignored it. Pointer-element classification remains prior to this lookup, so pointer array element width is not conflated with the pointee stride.

## Why this is useful

- **Correctness:** preserves a known compile-time leaf size instead of silently substituting four bytes when no live frame exists.
- **Consistency:** uses the shared metadata policy rather than duplicating the frame-only lookup.
- **Performance:** the accessor is `static inline`; this affects metadata construction/lowering, not the per-element runtime loop. No new runtime loop branch or helper call is introduced by the intended compiled path.
- **Maintainability:** fewer independent fallback rules make metadata provenance easier to reason about and review.

## Build and focused regressions

- `make -j2`: passed with GCC `-O2`, no warnings emitted by the build.
- Focused set: `961_pointee_stride_large_struct`, `442_array_of_fptr`, `443_array_fptr_dispatch`, `454_complex_dispatch`, `624_nested_fptr`, `520_bitfield_struct_array_loop`, `901_vm_dynamic_struct_array_copy_padding`.
- Tree: 7/7 expected outputs matched.
- Token-only: 7/7 expected outputs matched.
- Linear strict: 6/7 expected outputs matched. `454_complex_dispatch` still fails with `strict: tree fallback (body)`; this remains an unresolved strict coverage gap.
- Full suite, MSVC/Windows, and 32-bit builds were not run.

## Performance comparison

Compared SCAN7 (pre-change source) with SCAN8 (this change) using 50 process launches per binary/test in randomized interleaved order. Reported values are median wall-clock milliseconds for the entire process, so startup and harness overhead are included. Outputs and return codes matched for each comparison.

| Test | SCAN7 median | SCAN8 median | Change |
|---|---:|---:|---:|
| `961_pointee_stride_large_struct` | 1.217 ms | 1.298 ms | +6.7% |
| `443_array_fptr_dispatch` | 1.374 ms | 1.319 ms | -4.0% |
| `901_vm_dynamic_struct_array_copy_padding` | 1.311 ms | 1.272 ms | -3.0% |

The absolute times are very small and results are mixed; the first case is slower while the other two are faster. This does **not** establish a real performance regression or gain. Since the change is in metadata setup/lowering and the measurement is dominated by process overhead, treat the performance result as inconclusive, not as proof of zero cost. A larger stable benchmark is needed before making performance claims.

## Restructuring benefits (overall)

The metadata-helper restructuring aims to keep the interpretation of array dimensions, leaf element stride, pointer-pointee stride, pointer-element classification, and source precedence consistent across compiler and frame state. The concrete benefits are fewer mismatched fallback rules, less risk of stale metadata winning after nested compilation, more reliable handling of non-4-byte elements and pointer arrays, and easier addition of strict Linear VM coverage without adding ad-hoc runtime fast paths. It is not claimed to speed up execution automatically; the immediate value is correctness and auditability while keeping helpers inline and off per-element hot loops where possible.

## Next steps

Continue classifying remaining `LocalElemSize` and `CompLocalFromType` reads by semantic role before replacing any. Retain the unresolved `454_complex_dispatch` strict gap as an explicit item; do not conceal it with fallback. Use a stable benchmark suite for any future change that affects hot runtime paths.
