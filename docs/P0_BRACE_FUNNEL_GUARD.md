# P0.15 — Unified brace pack funnel + zero-cost guards

## Goal
Force tree + linear through one cold entry; delete dead parallel collect/sort.

## API (`ir_runtime.h` / `ir_mem.c`)
- `IrBraceInitHasDesignated` — **static inline**, zero cost
- `IrPackStructBraceInit` — single cold funnel (designated | sequential×narr)
- GUARD comments: do not re-expand collect/sort/write in `ir_stmt` / `ir_lin_exec`

## Call sites
| Path | Before | After |
|------|--------|-------|
| `ir_stmt.c` tree Decl | local MemberList+sort + dual call | `IrPackStructBraceInit` only |
| `ir_lin_exec.c` DECL_STRUCT | ~7KB expand/sort + pack | `IrPackStructBraceInit` only |

Hot paths untouched (`IrWriteMemberScalar` remains static inline).

## Verify
Suite 960/2; 699/652/596/543/357 STRICT OK.
