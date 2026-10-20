# P0.12 — Collect unify + fold zero-tax + bench

## Code
- `IrCollectSortedMembers` shared by designated + sequential (optional Names)
- Enum rvalue fold uses `E->CachedVal` from `IrBindIdentSlots` (no second TableGet)

## Suite
**960 pass / 2 fail** (097, bytecode) — unchanged residual set

## Bench vs attachment baseline (median, 11–21 runs)

| kernel | mode | delta |
|--------|------|-------|
| sieve | tree/vm | ~0% / +2.5% |
| structs | tree/vm | -1% / +3% |
| matrix | tree/vm | -10% / 0% |
| ptr | tree/vm | -12% / -2% |
| scalar | tree/vm | +8% / +10% (spread large; no_kernel **-4%**) |

No hot-path helper calls added. Scalar noise/layout; honest engine (no_kernel) not regressed.
