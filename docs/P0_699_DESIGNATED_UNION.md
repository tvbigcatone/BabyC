# P0.8 — 699 designated union nested CLOSED

## Fix
1. **Tree** (`ir_stmt.c`): mixed sequential + designated; nested name lookup inside union/struct members.
2. **Linear** (`ir_lin_exec.c`): same; skip nested expand when designators present.
3. **Compile** (`ir_compile.c`): brace-init folds `IrExConst` and global integer/enum Idents into `DeclInitVals` only (not general Ident path).

## Verify
tree / soft / STRICT: `0 42` / `1 2.71` / `2 Z`
Suite: **960 pass / 2 fail** (097 diagnostic, bytecode 820/825) — was 959/3.
