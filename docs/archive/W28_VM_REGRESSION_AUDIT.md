# W28 VM anti-regression coverage audit

## Scope

The compact-bytecode test runner is `tools/test_bytecode_vm.py`. Its cases are intentionally distinct from the general interpreter suite: `BABYC_EXEC=vm` alone selects the linear IR executor; compact bytecode requires `BABYC_BYTECODE_VM=1`; `BABYC_EXEC=vm_strict` is the no-tree-fallback acceptance gate.

## Existing coverage and gaps

| Area | Case(s) | Gate / status |
|---|---|---|
| Branch/relational lowering and wide immediates | 818 | compact bytecode + strict |
| Arithmetic wraparound | 819 | compact bytecode + strict |
| Scalar values | 820 | compact bytecode + strict |
| Indexed memory and copy fusion | 821 | known gap: compact execution not entered; preserve as explicit expected fallback until lowering is implemented |
| Global scalar | 822 | compact bytecode + strict |
| Division | 823, 764 | normal result plus divide-by-zero diagnostic parity |
| Operator matrix | 824 | compact bytecode + strict |
| Global arrays | 825, 826 | normal access plus bounds-error parity under safety mode |
| Terminal `printf` | 827 | compact bytecode + strict |
| Global array element types / unsigned values | 828, 829 | compact bytecode + strict |
| Unsigned-short typed array | fixture 830 | tree/linear/compact parity + strict |
| 2D typed arrays | fixture 831 | compact bytecode output parity passes; strict mode rejects the body (strict-only gap) |
| Pointer array | fixture 832 | output parity; compact fallback is expected and explicitly asserted |
| 3D and 4D arrays | fixtures 833, 834 | tree/linear/compact parity + strict |
| Pointer parameter with runtime offsets | 901 | tree/linear/compact parity + strict; covers `int *` and `const unsigned char *` |

## Added in this audit

- Expanded `vm_strict` assertions to the admitted scalar/global/operator cases, rather than relying only on compact-vs-linear output parity.
- Made the compact-path exclusions for 821, 831 and 832 explicit in the runner. An unsupported compact body must not be mislabeled as a successful compact run.
- Kept the existing runtime diagnostic tests for division-by-zero and out-of-bounds global arrays.

## Remaining recommended test families

1. Indexed-memory lowering: local arrays, pointer parameters, load/store widths, nested index expressions, pre/post increment, and read-after-write alias cases.
2. Typed memory: signed/unsigned 8/16/32/64-bit loads and stores, casts around loads, pointer arithmetic scaling, and negative indices under safety mode.
3. Control flow: nested branches, loops with break/continue, short-circuit `&&`/`||`, switch and early returns.
4. Calls: direct calls, recursion, pointer arguments, return values, and host intrinsics near the supported argument limit.
5. Globals: zero-initialized arrays, scalar writes, array bounds diagnostics, and global/local alias interactions.
6. Diagnostics: unsupported IR must fail closed in strict mode; runtime traps should retain canonical diagnostics and never silently run tree execution.

A family should be marked supported only when the strict gate passes without a tree-fallback diagnostic. Compact fallback cases are recorded separately from strict VM support.

## W28 follow-up: intervention batch

The focused runner now also covers:

| Area | New regression | Gate / status |
|---|---|---|
| Signed/unsigned 8- and 16-bit indexed stores and reloads | `src/tests/811_vm_typed_memory_matrix.c` | compact bytecode + strict |
| Nested loop control, `continue`, `break`, and compound branch condition | `src/tests/812_vm_control_flow.c` | compact bytecode + strict |
| Global-array read-after-write aliasing plus scalar-global commit | `src/tests/813_vm_global_alias.c` | compact bytecode + strict |
| Negative global-array index | `src/tests/814_vm_global_array_negative_oob.c` | canonical diagnostic parity; VM abort/fallback under safety mode |
| Strict-only multidimensional gap | fixture `831_vm_multidim_array.c` | explicit assertion that `vm_strict` rejects the body rather than silently accepting tree fallback |

The new tests are intentionally small and call-free where compact execution is required. This keeps a failure attributable to memory/control-flow semantics instead of conflating it with the separate internal-call admission gap.

## Remaining implementation gaps after this batch

1. `802_vm_indexed_memory.c`: the body contains internal calls and pointer-parameter indexed read/write. Compact preflight currently rejects before execution; `index_copy_fuse` remains zero. Do not count output parity as compact support.
2. `839_vm_ptr_param_offset.c`: `vm_strict` passes, but compact bytecode still falls back (`runs=0`). Need to inspect index metadata/parameter offsets before enabling it.
3. Local arrays: not admitted by compact preflight. Supporting them safely requires local-array bounds and initialization-map checks plus rollback for writes.
4. General internal calls/recursion and short-circuit operators: still need isolated fixtures and a defined fail-closed admission boundary. The compact path now admits only fixed-arity integer-scalar calls in a non-trapping tail, with a conservative callee-frame limit; nested compact execution is still disabled because the production bytecode cache is not reentrant.
5. Pointer scaling, nested index expressions with runtime indices, and signed/unsigned 32/64-bit pointer loads/stores need dedicated tests beyond the global-array and multidimensional fixtures.

No compact-VM lowering/execution behavior was changed in this batch; it expands the anti-regression surface and makes the 831 strict gap explicit. The next code change should target 821/901 after tracing why pointer-parameter index metadata is rejected, rather than weakening the preflight guard globally.

### W28-04 — call rejection and side-effect safety

- `815_vm_call_fallback_once.c` verifies that a body rejected by compact-VM
  preflight falls back before executing its side-effecting user call. The
  observable result must remain `1 1`; a duplicated call would produce `2 2`
  or a mismatched result/counter.
- Current trace on `802_vm_indexed_memory.c` shows rejection at a global-load
  instruction before compact execution (`attempts=1, runs=0`). This fixture
  combines pointer-parameter indexing with user calls, so it is not a clean
  isolated test of the index opcodes.
- Current trace on `839_vm_ptr_param_offset.c` shows rejection at a non-terminal
  call (`IrLinCall`) before compact execution (`attempts=1, runs=0`).
- Do not admit either fixture by loosening the preflight alone. The next engine
  change must define a supported user-call ABI (arguments, return values,
  recursion/stack limits, globals and memory transaction semantics), or split
  the fixtures so pointer-index lowering can be tested independently of calls.

Verification for this increment (Linux/GCC workspace):

- `python3 tools/test_bytecode_vm.py`: PASS, including strict coverage, expected
  compact fallbacks, division-by-zero parity, positive/negative OOB diagnostics,
  and the new `839` single-execution side-effect regression.
- `python3 tools/run_suite.py src`: `passed=834 failed=0 skipped=23`.
- No VM engine/lowering implementation was changed in this increment; `821` and
  `901` remain explicitly unsupported by compact bytecode. Windows/MSVC has not
  been verified in this environment.

### W28-05 — call fallback with arguments

- Added `816_vm_call_fallback_args.c`: a two-argument user function updates a
  global and returns it; expected output is `12 12`.
- The compact VM must reject the user call during preflight (`runs=0`), then
  produce the same result through the established executor. This complements
  `839`, which covers a zero-argument call and return value.
- This is a safety/ABI regression, not a claim of compact user-call support.
  Supporting internal calls remains gated on an explicit frame/argument/return
  contract and transaction behavior for nested side effects.


### W28-06 — restricted scalar user-call bridge

- Added a compact-VM bridge for fixed-arity user functions with integer scalar
  parameters and return type. Pointer, floating-point, aggregate, variadic,
  and string-packed arguments remain rejected.
- Admission is limited to a non-trapping tail: after the first call, the stream
  may contain validated scalar local/global reads and writes to local slots,
  further approved calls, immediates, line markers, and returns. Stores to
  globals, indexed memory, branches, and trap-capable arithmetic are not
  admitted after a call, preventing fallback from replaying call side effects.
- Callees with local slots are conservatively excluded from the compact-call
  bridge. The bytecode production cache/transaction state is currently
  single-depth; nested bytecode execution is rejected before it can overwrite
  the active caller stream. Such callees execute through the established
  linear executor instead.
- Added `817_vm_terminal_scalar_call.c`: a scalar-argument user call followed
  by a terminal `printf`, with expected output `12`. It passes compact output
  parity and `vm_strict`; `839` and `840` continue to assert fallback-before-
  execution for unsupported side-effecting call shapes.
- This is a deliberately narrow ABI step, not full support for arbitrary user
  calls, recursion, pointers, or `821`/`901`.

Verification for this increment (Linux/GCC workspace):

- `make -C src -j2`: PASS.
- `python3 -m py_compile tools/test_bytecode_vm.py`: PASS.
- `python3 tools/test_bytecode_vm.py`: PASS, including `841` and strict coverage.
- Windows/MSVC has not been verified in this environment.

### W28-07 — fail-closed gate for branchy callees

- Added `818_vm_nested_pure_scalar_call.c`, which places a non-leaf scalar
  function with conditional control flow before a terminal `printf`.
- Differential testing exposed that admitting this branchy callee to the current
  terminal-call bridge could report a successful compact run while producing no
  output. This is a correctness failure, not a performance gap.
- Tightened `IrLowerCallUserSafeProd`: callees containing `Jmp`, `BrZ`, `BrLT`,
  or another call are rejected. The enclosing stream is rejected during
  preflight, before any side effects, and the canonical executor produces `13`.
- `842` is therefore an explicit expected compact fallback (`attempts=1,
  runs=0`), and is intentionally excluded from `vm_strict` coverage. The nested
  cache/state isolation work is not considered complete; this patch does not
  claim nested compact execution support.

Verification for this increment (Linux/GCC workspace):

- `make -C src -j2`: PASS.
- `python3 -m py_compile tools/test_bytecode_vm.py`: PASS.
- `python3 tools/test_bytecode_vm.py`: PASS, including `842` expected fallback.
- `python3 tools/run_suite.py src`: `passed=837 failed=0 skipped=23`.
- Windows/MSVC remains unverified in this environment.

### W28-08 — side-effect guard for rejected branchy callees

- Added `819_vm_branchy_call_side_effect.c`: the called function mutates a global
  and returns different values depending on a branch. The enclosing caller then
  prints both the return value and the global, making duplicate execution or a
  partially executed compact call observable.
- The compact preflight must reject the call shape before executing the callee
  (`attempts=1, runs=0`); the canonical executor must produce exactly `9 1`.
- This test complements `842`, which exercises branchy-callee fallback without
  side effects. It does not broaden compact call support.

Verification for this increment (Linux/GCC workspace):

- `make -C src -j2`: PASS (no rebuild needed for the fixture-only addition).
- `python3 -m py_compile tools/test_bytecode_vm.py`: PASS.
- `python3 tools/test_bytecode_vm.py`: PASS, including the new side-effect guard.
- `python3 tools/run_suite.py src`: `passed=838 failed=0 skipped=23`.
- Windows/MSVC remains unverified in this environment.
