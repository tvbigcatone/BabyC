# P0.20 — Multidim aos Member (H10) — 964 STRICT CLOSED

## Root causes
1. **`IrSafeMemberAddr`**: only accepted `Index` of `Ident|Member` — not
   `Index` of `Index`. Const `grid[0][0].m` failed.
2. **Lower emit**: single-Index peel for type resolution; load path required
   `nm>=2` so `grid[i][j].value` never took the direct Tok=7 path.

## Fix
- `IrSafeMemberAddr`: collect Index chain, flatten const indices with
  LocalDims / TypeArray sizes, peel all TypeArray to leaf struct.
- Load + pure-store emit: Index chain + row-major flatten (`ni>=2`).
- Tok=1 Ident peel: while Index (not one level).

## Verify
- 964/965 STRICT + soft + tree → expect match
- Suite 960/2; cluster 1042/596/652/699 OK
- Global multidim 981/982 still honest STRICT fallback
