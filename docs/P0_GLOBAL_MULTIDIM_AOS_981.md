# P0.21 — Global multidim aos (981/982 STRICT CLOSED)

## Root
Local multidim (964) used LoadLocal + CompLocal dims. Global
`struct Cell grid[2][3];` has Ident LocalSlot<0 — pure-store/load paths
refused idE, so STRICT body fell back.

## Fix (`ir_lin_emit.c`)
- Accept global Ident as aos root (`LocalSlot<0 && Ident`)
- Dims from `IrGlobalIdentSoft` → TypeArray chain
- Base via `IrLinLoadGlobal` Tok=1 + `GlobPush` (array payload addr)
- Same row-major flatten as local (`ni>=2`)

## Verify
| Test | STRICT |
|------|--------|
| 981 fields | 41 9 42 11 |
| 982 copy | 11 7 11 7 |
| 964/965 local | still PASS |
| Suite | 960/2 |
