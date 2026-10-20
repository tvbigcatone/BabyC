# Root fix: nested Linear light calls and fib_ptr profile

Date: 2026-10-17

## Summary

The earlier explanation that local-slot call admission was being expanded only in compact bytecode was imprecise. `IrLowerCallUserSafeProd()` is a compact-bytecode terminal-call bridge; it is not Linear VM's call-eligibility gate. Linear's recursive-call slowdown was a separate problem in `IrExecLinLocals`' call dispatch.

The actual Linear hot-path issue was structural: scalar-only recursive calls from a function already running under a normal caller frame were forbidden from using the existing per-depth light-frame interpreter by `IrCallDepth == 0`. `fib_ptr` therefore used a full `IrCallFrame` for every recursive call, snapshot/restored compile tables, seeded local metadata by scanning all possible slots, and re-entered `IrExecLinLocals` for every invocation.

## Root causes and fixes

### 1. Avoid full compile-table snapshots on same-function recursion

Added `LinFramePushFor(FnCodeSlot *Target)`. When `Target == FnActive`, the active Call/Glob/Idx/Kernel/Wide compile tables are already the correct tables for same-function recursion. The frame now saves/restores only the runtime index-cache context; it does not copy all compile tables through `FnCodeSnapshot()` / `LinTabsRestore()`. Calls that switch to another function still take the full snapshot/restore path.

### 2. Stop scanning every possible local metadata slot on every call

Added `FnCodeSlot.MetaLocalN`, computed once after a body is lowered as the highest slot with `SlotIsPtr`, `SlotIsFptr`, or `SlotFromType` metadata. `IrStorageDescSeedFrameFromFnSlot()` now scans only `[0, MetaLocalN)`, with bounds validation. This does not assume `MaxLocal` captures every address-taken declaration, so it preserves metadata for locals not represented by ordinary LoadLocal/StoreLocal instructions.

### 3. Enable the Linear light-frame path for proven-safe nested calls

The old `IrCallDepth == 0` condition disabled the existing light-frame optimization when a scalar recursive function was called from `main` or another framed function. Added a compile-time `LightFrameSafe` proof to `FnCodeSlot` and replaced the blanket depth restriction with that proof.

The proof is deliberately conservative. It rejects local-address formation, pointer slots, indexed/kernel operations, indirect or non-self calls, unsupported unary conversions, FP/wide binary operations not implemented identically by `LightRunBody`, and non-integer globals. Existing runtime checks still reject pointer/aggregate parameters and non-self call graphs. This allows scalar recursion such as `fib_ptr` to use isolated `LightLocal` / `LightParam` windows at each depth without aliasing the caller's `IrCallFrame`.

The compact-bytecode call bridge remains separately guarded by its own preflight. No compact-specific condition was removed to force Linear into that path.

## Performance investigation

Benchmark command/environment:

`BABYC_EXEC=vm BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1 BABYC_NO_MEMO=1`

`bench/cases/stress/fib_ptr.c`, expected output `832040 2692537`. Both the pre-change reconstruction and final tree were clean-built with GCC `-O2`. Runs were interleaved, with two warm-ups and ten measured runs per build; every output and exit status was checked.

| Measurement | Pre-change | Fixed | Change |
|---|---:|---:|---:|
| `fib_ptr` median (10 runs) | 0.8714 s | 0.2614 s | -70.0% |
| `call_loop` median (6 runs) | 0.9074 s | 0.7854 s | -13.4% |
| Default Tree `fib_ptr` median (5 runs) | 0.5388 s | 0.5391 s | +0.1% (noise) |

`fib_ptr` timing samples, pre-change: 1.0012, 0.8672, 0.8507, 1.0264, 0.8740, 0.8813, 0.8568, 0.8814, 0.8687, 0.8436 s.

`fib_ptr` timing samples, fixed: 0.2637, 0.2735, 0.2909, 0.2764, 0.2527, 0.2605, 0.2623, 0.2568, 0.2599, 0.2494 s.

The previous source profile showed 2,692,538 calls each to `IrStorageDescSeedFrameFromFnSlot`, `IrExecLinLocals`, `FnCodeSnapshot`, and `LinTabsRestore`; `IrStorageDescSeedFrameFromFnSlot` accounted for about 31% of sampled time and `IrExecLinLocals` about 30%. After the fix, an instrumented profile attributed about 92% to the actual `LightRunBody` execution and 8% to `LightCallSelf`; the repeated full-table snapshots and full metadata scans no longer appeared as hot functions. This is evidence of an execution-path bottleneck, not just an inlining issue.

The user's historical 0.45 s versus 0.95 s pair cannot be recreated exactly from the archives available in this workspace. The controlled before/after comparison above is built from the pre-change source reconstructed from the current cumulative overlay and the final source, using the same environment and checking identical output.

The bench runner also passed all four selected stress cases (`call_loop`, `fib_global`, `fib_multiarg`, `fib_ptr`) across its checked modes. Its measured VM values for `fib_ptr` were 0.265 s (VM_no) and 0.259 s (VM_acc); default Tree was about 0.54 s.

## Validation

- `make clean && make -j2`: PASS, GCC `-O2`, zero warnings.
- `fib_ptr`: expected output in normal Linear and `BABYC_EXEC=vm_strict`; no strict Tree fallback.
- Focused recursion/local-call suite: 12/12 PASS in Tree and 12/12 PASS in Linear strict, including `015`, `210`, `211`, `216`, `310`, `311`, `313`, `472`, `911`, `1035`, `1045`, `1047`.
- Diagnostic subset (`097`, `252`, `1035`, `1038`, `1039`): 5/5 PASS in Tree, token-only, and Linear strict.
- `tools/test_fp_modes.py`: 32/32 PASS.
- `tools/test_sparse_local_snapshot.py`: PASS (Linear/compact/strict).
- `tools/test_string_scan_fastpath.py`: PASS.
- `tools/test_empty_expect_exit_status.py`: PASS.
- Compact bytecode targeted gates: `964`, `965`, `981`, `982`, `820`, `825`, `842`, `843`, `931`, `932` PASS; `BT_ONLY=extras` PASS, including OOB diagnostics `968` and `969`.
- The complete `tools/test_bytecode_vm.py` gate exceeded the execution time limit after 46 groups had printed PASS; no failure was observed in the printed groups, but a full-suite pass is **not claimed**.
- MSVC/Windows was not built in this turn.

## Diagnostic collector note

The previous `257_member_array_uninit` discrepancy was not a collector bypass: both Tree and NO_IR passed through `DiagnosticReportV` / `DiagnosticRenderLocation`. The collector intentionally canonicalizes this uninitialized-member access to the leaf member column 22; the stale NO_IR expectation at column 25 was aligned to that canonical anchor. After this source change, the focused diagnostic subset was rerun in Tree, token-only, and Linear strict and all passed.
