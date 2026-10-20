# Applying W37

This is a cumulative package based on the uploaded W36 baseline. Merge/replace the `babyc/` tree with the archive contents and use the project's normal build procedure.

Source changes:
- `src/type.c`: propagate FP storage size/alignment through derived array types and aggregate layout.
- `src/ir_mem.c`: use the central FP storage-size helper for known FP pointee stride.
- `src/ir_lower.c`: use FP storage size/alignment helpers in aggregate-layout inference.

Validation: see `VERIFY_W37.txt` and `docs/FP_IR_W37_STORAGE_METADATA_PROPAGATION.md`. The architectural FP probe is expected to remain red until the frontend distinguishes `float` from `double` and the IR carries precision/rounding semantics.
