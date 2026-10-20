# BabyC — gap 2 follow-up (attempt15)

## Baseline and scope

This is a cumulative delta over attempt14, which itself is cumulative over the fixed BabyC baseline. The archive retains the prior cumulative changes and applies the new changes described below.

## Implemented

- `src/ir_frame.c`: `IrFrameMarkAddressInitialized()` no longer treats `NumLocals` as the addressable-local bound. It checks the complete physical `LocalVal[]` address interval using `uintptr_t`, verifies slot alignment, calls `IrFrameNoteLocalSlot()`, then marks the discovered slot initialized. This addresses the root cause where `&i` belonged to a valid frame slot but fell beyond the currently noted high-water mark.
- `src/cstdlib_ext/stdarg.c`: `StdargVaArgBits()` selects the ABI union member based on `RequestedType` for floating-point, pointer, and unsigned integer requests instead of unconditionally reading `Payload.I`. This improves typed decoding at the consumer boundary.
- Added GCC `-O0` expected-output tests 1061–1063:
  - 1061: array locals declared before scalar targets; `sscanf` and `vsscanf` write int, double, long long, unsigned, and char-array targets.
  - 1062: mixed promoted arguments (int, unsigned, long, long long, float promoted to double, and pointers) are read via `va_arg` and independently forwarded via `va_copy` to `vsnprintf`.
  - 1063: active va_lists across nested variadic calls, interleaved reads, and `va_copy`.

## Verification performed

- `make clean && make -j8`: success, 0 warnings and 0 errors.
- Numbered suite matrix: all 1047/1047 cases passed in each of the six configurations:
  - Tree ACCEL 0 and 1
  - Linear STRICT ACCEL 0 and 1
  - Linear non-STRICT ACCEL 0 and 1
- Canonical type gate: `reports=1047/1047`, `invalid_nodes=0` in all six configurations.
- Linear non-STRICT no-fallback gate: `violations=0`, `tree_body=0` for ACCEL 0 and 1. Linear STRICT also reported `tree_body=0`.
- Tests 1061–1063 match GCC `-O0` output across all six configurations.
- A/B versus attempt14: seven alternating runs per binary per benchmark, for `string_scan`, `string_scan_bounded`, `struct_score`, and `dfs_rec`, at ACCEL 0 and 1. All outputs matched. Median attempt15/attempt14 ratios:
  - ACCEL 0: `string_scan` 0.9980; `string_scan_bounded` 0.9471; `struct_score` 1.0161; `dfs_rec` 1.0043.
  - ACCEL 1: `string_scan` 0.9418; `string_scan_bounded` 0.9945; `struct_score` 1.0571; `dfs_rec` 0.9641.
  These are short-run measurements; no broad performance claim is inferred from them.
- MSVC/LLP64 was not run in this environment.

## Explicitly still open

Gap 2(b) is **not fully closed**: the current call boundary still does not consistently populate `IrCallAbiSlot.Type` and `Payload.D/U/P` from each extra argument's statically promoted type at every Tree/Linear call site. `StdargVaArgBits()` now decodes by the requested type, and test 1062 exercises mixed-type behavior, but that does not replace caller-side type propagation. I am not claiming that requirement complete.

The scanf initialization root cause, tests 1061–1063, six-way correctness/canonical/no-fallback gates, and requested A/B run count are covered by the validation above.
