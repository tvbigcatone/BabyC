# P0.9 — Shared designated/brace pack (anti double-maintenance)

## Principle
Tree (`ir_stmt`) and linear (`ir_lin_exec`) must not own parallel copies of
the same semantic. Cold Decl packing is shared; hot paths stay direct.

## New API (`ir_mem.c` / `ir_runtime.h`)

| Helper | Role |
|--------|------|
| `IrWriteMemberScalar` | one member write (bitfield / int / FP / char…) |
| `IrPackStructDesignatedInit` | mixed sequential + designated + nested union |

Callers:
- `ir_stmt.c` — tree Decl brace when any `DeclInitNames`
- `ir_lin_exec.c` — `IR_K_DECL_STRUCT` when designated

Pure sequential expand (arrays, nested braces without names) stays local
until a second shared sequential pack is justified.

## Perf
Decl-only / cold. No change to BinOp, LoadLocal, or loop kernels.

## Verify
699 / 1040 / 567 / 954 all modes OK. Suite **960 / 2**.
