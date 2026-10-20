# BabyC variadic intrinsics — phase report (2026-10-19)

## Implemented

- Removed spelling-based `strcmp` dispatch for `va_start`, `va_arg`, `va_end`, and `va_copy` from `ExpressionParseStdargStorageCall`. The parser dispatches using the registered `MacroDef *` identity assigned by `stdarg.c`.
- Removed the `strcmp("va_arg")` special case from IR compilation; typed `va_arg` lowering consults the same stdarg macro registration.
- Added numbered tests 1045–1047, each with GCC `-std=c99 -O0` expected output:
  - 1045: `char`/`short` integer promotions and `float` promoted/read as `double`.
  - 1046: pointer, `long long`, and struct-pointer payloads.
  - 1047: `va_copy` remains usable after `va_end` on the original list.

## Verification from the cleaned overlay

The fixed baseline was extracted, every path in `DELETE_FILES.txt` was removed, and the cumulative overlay was applied. In the exact extracted overlay, `make clean && make` with `-O0` completed with zero warnings and zero errors, and the full six-way matrix below passed. A default `-O2` clean build completed with zero warnings/errors in the working tree before packaging; the separate exact-overlay `-O2` rebuild exceeded the available execution window and is not claimed as completed.

The numbered matrix contains 1036 tests after adding 1045–1047. Results in all six configurations:

- Tree ACCEL=0: 1036/1036; canonical reports 1036/1036, invalid nodes 0.
- Tree ACCEL=1: 1036/1036; canonical reports 1036/1036, invalid nodes 0.
- Linear STRICT ACCEL=0: 1036/1036; canonical reports 1036/1036, invalid nodes 0.
- Linear STRICT ACCEL=1: 1036/1036; canonical reports 1036/1036, invalid nodes 0.
- Linear non-STRICT ACCEL=0 with `--require-no-tree-fallback`: 1036/1036; canonical reports 1036/1036, invalid nodes 0, tree_body=0.
- Linear non-STRICT ACCEL=1 with `--require-no-tree-fallback`: 1036/1036; canonical reports 1036/1036, invalid nodes 0, tree_body=0.

The three new test outputs match GCC `-O0`. No performance benchmark was run, and MSVC was not run; performance neutrality and MSVC portability are therefore not asserted as measured results.

## Explicitly open

The ABI payload for variadic arguments is still carried through the IR/Linear call boundary as a 64-bit value without per-argument type metadata. The new tests exercise the requested values and pass, but this does **not** complete the requested architectural change to typed variadic payloads or establish a general typed-promotion contract. That work remains open. MSVC has not been run in this environment.

## Packaging

This delta is cumulative against `babyc_baseline_2026-10-18_loop_promotion.zip`. The package contains no path listed in `DELETE_FILES.txt` and no `.o`, executable, `.pyc`, or `__pycache__` build artifact. `CHANGED_FILES_SHA256.txt` records the SHA-256 of every overlay file in the archive other than the checksum file itself.
