# FP IR W66 — pointer-to-pointer locals and scalar FP initialization

## Defect reproduced

A new regression (`src/tests/098_fp_alias_store_matrix.c`) combines:

- a local `float *` copied into another alias;
- a local `float **` initialized with `&q`;
- recursive calls that write through `*p` and `**pp`;
- `float` array writes using both indexing and pointer arithmetic.

GCC reference output:

```text
alias=3.50 values=3.25,4.50
```

Before the fix, default IR and `BABYC_EXEC=vm` crashed on the `**pp` write. Token mode passed.

## Root cause

The `IrStDecl` execution path in `src/ir_stmt.c` had a scalar FP initializer fast path keyed on `DeclFromType->Base == TypeFP`, but did not exclude pointer declarations. For `float **pp`, the stored pointee type is `float`; the path therefore interpreted the pointer initializer as a scalar FP value and converted the address bits into a floating-point representation. Dereferencing `pp` then attempted to load a pointer from an invalid address.

## Source fix

The scalar FP initializer branch now requires both `!DeclIsPtr` and `!DeclIsFptr`. Pointer declarations retain the raw pointer value, including multi-level pointers whose pointee type is FP. The earlier W64/W65 changes to `src/ir_mem.c` remain included cumulatively.

## Validation

- Clean GCC build: PASS; no `warning:` or `error:` lines in the build log.
- `tools/test_fp_modes.py --babyc src/babyc`: **32/32 PASS**.
- Four targeted alias tests (`open_fp_alias_minimal`, `open_fp_alias_copy`, `open_fp_alias_cross_frame`, `098_fp_alias_store_matrix`) in default, `BABYC_EXEC=vm`, `BABYC_NO_IR=1`, and `BABYC_NO_KERNEL=1 BABYC_NO_SHAPE_FP=1`: **16/16 PASS**.
- New matrix output matches GCC in all four modes.
- `tools/test_bytecode_vm.py --babyc src/babyc`: the run exceeded the session time limit after passing through test `845_vm_global_narrow_incdec.c`; the full suite is **not claimed as passed**.
- MSVC build not run.

## Baseline and package

This is a cumulative delta against `babyc(20261015-093958).zip`, not against W65. The patch is intended to apply with `patch -p1` from the extracted baseline directory.
