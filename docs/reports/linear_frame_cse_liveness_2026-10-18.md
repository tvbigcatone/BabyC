# Linear VM — per-function register frame and expression-local reuse (2026-10-18)

## Changes

1. `FnCodeSlot.RegCount` records the highest register operand used by the compiled function (including call argument and indirect-call registers). The function executor now initializes only this count and uses a dynamically sized stack allocation (`alloca` POSIX / `_alloca` MSVC) rather than a fixed 64-register array. `IR_LIN_REGS` remains the hard upper bound.
2. Ordinary binary expressions reserve their result register before lowering operands. Child temporaries are younger registers; after emitting the parent operation, `Next` is rewound to `result_reg + 1`.
3. CSE slot and indexed-expression cache entries now pin their result register immediately when stored, and unpin when invalidated. The old behavior marked new cache entries unpinned, allowing the expression-local allocator to overwrite a register still referenced by a cache entry. This was the root cause of the newly observed wrong results in tests such as `103_return_in_for`, `409_dense_dual_index`, `544_ir_struct_array`, `712_crc_unsigned_shift`, and `728_heap_store_lost`. They pass after the cache-lifetime fix.

## Frame cost / dfs_rec

The old `IrExecLinLocals` had `long long R[IR_LIN_REGS]` and zeroed all 64 entries on every body entry. The function-specific path now allocates/initializes `RegCount` entries. This removes the 64-register clear and reduces per-call register stack footprint. `dfs_rec` A/B measurements are below; the result is not a performance win in this run and fails the no-regression gate, so the frame optimization is structurally in place but needs further performance investigation.

## Validation

- Clean Linux/GCC build: passed.
- Tree STRICT=0, ACCEL=0: 996/996 pass, 0 fail, 0 timeout.
- Tree STRICT=0, ACCEL=1: 996/996 pass, 0 fail, 0 timeout.
- Linear STRICT=1, ACCEL=0: 979 pass, 17 fail, 0 timeout.
- Linear STRICT=1, ACCEL=1: 979 pass, 17 fail, 0 timeout.
- The 17 Linear failures are unchanged historical baseline failures: `047_grep`, `058_macro_bug`, `062_initializers`, `126_csmith_modulo_zero`, `288_heap_size_classes`, `395_array_fn_loop_local_global`, `454_complex_dispatch`, `606_storm_inc_dec_member_call`, `609_storm_enum_union_fptr`, `618_inc_member_call_arg`, `651_sqlite_exec_null_varargs`, `789_vm_pointer_fallback`, `873_alignment_extended`, `874_array_member_storage`, `893_vm_struct_return_effect_fallback`, `947_qsort_bsearch`, `948_qsort_stable`.
- Targeted regression tests for expression register reuse / cache lifetime pass: `103_return_in_for`, `409_dense_dual_index`, `544_ir_struct_array`, `712_crc_unsigned_shift`, `728_heap_store_lost`, `805_vm_operator_matrix`.

## Benchmark A/B (median ms, 3 runs; baseline vs current)

| Benchmark | ACCEL | Baseline | Current | Change |
|---|---:|---:|---:|---:|
| string_scan | 0 | 270.151 | 303.571 | +12.4% |
| string_scan_bounded | 0 | 315.194 | 396.608 | +25.8% |
| struct_score | 0 | 63.012 | 85.190 | +35.2% |
| dfs_rec | 0 | 24.871 | 28.741 | +15.6% |
| string_scan | 1 | 4.984 | 6.552 | +31.5% |
| string_scan_bounded | 1 | 6.049 | 9.427 | +55.8% |
| struct_score | 1 | 1.661 | 3.681 | +121.7% |
| dfs_rec | 1 | 8.965 | 15.956 | +78.0% |

All benchmark executions exited 0 and produced output, but the performance gate fails. Do not treat this delta as performance-approved. The next investigation should isolate whether broad cache pinning or the result-register reservation causes the slowdown; correctness/cache lifetime is not to be sacrificed to hide it.

## Limitations

- Linux/GCC build and matrix verified. MSVC build has not been run in this environment; `_alloca` is used for source-level MSVC compatibility, but that is not a claim of Windows validation.
- Expression-local reuse currently covers the ordinary scalar binary-expression lowering branch; pointer-scaled binary lowering and other specialized expression branches still need the same discipline where safe.
