# Applying W36

This is a cumulative project package based on W35. Replace/merge the `babyc/` tree with the contents of this archive, preserving the project's normal build procedure.

Source changes:
- `src/interpreter.h`: add `FPStorageFormat`, `ValueType.FPFormat`, and helper declarations.
- `src/type.c`: add centralized storage size/alignment helpers, mark the legacy FP type as binary64, and route FP scalar/array storage sizing through the helper.

Verification:
- `cd src && make clean && make -j2`
- `python3 tools/test_fp_modes.py --babyc src/babyc`
- `python3 tools/audit_fp_ir_arch.py --babyc src/babyc` (expected red until binary32 semantics are implemented)

See `VERIFY_W36.txt` and `docs/FP_IR_W36_STORAGE_METADATA.md` for precise scope and limitations.
