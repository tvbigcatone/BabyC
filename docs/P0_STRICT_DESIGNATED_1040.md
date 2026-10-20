# P0.6 — STRICT designated struct init (1040/567)

## Symptom
`struct S s = { .b = 2, .a = 1, .c = 3 };` under STRICT printed `2 1 3`
(positional) instead of `1 2 3`. Soft used tree residual.

## Fix
`IR_K_DECL_STRUCT` in `ir_lin_exec.c`: when any `DeclInitNames[i]` is set,
write each value by member name into the sorted member list (same contract
as tree `ir_stmt.c`). Sequential braces unchanged.

## Verify
- 1040 / 567: tree = soft = STRICT = expect
- Suite: 959/3 pre-existing (699 nested union designated still open on tree)
