# BabyC — Global array-of-struct copy in compact VM

Date: 2026-10-14

## Scope

This delta adds lowering for by-value assignment where both the source and destination are elements of a one-dimensional global array of structs/unions, e.g. `table[i] = table[j]`. The destination address is now emitted as `global_base + index * sizeof(aggregate)`, matching the already-present global-source address path. Index expressions are lowered once each; the aggregate copy remains the existing byte-copy operation (`IrLinStoreIndex`, `Tok=3`) with the compact VM's transactional memory-write path.

The new regression fixture `src/tests/924_vm_global_struct_array_copy.c` checks runtime-variable source and destination indices, chained copies, self-assignment, and subsequent field mutation. The fixture is part of the bytecode differential harness and strict coverage set.

## Verification performed

- `make -C src clean && make -C src -j4`: PASS, GCC `-Wall -O2`, no warnings observed.
- `python3 tools/test_bytecode_vm.py`: PASS, 45/45 groups.
- Test 980: Tree/linear/compact outputs agree: `40 50 60 40 57 60`.
- Test 980 compact execution confirmed: `attempts=1 runs=1`.
- Regression tests 549, 956, and 970: PASS; compact runs were 3/3, 3/3, and 1/1 respectively.
- `BABYC_EXEC=vm_strict BABYC_BYTECODE_VM=1` test 980: output matches expected.

## Boundaries / not claimed

This change is limited to one-dimensional global array-of-struct assignment. It does not claim support for global multidimensional aggregate copies, aggregate arrays through arbitrary pointers, or side-effecting index expressions. The existing compact VM eligibility checks remain in place; this patch does not broadly relax preflight. MSVC and ASan/UBSan were not run in this tranche.
