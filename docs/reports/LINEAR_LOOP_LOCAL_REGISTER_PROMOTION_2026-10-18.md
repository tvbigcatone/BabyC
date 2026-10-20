# Linear VM: loop-local register promotion — 2026-10-18

## Implementation

- Added `IrLinAddRegLocal`: updates the pinned register, preserves the old `AddLocal` result convention (`R[Dest]`), and applies scalar integer narrowing for char/short/int/enum types. Pointer and wider integer values retain their native word representation.
- The generic `while`, `for`, and `do/while` lowerers select scalar integer/pointer locals only when the condition/body/increment contain no calls, the local is not address-taken, and the write set uses only `StoreLocal`/`AddLocal` forms. Compound assignments lowering to `AddStoreLocal` are excluded by design.
- Each promoted slot receives one preheader `LoadLocal`; the register is pinned for the whole loop region. The backedge targets the loop head after that preheader. Region rewriting turns local loads into register copies, stores into register moves, and `AddLocal` into `AddRegLocal`.
- Normal condition-false and `break` exits converge on a single sync block. `return` and `goto` emit sync stores before leaving. Labels inside a loop disable promotion conservatively so a `goto` cannot enter the body while bypassing the preheader. `continue` follows the existing loop-specific continuation edge.
- Nested loops reuse an already-promoted register for the same slot, avoiding a stale second preheader load. Each loop exit still syncs its selected slots.
- If the pre-existing tree-loop fast attempt succeeds, its branch skips both the promotion preheader and the sync block: the fast path has already committed the real local slots. This avoids overwriting its result with an uninitialized/stale register.
- Removed the two shape-specific CSE blocks for `while (*p)` and `while (p < end)`. The separate native tree/kernel path remains.
- `BABYC_NO_LOOP_LOCAL_PROMOTION=1` disables this optimization for paired A/B measurement; default behavior enables it.

## Correctness validation

- GCC build: passed; build log contains no warnings.
- Targeted matrix: 17 tests × Tree/Linear STRICT/compact STRICT × `BABYC_ACCEL=0/1` = **102 runs, 102 passed, 0 failed**. Tests include the existing loop/CSE/condition regressions and new tests `1090`–`1097` for break, return, nested promotion, pointer increment, goto-out, address-taken exclusion, do/continue, and for/continue/break.
- `tools/test_string_scan_fastpath.py --babyc src/babyc`: passed; native output differential and independent fast-path hit/control checks pass.
- `BABYC_ACCEL=0 python3 tools/test_sparse_local_snapshot.py`: passed; compact execution confirmed.
- A `GEN_DUMP=1` sample showed the preheader `LoadLocal` before the loop head, `AddRegLocal` in the body, the backedge targeting the head, and a final `StoreLocal` marked as sync. This verified the intended control-flow shape, not just output equivalence.
- The complete `tools/run_suite.py` run did not finish within the 300-second tool limit. The compact VM aggregate test also exceeded the 240-second session limit after reporting passing cases through `981`; therefore no full-suite pass claim is made. The focused matrix above completed normally.

## A/B and CSE decision

With `BABYC_PATH=linear`, `BABYC_EXEC=vm`, `BABYC_ACCEL=0`, `BABYC_NO_SHAPE_FP=1`, `BABYC_NO_KERNEL=1`, and the generic loop lowering forced, seven alternating pairs compared the old shape-CSE path with generic lowering plus promotion. Outputs matched for all four benchmarks:

| Benchmark | Shape CSE median | Generic + promotion median | Generic delta |
|---|---:|---:|---:|
| `string_scan.c` | 554.61 ms | 544.86 ms | -1.76% |
| `string_scan_bounded.c` | 648.17 ms | 640.71 ms | -1.15% |
| `struct_score.c` | 139.72 ms | 139.47 ms | -0.18% |
| `dfs_rec.c` | 43.20 ms | 43.48 ms | +0.64% |

The difference is within observed timing noise; generic lowering ties or wins across this set. The two shape-specific CSE implementations were removed rather than retained as redundant alternate paths. The A/B is descriptive, not a CI performance gate: additional promotion on/off runs were noisy (outliers in both modes), especially for `dfs_rec`.

## Remaining limits

- This first version intentionally declines promotion for loops containing calls, address-taken locals, locals written via `AddStoreLocal`/compound assignment, or labels inside the loop body (to prevent jumps into the region bypassing the preheader).
- MSVC was not built in this session; only Linux/GCC was validated.
- Full corpus validation remains outstanding due the session execution timeout.
