# Fix: compact bytecode multidimensional structs, coverage, and diagnostics

Date: 2026-10-17

## Root causes and changes

### 1. Wrong values in 964/981 (P0)

`IrLinLoadIndex` with `Tok=7` is emitted as a direct field load: `A` contains the
computed address, `B` is the `IdxTab` metadata slot, and `Imm` is the field width.
The compact executor treated `Imm` as the metadata slot. For an `int` field,
`Imm == 4`, so all such loads consulted `IdxTab[4]` and read the same AST access.
This explains the silent repeated outputs; it was not an array stride or data-copy
problem in Linear VM.

The compact executor now handles `Tok=7` before generic index dispatch, reads
metadata from `B`, validates the multidimensional access, checks initialization,
and loads the value from the address already computed by the lowerer. Preflight
validates this instruction layout too.

### 2. OOB writes in 968

The multidimensional member-store lowering computed a direct address and emitted
`Tok=2`, which carries no source-AST metadata. The compact executor therefore
could not validate each dimension before touching the address. Compact mode now
emits `Tok=8` for this specific indexed-member store shape: `A` is the value,
`B` the computed address, `Dest` the metadata slot, and `Imm` the width. The VM
checks each dimension using `VmMemberIndexInBounds` before logging or storing.
The existing `Tok=2` contract remains in place for Linear VM, avoiding a new
Linear-path regression. OOB diagnostics use the failing subscript's source line
and column when available; the shared diagnostic renderer remains the final
presentation path.

### 3. Coverage gaps 965/982/820/825

- `965` and `982` stopped failing once member loads used the correct metadata
  slot; their compact `runs` counter is now nonzero and output matches expected.
- `820` and `825` were rejected because `IrLowerCallUserSafeProd` blanket-rejected
  callees with `MaxLocal > 0`. That gate is now relaxed only for compact-bytecode
  mode. The callee must still have an eligible compiled stream, and the VM's
  complete preflight rejects unsupported operations before execution. Linear VM
  retains the old admission gate to avoid changing its call path/performance.

### 4. Diagnostic collector discrepancy: 252 NO_IR

The current collector does receive both paths. `ProgramFail` routes through
`DiagnosticReportV`/`DiagnosticRenderLocation`; for messages containing
`uninitialized variable`, `DiagnosticMemberColumn` deliberately canonicalizes
an access such as `s.a[2]` to the leaf member identifier (`a`, column 22), even if
an engine supplied the closing bracket column 25. The mismatch was between the
actual canonical output (22) and stale `257_member_array_uninit.expect.noir`
(which still expected 25), not evidence that the token path bypassed the
collector. The NO_IR expectation now matches the canonical column 22.

This normalization is message-specific: assignment diagnostics use
`DiagnosticReportAssign`, which intentionally does not apply uninitialized-member
anchor resolution. OOB accesses instead supply the failing index node's source
coordinates to the same renderer.

## Validation

Build:

- `make -C src -j2` — PASS, GCC `-O2`, no warnings emitted.

Focused bytecode gates:

| Case | Result | Compact counters / notes |
|---|---|---|
| 964 multidimensional local struct fields | PASS | `attempts=1 runs=1`, `11 27 15 42` |
| 965 multidimensional local struct copy | PASS | `attempts=1 runs=1`, `27 9 27 9` |
| 981 multidimensional global struct fields | PASS | `attempts=1 runs=1`, `41 9 42 11` |
| 982 multidimensional global struct copy | PASS | `attempts=1 runs=1`, `11 7 11 7` |
| 820 scalar function with locals | PASS | `attempts=2 runs=2`, output `11` |
| 825 global array loop through callee | PASS | `attempts=2 runs=2`, expected empty output and rc 0 |
| 968 outer-dimension OOB | PASS | `attempts=1 runs=0`, rc 1, `array index 2 out of bounds (size 2)`; caret at the failing `i` (column 10) |
| 969 inner-dimension OOB | PASS | `BT_ONLY=extras` safe-mode gate passes; diagnostic preserved |

Also passed focused gates 839, 840, 842, 843, 931, 932, 960–964, 970, 980–982,
986, 987, and the `extras` gate. Output and compact counters were checked; the
OOB `runs=0` is expected because the diagnostic terminates execution before the
VM can increment its successful-run counter.

Diagnostic parity:

- `257_member_array_uninit`: Tree and `BABYC_NO_IR=1` now both emit column 22;
  both match their expectations.
- `100_array_type_mismatch`: Tree and NO_IR both emit `can't assign int[] from
  char[3]` at column 27.
- `952_tree_pointer_param_read_uninit` and `953_tree_pointer_alias_partial_init`:
  Tree and NO_IR both emit the expected uninitialized-variable diagnostics.

Full gate limitation:

- `python3 tools/test_bytecode_vm.py --babyc src/babyc` exceeded the execution
  time limit after 45 groups had printed PASS. No failure was observed before
  timeout, but a complete full-gate pass is **not claimed**.

Performance spot-check:

- `bench/cases/stress/fib_ptr.c`, `BABYC_EXEC=vm BABYC_NO_KERNEL=1
  BABYC_NO_SHAPE_FP=1`, three interleaved runs after warm-up.
- Snapshot 074837 median: 0.8424 s (`0.9375, 0.8424, 0.8357`).
- Current median: 0.8498 s (`0.8325, 0.8498, 1.0068`).
- Ratio: 1.009 (about +0.9% median; current run spread includes a clear outlier).
  This is within measurement noise for this short sample, not proof of zero cost.
  The call-admission expansion is gated to compact mode so Linear VM retains its
  previous admission behavior.

## Remaining limitations

- Full bytecode gate did not finish within the available execution window.
- No MSVC/Windows build was run in this turn.
- The historical evaluator comparison against its older baseline cannot be
  reconstructed from this snapshot alone.
