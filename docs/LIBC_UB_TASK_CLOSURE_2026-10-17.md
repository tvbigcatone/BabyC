# BabyC libc undefined-behavior task closure — 2026-10-17

## Closure rule

BabyC diagnoses contract violations that can be checked reliably at the guest-to-host library boundary, before entering the host CRT. This is not a claim that arbitrary C undefined behavior is statically or dynamically detectable: a non-null dangling pointer, a wrong-but-non-null pointer, or an undersized stack/global destination cannot be validated in general without stronger object-provenance metadata. The diagnostic policy is to catch known local preconditions without pretending those broader cases are solved.

## Final fixes

### libc wrapper boundary

- `memcpy` now diagnoses overlapping source/destination ranges; `memmove` continues to permit overlap. String copy/concatenation wrappers also check detectable overlap, null pointers, and negative lengths.
- `ctype` wrappers validate the C domain (`EOF` or an unsigned-char value) before calling the host CRT.
- `stdlib` checks detectable invalid `free`/`realloc`, null conversion inputs, allocation-size overflow, invalid comparators and counts/sizes, and signed minimum absolute-value overflow. `strtol`/`strtoul` reject bases outside `0` or `2..36` before host libc.
- `qsort`/`bsearch` validate required array arguments, element sizes/counts and comparator pointers; `qsort` now rejects a missing comparator even for a one-element array.
- `stdio` wrappers validate null files/buffers and invalid sizes/modes; printf/scanf format and argument checks run before calling the host CRT. The `scanf` validator rejects incomplete/invalid conversions, unterminated scansets, incompatible length modifiers, `%n` suppression/width, and missing destination arguments. `%%` and suppressed conversions do not consume destination arguments; surplus variadic arguments are permitted and ignored when not consumed by the format.
- `scanf`'s host argument slots are initialized, and the wrapper passes only the guest destination arguments actually consumed by the format. Because BabyC currently stores `long double` using the same object representation as `double`, `%Lf` is normalized to `%lf` before calling host `scanf`; otherwise a host `long double` write could exceed the guest object.
- `gets` is disabled with a source-located diagnostic directing students to `fgets`, because its interface has no destination-capacity parameter.
- `math` and `time` wrappers validate required output/input pointers; `modf` uses the correct output pointer. `strptime` validates its three required pointers on platforms where it is available.
- The legacy optional-extension sources also received boundary guards: `bcopy`/`bzero` reject negative sizes and null buffers for nonzero lengths; `stat`/`fstat` reject null output buffers and `stat` no longer copies uninitialized native `struct stat` contents after a failed host call; the legacy `dirent` wrappers check null handles/pointers and allocation failure. These three legacy source files are not part of the current Makefile's default link set; they passed a GCC syntax-only compile, but are not counted as runtime-tested in the current executable.

### Memory-cleanup fixes found by the sanitizer gate

The full ASan/UBSan run exposed additional real leaks in nearby execution/parse paths, which were fixed rather than ignored:

- Macro-call argument expression trees are released after their nodes have been cloned into the expansion. Owned joined-string constants are deep-copied when an IR expression is cloned, so freeing the source tree cannot invalidate the clone.
- `sizeof("...")` releases the expression tree after folding to a constant.
- Lexing scratch allocations are registered for failure cleanup; `LexFail` releases the active scratch buffers. `PicocParse` registers source ownership before lexing, so a lexical error cannot bypass cleanup of the input buffer.
- Anonymous bitfield descriptors are freed after affecting layout because they are intentionally not inserted into the member table.

### Safe input handling

- Tests 1050–1063 cover invalid `ctype`, overlapping `strcpy`, invalid `qsort` comparator, freeing a stack pointer, null `%s`, missing/suppressed/invalid `scanf` conversions, disabled `gets`, incompatible `scanf` length modifiers, invalid `strtol` base, a null `qsort` comparator with one element, and valid surplus `scanf` arguments.
- An IR-only probe under `tests/samples/1060_scanf_long_double_storage_ir_probe.c` checks `%Lf` storage normalization. The legacy NO_IR parser does not support the `long double` spelling, so this probe is not counted as a dual-mode suite test.

## Verification results

- Clean GCC `-Wall -O2` rebuild: PASS; no compiler warnings in the final build log.
- Complete expected-output suite, executed in 10 tranches with a 45-second per-test timeout:
  - IR: **973/973 PASS**
  - NO_IR: **973/973 PASS**
  - IR with `BABYC_IR_SHADOW=0`: **973/973 PASS**
  - Total: **2,919/2,919 executions PASS**.
- Complete AddressSanitizer + UndefinedBehaviorSanitizer expected-output suite, also executed in 10 tranches with leak detection enabled: **973/973 PASS**, no sanitizer diagnostics.
- Additional targeted ASan/UBSan runs of the UB diagnostics, scanf compatibility cases, `%Lf` storage probe, macro/string/sizeof regressions, anonymous bitfield layout, and lexical-error cleanup: PASS, no sanitizer diagnostics.
- Tests 1050–1059 and 1061–1063 also match their expected output under the default engine, `BABYC_EXEC=vm`, `BABYC_EXEC=both`, and `BABYC_BYTECODE_VM=1`: **52/52 targeted mode/test combinations PASS** after excluding the engine-selection banner from output comparison.
- `make perf_gate` completed. Strict comparison against `tools/perf_baseline_babyc8.json` passed for all seven main benchmarks: AST-accelerated median times were 23.3%–46.7% faster than the stored baseline in this run. This is a whole-program benchmark, not a microbenchmark of libc wrapper checks.

## Coverage boundary and platform status

- The audit covers the standard `string.h`, `ctype.h`, `stdlib.h`, `stdio.h`, `math.h`, and `time.h` wrapper paths plus an inventory of POSIX/optional extension wrappers.
- POSIX syscalls commonly return an error for invalid addresses; they are not all reclassified as C undefined behavior. Their wrappers are not claimed to provide a general pointer-provenance validator.
- A complete MSVC/Windows build could not be run in this Linux environment. The changes are in standard-C code paths, with `strptime` guarded out on Windows, but Windows/MSVC remains an external platform verification item rather than a claimed pass.

## Semantic boundary

No runtime can reliably detect every instance of C undefined behavior from pointer values alone. BabyC's verified guarantee here is narrower: known, locally-checkable violations are diagnosed before unsafe host-library calls; valid covered paths remain regression-tested; and the report distinguishes verified behavior from unavailable platform checks and deeper provenance limitations.
