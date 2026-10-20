# W32 — initialization-aware direct member loads (2026-10-16)

## Goal

Restore direct lowering for nested local array-of-struct member reads such as `v[i].in.a` without bypassing runtime uninitialized-value diagnostics. Single-member `v[i].m` expressions remain on the established safe path, preserving its bounds behavior.

## Implementation

- Restored the direct address calculation for local array-of-struct member chains with at least two member steps (for example `v[i].in.a`), including variable indices. Single-member `v[i].m` remains on the established safe path.
- The direct load uses `IrLinLoadIndex` with a dedicated `Tok=7` form. `A` holds the computed effective address, `Imm` the scalar width, and `B` indexes the original member expression in `IdxTab`.
- `Step()` uses that AST metadata to retain the original source line/caret and performs the existing effective-address check against owned call-frame `LocalInitMap` byte maps before reading. This avoids a second memory load and avoids adding a separate IR instruction to the hot path.
- If the expression metadata table is full, lowering falls back to the existing safe path instead of emitting a direct load without diagnostic metadata.
- The earlier W31 `LightRunBody()` fix is retained: a light callee does not write `LocalVal` or `LocalInit` through to its caller's frame.

## Validation

- GCC `-O2` build: PASS, no warnings/errors in the final incremental build. Both safe-path comparison and direct-path builds completed successfully.
- Uninitialized scalar member `v[1].b` (test 248): Tree, Linear VM and Bytecode-requested VM all return failure with the same source caret and `uninitialized variable` diagnostic.
- Uninitialized struct-array member (test 548): Tree, Linear VM and Bytecode-requested VM all return failure with matching source caret and diagnostic; output before the failure remains `1 2`.
- New regression `950_nested_member_direct_uninit.c`, reading `v[i].in.a` from an uninitialized local array: Tree, Linear VM, Bytecode-requested VM and `vm_strict` all return failure with the same caret and diagnostic.
- Initialized nested member access (test 564) returns `40` in Tree, Linear VM and Bytecode-requested VM. `276_fastpath_nested_struct` remains `s 60` in Tree/VM/Bytecode-requested modes.
- Test 1035 confirms the caller's uninitialized scalar is not masked by a light callee in Tree, Linear VM and Bytecode-requested VM. Its pre-existing `vm_strict` discrepancy remains: strict mode returns `0` rather than reporting the uninitialized scalar; this is unrelated to the direct member-load change and is not claimed fixed.
- Performance spot-check (5-run medians; seconds; safe path → direct path):

  | Case | ATS_no | VM_no | ATS_acc | VM_acc |
  |---|---:|---:|---:|---:|
  | `micro.array_sum` | 0.246 → 0.231 | 0.236 → 0.246 | 0.003 → 0.003 | 0.003 → 0.003 |
  | `micro.struct_score` | 0.140 → 0.153 | 0.146 → 0.153 | 0.001 → 0.002 | 0.002 → 0.002 |
  | `classic.structs` | 0.093 → 0.098 | 0.055 → 0.056 | 0.096 → 0.097 | 0.057 → 0.056 |

  These short measurements show no consistent material regression, but also no decisive overall speedup. They are a spot-check, not a claim about the full benchmark suite.
- The initial monolithic `tools/run_suite.py src` attempt exceeded the 300-second execution budget. Follow-up W33 validation split the existing 950-test corpus into 10 batches and ran both IR and `BABYC_NO_IR=1` modes: all 1,900 executions passed. The monolithic invocation itself was not rerun to completion; the equivalent test corpus was validated in tranches.
- MSVC and sanitizer validation have not been run.

## Result

The direct member-read path is restored with runtime initialization-map checking and source-position parity on the focused regressions. The known strict-mode test 1035 discrepancy, complete-suite validation, MSVC, and sanitizers remain open.
