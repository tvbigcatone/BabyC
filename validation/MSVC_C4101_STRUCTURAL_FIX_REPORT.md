# Structural MSVC C4101 fix — 2026-10-20

## Changes
- Moved POSIX-only locals into the `#else` branch in `posix_unistd.c`, `posix_fd.c`, `posix_dirent.c`, `posix_stat.c`, and `posix_more.c`.
- Removed the now-unnecessary `(void)native_dir` from `BabyPosixOpenatCall` and `BabyPosixFstatatCall` because those locals now exist only in the POSIX branch.
- Kept locals that are genuinely used in both branches, such as `native_fd` in pread/pwrite, outside the conditional.

## Validation status
- Source transformation assertions: PASS (each targeted edit matched exactly once).
- MSVC compilation: NOT RUN here; this environment has no Windows/MSVC toolchain.
- POSIX build and tests: NOT RUN in this step. Do not interpret this report as a clean-build claim.
