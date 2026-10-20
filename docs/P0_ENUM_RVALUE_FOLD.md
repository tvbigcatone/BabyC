# P0.10 — Root fix: enum rvalue fold (not brace-only retreat)

## What was wrong before
Folding *all* global integers to Const broke mutable globals. Retreating to
brace-init only papered over 699 without defining the rule.

## Root distinction (already in the runtime)
| Binding | `VariableDefine(..., MakeWritable)` | `IsLValue` |
|---------|--------------------------------------|------------|
| Enum constant `INTERO` | `FALSE` | **0** (rvalue) |
| Mutable global `int g` | `TRUE` | **1** (cell) |
| `const int c` | usually writable cell + `IsConst` | 1 (`&c` ok) |

## Fix
1. **`type.c`**: enum members get `IsConst = TRUE` at define (still `IsLValue=0`).
2. **`ir_compile.c` Ident**: fold global Ident → `IrExConst` iff
   - `!GV->IsLValue`, integer-like type, no postfix
   - does **not** fold mutable or const *cells* (preserves `&x`)
3. Brace-init only peels `IrExConst` into `DeclInitVals` (consequence of 2).

## Verify
699 tree/soft/STRICT OK. Suite **960/2** (no regression from root fold).
