# P0.25 — Dual-read accessors

## Problem
Emit/lower repeated:
```
ST = CompLocalFromType[slot];
if (ST == NULL && IrCallDepth > 0) {
  ST = Fr->LocalFromType[slot];
  if (ST == NULL) ST = Fr->LocalTyp[slot];
}
```
Same for NDims / LeafEs. Order is intentional: CompLocal = function under
compile; Frame = live caller (fallback only).

## Fix (`ir_storage.h` static inline, zero-cost)
- `IrCompLocalFromTypeOrFrame(slot)`
- `IrCompLocalLeafEsOrFrame(slot)`
- `IrCompLocalNDimsOrFrame(slot)`

Wired: ~10 FromType dual-reads in `ir_lin_emit.c`, Twalk dims, `ir_lin_ptr.c`
IndexRowIsPointer.

## Policy
Hot paths still direct-field when single source is known. New dual-read sites
must use accessors (`GUARD` comments).

Suite 960/2.
