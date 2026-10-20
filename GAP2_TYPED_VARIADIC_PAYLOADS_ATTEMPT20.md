# GAP2 typed variadic payloads — attempt20

## Scope delivered

This is a cumulative source delta over `babyc_baseline_2026-10-18_loop_promotion.zip` plus attempt18. It changes the runtime call ABI and adds tests 1073–1074; it also carries forward attempt19 tests 1070–1072.

- `IrCallAbiBindSlots` now accepts `ArgTypes`, assigns each slot's type (fixed arguments use the declared parameter type; extra arguments use the caller's promoted static type), and writes D/P/U/I according to the slot type.
- Tree expression calls construct promoted `ArgTypes` from canonical expression types and pass them through the four `ir_call.c` binding paths.
- Linear call metadata now carries promoted argument base types, including FP and pointer distinctions, through `FnCodeSlot`; the fifth binding path in `ir_lin_exec.c` uses those types. Linear `va_arg` passes the requested type to the shared runtime helper.
- `StdargVaArgBits` rejects null IR slot types with `untyped variadic slot`, reads payloads based on `Slot->Type`, and reports `va_arg type mismatch`. Tree/Value-backed reads also check the type and use the same mismatch diagnostic.
- Added `1073_variadic_many_args.c` (14 alternating int/double extras) and `1074_variadic_type_mismatch.c` (int supplied, double requested). Tests 1070–1072 from attempt19 are included for the cumulative regression set.

## Verification performed

- GCC build at `-O0`: completed; compiler output contained no warnings.
- Tests 1070–1074: 5/5 matched expected output/diagnostic in Tree, Linear STRICT, and Linear non-STRICT (`BABYC_PATH=linear`, `BABYC_COV=1`, `BABYC_ACCEL=0`).
- GCC `-O0` output comparison: 1070–1073 all matched GCC. Test 1074 intentionally exercises undefined behavior; its expected result is BabyC's diagnostic, not a GCC oracle.
- Smoke tests 1032–1069, run in blocks of ten-number ranges with `BABYC_ACCEL=0`: Tree passed all present fixtures (7/7, 9/9, 6/6, 8/8); Linear non-STRICT with coverage enabled passed the same counts in all four blocks. No smoke mismatch was observed; no full suite was run.
- No `-O2` build, full six-configuration matrix, canonical gate summary, A/B benchmark, or MSVC/LLP64 build was run in this turn. The explicit gate self-check that counts extra slots with `Type == NULL` was **not added** in this partial attempt.

## Remaining limitation (important)

The existing expression/type metadata does not yet preserve exact integer-width compatibility consistently through every legacy Tree/IR argument path. To avoid regressing established variadic fixtures 1037 and 1062, the current compatibility helper accepts mismatches within the integer family while still rejecting cross-category mismatches such as `int` versus `double`; pointer compatibility is also permissive because legacy Tree packing may represent object pointers as `void *`. This is an acknowledged partial implementation, **not** a claim that all C `va_arg` compatibility cases are now strictly enforced. The next correctness step is to repair the upstream static type propagation so the compatibility check can be exact without these allowances.

The known parser gap for variadic function-pointer declarators (`long long (*fp)(int, ...)`) was not touched.
