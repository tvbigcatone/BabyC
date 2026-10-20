# Applying W47

1. Start from the W46 cumulative source tree (or the `babyc(20261014-194337).zip` baseline plus W37–W46 deltas).
2. Apply `DELTA_W47.patch` from the repository root with `patch -p1 < DELTA_W47.patch`.
3. Build with `make -C src clean && make -C src -j2`.
4. Run `python3 tools/test_fp_modes.py --babyc src/babyc`.
5. Run `python3 tools/audit_fp_ir_arch.py --babyc src/babyc`; it is expected to remain failing until the distinct float/double architecture is implemented end-to-end.

Only `src/ir_compile.c` is changed by W47. The full regression suite did not finish within the available 300-second execution window.
