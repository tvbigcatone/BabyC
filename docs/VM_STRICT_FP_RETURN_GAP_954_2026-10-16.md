# VM strict FP return gap — test 954

Cumulative fix retained from the preceding delta: `src/ir_lower.c` refreshes the binary32 value/proxy before copying when alias mutation may have made the double proxy stale. This avoids copying stale FP state without prematurely splitting frontend token paths. The existing targeted test `954_vm_strict_fp_return` was checked in `tree`, `vm` and `vm_strict`; expected output: `3.000000 1.000000 2.500000 3.000000 8.000000`.
