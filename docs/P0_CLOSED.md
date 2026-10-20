# P0 CLOSED — Unified storage + shared helpers + exec config (2026-10-17)

## Delivered
1. **IrStorageDesc** dual-write (CompLocal / FnSlot / Frame / Param)
2. **Helpers H1–H11 / C1–C9** shared tree↔linear (zero-cost hot, cold GUARD)
3. **Dual-read accessors** `IrCompLocal*OrFrame`
4. **Exec config** PATH / ACCEL / STRICT (+ legacy aliases + tools migration)

## Policy kept
- Hot path: static inline only; no call overhead
- One runtime meaning → one helper
- Historical gate reference: 960/2 (097 diagnostic + bytecode subgroup baseline).
  This is not, by itself, evidence that every compact-bytecode regression is closed.

## Residual intentional
es→TypePointer store maps; host/printf/WideImm/Light deferred

## Compact-bytecode follow-up (2026-10-17)

The evaluator review found a separate compact-VM contract bug that the historical
960/2 gate did not cover: Tok=7 indexed-member loads used `Imm` (field width) as
an `IdxTab` index instead of `B` (metadata slot), causing 964/981 to repeat one
field value. Tok=8 now carries metadata for multidimensional member stores so
bounds checks run before writes. The related compact gate cases 964, 965, 981,
982, 820, 825 and the safe-mode OOB tests 968/969 were re-run after the fix.
See `BYTECODE_STRUCT_ARRAY_AND_DIAGNOSTIC_FIX_2026-10-17.md` for the exact
results; the full bytecode gate timed out before completing and is not claimed
as a full-suite pass.
