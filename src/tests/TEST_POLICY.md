# Anti-regression test policy (babyc)

At **every** change step:
1. Add new tests before considering the step done.
2. Re-run the **full** suite (`tests/*.c` + `.expect`).

## Test design criteria

### #1 Isolated interceptors
One mechanism, one failure mode. Minimal source. Name documents the edge.
Examples: `382_sprintf_cache_isolated`, `364_global_struct_arr_member`,
`376_array_elem_postfix_inc`, `373_memo_global_side_effect`.

### #2 Cross-cutting / overlapping edges
Combine features that interact: global struct member + sprintf + loop + pointer;
memo + globals; fptr table + IR call; etc.
Examples: `383_sprintf_struct_loop_cross`, `031_hanoi`, `362_fptr_table_dispatch`.

## Rules of thumb
- Prefer pairs `.c` / `.expect` with deterministic stdout.
- Avoid depending on cwd/file presence unless the test copies its own fixture.
- Include both IR-heavy and token-fallback paths when relevant.
- Never delete a passing baseline test to “make green”.
