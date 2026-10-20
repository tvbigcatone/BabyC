# Indexed metadata helper scan 13 — 2026-10-17

## Change

In `src/ir_lin_emit.c`, the array-of-pointer member-lowering path in `LowerRec()` previously fell back directly to `CompLocalFromType[slot]` when frame metadata did not provide `ST`. It now resolves that type through `IrLinEmitLocalType(slot)`, which gives the active `CompileSlot->SlotFromType[slot]` precedence while compiling and otherwise uses `IrCompLocalFromTypeOrFrame(slot)`.

This is a compile/lowering-time metadata lookup; it adds no VM instruction or per-element runtime check. The existing unwrapping of `TypeArray`/`TypePointer` and member-layout logic is unchanged.

## Validation

- `make -j2` in `src/`: **PASS**, GCC `-O2`; no warnings emitted by this build.
- Focused semantic tests: **not run in this scan**.
- Full test suite: **not run**.
- MSVC/Windows and 32-bit builds: **not run**.
- Before/after performance benchmark: **not run**; no performance-neutrality or speedup claim is made.
- Previously reported Linear strict gap `454_complex_dispatch` remains unresolved and was not retested here.

## Risk and next step

The changed branch is used only when frame metadata has not already resolved `ST`. The helper's active-template precedence is intentional, but a focused differential test should cover nested array-of-pointer member access under function-template compilation before this is considered fully validated. Continue classifying remaining direct metadata reads by semantic role rather than replacing them mechanically.
