# Applying the W46 delta

This is a cumulative file-replacement delta relative to `babyc(20261014-194337).zip` (W36), incorporating the source/doc changes from W37–W45 plus the W46 `ExpressionAssign()` boundary fix.

1. Extract the original baseline into a working directory.
2. Extract this ZIP into the project root (`babyc/`), preserving relative paths and allowing changed files to be overwritten.
3. Rebuild with `make -C src clean && make -C src -j2`.
4. Run `python3 tools/test_fp_modes.py --babyc src/babyc`. The architecture audit remains intentionally red: `python3 tools/audit_fp_ir_arch.py --babyc src/babyc` currently reports 0/4 modes matching GCC.

The ZIP does not include generated executables or object files. `MANIFEST_W46.txt` lists each payload file and its SHA-256.
