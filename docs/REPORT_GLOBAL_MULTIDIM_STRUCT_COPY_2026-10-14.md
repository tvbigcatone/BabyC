# BabyC — global multidimensional struct copy closure report

Date: 2026-10-14  
Baseline: `babyc(20261014-133924).zip`

## Fix

`src/ir_mem.c` resolves the root global identifier through a chained `IrExIndex` and strips all array layers to obtain the leaf struct/union type. The same path checks the index against each statically declared dimension in safe mode before calculating the address.

`src/ir_lower.c` now has a dedicated `LowerGlobalStructArrayAddress` routine. It gathers the complete subscript chain, reverses it into source order, obtains each extent from the declared global type, computes the row-major flattened index, and scales exactly once by the leaf aggregate size. Both source and destination of aggregate assignment use this routine. The lowerer also explicitly clears `IrLinBin.Imm` when emitting the scale multiplication; without this, a stale scale value (e.g. 8) was interpreted as invalid bin-op flags by the compact VM verifier and forced fallback.

## Regression results

| Test | Tree/linear differential | Compact VM | Output |
|---|---|---|---|
| `924_vm_global_struct_array_copy.c` | PASS | PASS, attempts=1 runs=1 | `40 50 60 40 57 60` |
| `925_vm_global_multidim_struct_fields.c` | PASS | PASS, attempts=1 runs=1 | `41 9 42 11` |
| `926_vm_global_multidim_struct_copy.c` | PASS | PASS, attempts=1 runs=1 | `11 7 11 7` |
| `903_vm_multidim_struct_array.c` | PASS | PASS, attempts=1 runs=1 | `11 27 15 42` |
| `905_vm_multidim_struct_array_copy.c` | PASS | PASS, attempts=1 runs=1 | `27 9 27 9` |

Safe-mode OOB cases: `909_vm_multidim_struct_oob_outer.c` reports index 2 out of bounds for size 2; `911_vm_multidim_struct_oob_inner.c` reports index 3 out of bounds for size 3. Both terminate nonzero before the invalid access.

Clean build: `make clean && make -j4`, PASS; no `warning:` or `error:` lines.

The complete harness emitted `bytecode-vm: 47/47 groups passed`, but the command hit the external time limit after that line. The result set shown is all passing; a clean process exit is not claimed. Focused groups listed above exited successfully.

## Performance snapshot

Command: `python3 tools/bench_vm_compact.py --rounds 9`, then `--rounds 15`. One warm-up is excluded in each batch. The metric is end-to-end wall time (startup, parsing/lowering, execution), not isolated VM throughput. Full raw samples are in `docs/BENCHMARK_RAW_2026-10-14.txt`.

| Batch | Mode | Median ms | Min ms | Max ms | N |
|---|---|---:|---:|---:|---:|
| 9 rounds | Tree | 23.622 | 21.912 | 27.399 | 9 |
| 9 rounds | Linear VM | 15.096 | 14.094 | 18.656 | 9 |
| 9 rounds | Compact VM | 16.266 | 14.300 | 16.897 | 9 |
| 15 rounds | Tree | 30.538 | 21.512 | 39.196 | 15 |
| 15 rounds | Linear VM | 16.118 | 13.616 | 24.855 | 15 |
| 15 rounds | Compact VM | 17.950 | 14.888 | 27.663 | 15 |

Both batches show VM modes faster than Tree by median on this loop benchmark. Compact VM is slower than linear VM by median in both batches (about 7.8% in the 9-round batch and 11.4% in the 15-round batch). However, the broad and shifting ranges reveal substantial environmental noise; this is directional evidence, not a precise performance ranking. The next useful measurement is repeated paired runs under a quieter system and additional representative workloads.
