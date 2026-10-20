# Applying W38

This delta is based on the uploaded `babyc(20261014-194337).zip` baseline (W36)
and includes the cumulative W37 storage-metadata propagation plus W38 typed-size
use in IR lowering.

Merge the archive's `babyc/` tree over the baseline, or apply `DELTA_W38.patch`
from the project root. Rebuild from clean state with `cd src && make clean && make -j2`.

See `VERIFY_W38.txt` and `docs/FP_IR_W38_TYPED_STORAGE_SIZES.md` for scope and
verification. The FP architecture probe remains intentionally red; do not
change its reference expectations to make it pass.
