# P0.16 — Array-ident Decl init shared (`int dst[] = src`)

## Root
Linear `IR_K_DECL_ARRAY` allocated and zeroed the blob but never copied from
the source Ident when `DeclInitCount == -1` (tree had the path). Under
uninit checks → false "uninitialized" on `dst` (1042 soft+STRICT).

## Fix
- `IrCopyArrayIdentInit` in `ir_mem` (cold) — single memcpy + InitMap mark
- `ir_stmt` and `ir_lin_exec` call only that helper (GUARD comments)
- `LocalInit` true when `DeclInitCount == -1`

## Verify
1042 tree/soft/STRICT → `10 20 30 12`; suite 960/2.
