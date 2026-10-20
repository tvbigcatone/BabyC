# Applying W39

W39 is cumulative from the user-supplied `babyc(20261014-194337).zip` baseline (W36). It contains the previous W37/W38 work plus the W39 FP storage-alignment propagation.

Apply `DELTA_W39.patch` from the project root, or overlay the files in this archive onto the extracted `babyc/` tree. Rebuild with `cd src && make clean && make -j2`.

See `VERIFY_W39.txt`, `docs/FP_IR_W39_LAYOUT_METADATA.md`, and `logs/FP_W39_*.log`. The FP architecture probe remains intentionally failing; this package does not claim complete binary32 support.
