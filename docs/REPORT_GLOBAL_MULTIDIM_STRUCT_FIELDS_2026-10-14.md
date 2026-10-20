# BabyC — global multidimensional struct arrays and performance snapshot

Date: 2026-10-14
Baseline: `babyc(20261014-133924).zip`

## Fix delivered

`src/ir_mem.c` now resolves the global root identifier when the left side of a member access is a chained index (`grid[i][j].field`), and strips every remaining `TypeArray` wrapper before looking up the leaf struct/union member. Previously it attempted to look up the global symbol using an intermediate `IrExIndex` node, so the type lookup failed; even if that lookup succeeded, only removing one array layer left the wrong type for 2-D arrays.

The same path now performs safe-mode bounds checks for each known dimension in the global index chain. The checks run before address arithmetic, avoiding the observed out-of-bounds write / allocator corruption. Pointer indexing remains unchecked when no static extent is available.

## Verification performed

- Clean GCC build (`make clean && make -j4`): PASS; no `warning:` or `error:` lines in build output.
- `925_vm_global_multidim_struct_fields.c`: PASS in Tree, linear VM, compact VM, and `vm_strict`; expected output `41 9 42 11`. Compact profile confirmed `attempts=1 runs=1`.
- `905_vm_multidim_struct_array_copy.c`: PASS across the differential harness modes; expected output `27 9 27 9`.
- Global out-of-bounds repros with `BABYC_SAFE=1`: outer dimension `grid[2][0]` trapped with `array index 2 out of bounds (size 2)`; inner dimension `grid[0][3]` trapped with `array index 3 out of bounds (size 3)`. Both returned non-zero rather than corrupting memory.
- `BT_ONLY=925_vm_global_multidim_struct_fields.c python3 tools/test_bytecode_vm.py`: PASS; compact `attempts=1 runs=1`.
- `BT_ONLY=905_vm_multidim_struct_array_copy.c python3 tools/test_bytecode_vm.py`: PASS.
- The full `tools/test_bytecode_vm.py` invocation progressed through the listed core cases including the new 981 case, but exceeded the execution time limit in its later phase. Therefore a complete end-to-end full-suite pass is **not claimed**.

## Still open

`926_vm_global_multidim_struct_copy.c` proves that `grid[x][y] = grid[i][j]` produces the expected bytes in Tree and linear VM (`11 7 11 7`). The compact lowerer currently rejects that aggregate-copy body (`attempts=0 runs=0`) and transparently falls back to Tree. This delta does **not** claim compact-VM execution for global multidimensional struct copies. That remains the next concrete blocker.

## Benchmark data

Command: `python3 tools/bench_vm_compact.py --rounds 9` after the final clean build. One warm-up is excluded; each reported sample is end-to-end wall time, including startup, parse/lowering and execution. Compact execution was checked by the benchmark harness (`runs >= 1`).

| Mode | Median (ms) | Min (ms) | Max (ms) | Samples |
|---|---:|---:|---:|---:|
| Tree | 22.026 | 21.046 | 29.539 | 9 |
| Linear VM (`BABYC_EXEC=vm`) | 14.419 | 13.213 | 18.970 | 9 |
| Compact VM (`BABYC_BYTECODE_VM=1`) | 14.696 | 13.646 | 15.878 | 9 |

Interpretation: on this loop benchmark, both VM modes are materially faster end-to-end than Tree (linear about 34.5% lower median time; compact about 33.3% lower). Compact is about 1.9% slower than linear by median in this run, a small difference relative to sample variability; this benchmark does not establish a general compact-VM speed advantage. It is a single loop benchmark and includes parse/lowering costs, so these are directional measurements, not execution-only throughput.

## Reproduction

```sh
make -C src clean && make -C src -j4
BT_ONLY=925_vm_global_multidim_struct_fields.c python3 tools/test_bytecode_vm.py
BT_ONLY=905_vm_multidim_struct_array_copy.c python3 tools/test_bytecode_vm.py
python3 tools/bench_vm_compact.py --rounds 9
```
