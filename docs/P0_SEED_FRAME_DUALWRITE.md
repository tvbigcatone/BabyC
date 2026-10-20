# P0.17 — Dual-write reduction: FnSlot → Frame seed

## Point 2 (descriptor)
- **`IrStorageDescSeedFrameFromFnSlot`**: single cold funnel for
  nested-call frame seed (LocalIsPtr/Fptr/FromType/Name).
- Call sites: `ir_lin_exec.c` (nested user Call), `ir_lower.c` (body entry).
- GUARD: no hand-written `SlotIsPtr` loops at those sites.
- Heuristic TypePointer-without-SlotIsPtr still via `ApplyFrame` (619).

## Point 1 (STRICT residuals)
| Case | Status |
|------|--------|
| 262 fopen path | env (cwd); not storage |
| 964 multidim `grid[i][j].m` | **honest** STRICT fallback (1D aos path only) |
| 596/652/1042/699 | still PASS |

Next for 964: extend pure-store/load aos path to Index×Index root (flatten).

## Verify
Suite 960/2; no regression on closed STRICT cluster.
