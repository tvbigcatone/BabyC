# Applying W40

W40 is cumulative with W37–W39 and is based on the user-supplied W36 baseline `babyc(20261014-194337).zip`.

For a clean merge, extract `BABYC_FP_IR_W40_CUMULATIVE_2026-10-14.zip`. To apply only W40 on top of the W39 cumulative tree, use `DELTA_W40.patch` from the W40 delta archive from the project root (`patch -p1 < DELTA_W40.patch`). Review `VERIFY_W40.txt` before treating this as accepted FP support: the FP mode gate passes, but the architecture probe remains intentionally red.
