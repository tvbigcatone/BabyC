# W41 — FP type classification groundwork

## Scope

This delta removes pointer-identity assumptions in four existing FP dispatch points. Expression prefix/postfix arithmetic, mixed FP arithmetic, formatted output, and IR member metadata now recognize floating types through `ValueType.Base == TypeFP`. `TypeAdd()` also initializes `FPFormat` explicitly to `FPStorageNone` rather than relying on allocator zeroing.

## Why this change is limited

The frontend still maps both `float` and `double` to `pc->FPType`. This delta does **not** introduce a distinct binary32 semantic type, change object layout, add IR precision tags, or claim full FP architecture support. The `audit_fp_ir_arch.py` probe remains expected to fail until those larger changes are implemented.

## Verification

- Clean `make clean && make -j2`: PASS, zero compiler warnings and errors.
- `tools/test_fp_modes.py --babyc src/babyc`: PASS, 28/28.
- Focused `023_floating_point`, `063_float`, `666_fp_member_index`: PASS, 9/9 across default, linear VM, and token modes.
- `tools/audit_fp_ir_arch.py --babyc src/babyc`: still fails, 0/4 modes match GCC; details in `logs/FP_W41_ARCH_PROBE.log`.
- Full `tools/run_suite.py` did not finish within the execution window; no full-suite result is claimed.

## Next architectural steps

1. Separate object storage width from the double-sized FP execution slot in allocation, copies, returns, and argument binding.
2. Introduce distinct `float` and `double` semantic types only after every consumer respects that split.
3. Carry precision through IR constants, loads/stores, conversions, calls/returns, arrays, pointers, and aggregate layout.
4. Require the GCC architecture probe and FP differential tests to pass in Tree, linear VM, compact VM, and token modes before considering the work complete.
