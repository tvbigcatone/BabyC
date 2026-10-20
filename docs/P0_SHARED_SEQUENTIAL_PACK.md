# P0.11 — Shared sequential brace pack + zero-cost writes

## Cost model
| API | Placement | Cost |
|-----|-----------|------|
| `IrWriteScalarBase` / `IrWriteMemberScalar` | `static inline` in `ir_runtime.h` | **0** call (inlined) |
| `IrPackStructDesignatedInit` | `ir_mem.c` | cold Decl only |
| `IrPackStructSequentialInit` | `ir_mem.c` | cold Decl only; recursive nested |

Hot paths (BinOp, LoadLocal, loop kernels) **unchanged** — no new call edges.

## Sequential pack
- Tree + linear both call `IrPackStructSequentialInit`
- Recursive into nested struct/union (569 `C{B{A{x},y},z}`)
- Array members expand element-wise via inline writes

## Verify
569 / 699 / 1040 / 567 OK all modes. Suite **960/2**.
