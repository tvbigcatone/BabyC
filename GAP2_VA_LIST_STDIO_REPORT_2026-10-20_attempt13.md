# Gap 2 — v* formatted I/O bridge (attempt13)

## Changes in this delta

- `src/cstdlib/stdio.c`: `StdVararg` can now represent either ordinary `Param[]` arguments or a BabyC `va_list` cursor. `StdioVarargNext()` obtains each requested argument through `StdargVaArgValue()` using the type selected by the format parser. `vprintf`, `vfprintf`, `vsprintf`, `vsnprintf`, `vscanf`, `vfscanf`, and `vsscanf` now pass a cursor-backed argument source instead of treating `va_list` as `struct StdVararg *`.
- `src/ir_call.c` and `src/ir_lin_exec.c`: call ABI slots are bound only for variadic callees, avoiding slot-binding work on ordinary calls.
- Added regression tests 1056 (`vsnprintf` forwarding with mixed arguments), 1057 (`vprintf` and `vfprintf` forwarding), and 1058 (`vsscanf` forwarding). Expected output was checked against GCC `-O0`.

## Verification performed

- `make clean && make -j8`: passed, 0 warnings, 0 errors.
- Existing expected-output suite: 1049/1049 in each of six requested execution configurations (Tree ACCEL 0/1; Linear STRICT ACCEL 0/1; Linear non-STRICT ACCEL 0/1).
- Tests 1056, 1057, and 1058: passed individually in all six configurations with output matching GCC `-O0`.
- Packaging built from attempt12; entries listed in `DELETE_FILES.txt` and build artifacts are excluded. `CHANGED_FILES_SHA256.txt` is regenerated for the archived changed files.

## Still open / not claimed complete

- `IrCallAbiSlot` and `IrCallAbiBindSlots()` are not yet fully typed end-to-end for all variadic expressions: extra argument static types/default promotions are not propagated at every call site, and the slot payload still relies on the scalar register payload in some paths. Gap 2(b) is therefore still open.
- No two-Picoc-instance isolation test was added or run for `StdargMacroValues`; point (c) remains unvalidated.
- The full six-configuration suite was run, but canonical gate / `tree_body` counters were not separately collected for this attempt.
- No seven-run alternating A/B benchmark batch was run. No MSVC/LLP64 build was available in this environment.
