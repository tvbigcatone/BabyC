# BabyC libc undefined-behavior audit — 2026-10-17

## Policy

BabyC is a teaching interpreter. Where a standard-library precondition violation is reliably detectable at the wrapper boundary, reject the operation before calling the host CRT and report a source-located diagnostic. Do not allow a host-specific result, a crash, or a plausible-looking result to hide the student's mistake. This does **not** imply that every C undefined behavior can be detected: arbitrary invalid pointers and object bounds for untracked stack/global storage require richer provenance/bounds metadata.

## First-pass coverage and changes

| Area | Audited entry points / rules | Current result |
|---|---|---|
| `string.h` (`src/cstdlib/string.c` and legacy `src/clibrary.c`) | `memcpy` overlap; `strcpy`/`strncpy`/`strcat`/`strncat` overlap; null string arguments; negative length values passed through BabyC's current `int`-based prototypes; null memory arguments for non-zero lengths | Guards and diagnostics added. `memmove` remains overlap-safe. `strncpy` legacy implementation also fixed to pad the destination with zeroes as required. `memset` legacy return value now returns destination. |
| `ctype.h` | ISO ctype argument domain: `EOF` or a value representable as `unsigned char`; includes `tolower`/`toupper` and custom `isblank` wrapper | Validated before host CRT. Test 1050. |
| `stdlib.h` conversions | Null input strings to `atof`/`atoi`/`atol`/`strtod`/`strtol`/`strtoul` | Explicit diagnostics added. |
| `stdlib.h` sort/search | Invalid count/size, null base/key where elements are required, null/unresolvable comparator, size multiplication overflow in `bsearch` | No longer silently returns for invalid non-empty calls. Test 1052 covers null `qsort` comparator. Valid comparator smoke test passed. |
| `stdlib.h` integer helpers and allocation | `abs(INT_MIN)`, `labs(LONG_MIN)`, invalid `free`/`realloc` pointers; tracking of `strdup` allocations; heap tracking capacity raised to 16,384 entries; `calloc` product overflow | Explicit diagnostics added for detectable cases. Test 1053 covers freeing a stack pointer. `strdup` allocations enter the same tracker so `free`/`realloc` can recognize them. Legacy `CLibrary` malloc/calloc/realloc/free now delegate to the same tracked implementation. |
| `stdio.h` | Null file/path/mode/buffer/format/position arguments for wrappers that require them; negative `fread`/`fwrite`/`snprintf` sizes; non-positive `fgets` size; invalid `fseek` origin; invalid `setvbuf` mode; null `%s`/`%n` pointer and missing/invalid `printf` arguments | Guards added. `fflush(NULL)` remains explicitly allowed. Test 1054 covers null `%s`. |
| `math.h` | Required output pointers for `frexp` and `modf` | Null output pointers diagnosed; fixed a discovered `modf` wrapper bug that passed its input value as the output pointer instead of argument 2. |
| `time.h` | Required `tm`, time, format, and output pointers for `asctime`, `ctime`, `gmtime`, `localtime`, `mktime`, `strftime`, `strptime`, `gmtime_r`, `timegm` | Detectable null/negative-size cases diagnosed. `time(NULL)` remains valid. |
| `stdint.h`, `stdbool.h`, `errno.h` | Module inventory | Primarily definitions/constants; no analogous general wrapper precondition set found in these modules during this pass. |
| `printf_format.c`, `mini_printf.c` | Formatter inventory | Main `stdio.c` formatting path now stops on null/incomplete format strings, missing/type-incompatible arguments, null `%s`, and invalid `%n` pointer. Exact C default-promotion/length-modifier compatibility is not yet exhaustively validated. |

## Regression tests added

- `962_ctype_invalid_arg.c`: `isalpha(-2)` must stop with a ctype-domain diagnostic.
- `963_strcpy_overlap.c`: overlapping `strcpy` must stop before the host CRT call.
- `964_qsort_null_comparator.c`: invalid comparator must stop.
- `965_free_stack_pointer.c`: `free(&local)` must stop rather than call host `free`.
- `966_printf_null_string.c`: `printf("%s", null_pointer)` must stop.
- `967_scanf_missing_destination.c`: missing destination for `%d` must stop before invoking host `sscanf`.
- `968_scanf_suppressed_conversion.c`: `%*d` consumes no pointer argument and the following `%d` receives the correct destination.
- `969_scanf_invalid_conversion.c`: invalid `%q` conversion must stop with a format diagnostic.
- Existing `084_memcpy_overlap.c` still verifies that overlapping `memcpy` stops while `memmove` succeeds.

## Validation

- GCC `-Wall -O2` build: PASS, no new compiler warnings in the build log.
- Previous audit checkpoint: dual suite A-IR/B-NO_IR/C-IR-SHADOW0 each reported 965/965 before the new scanf tests were added.
- Current full dual-suite rerun timed out at the 120-second tool limit before producing a final summary; therefore the modified tree's full-suite status is **not certified in this turn**.
- FP modes: 32/32 PASS on the modified tree.
- Focused tests 1050–1057: all eight expected outputs matched; 1050–1055 and 1057 exited with diagnostic status 1, while valid suppression test 1056 exited 0.
- Valid `qsort` smoke test: sorted `123`, exit 0.
- The full `test_bytecode_vm.py` gate and a targeted invocation both exceeded the 120-second execution window in this turn. No pass is claimed for that gate.
- Targeted ASan/UBSan runs for tests 083 and 1050–1054 all exited through the expected diagnostic (status 1) with no sanitizer report. Full sanitizer corpus and MSVC/Windows were not run.
- `BABYC_PATH=linear BABYC_STRICT=1` still reports `strict: tree fallback (body)` for 1050–1052, so those standard-library wrappers are not strict-Linear-covered yet; the regular dual suite remains green.

## Performance spot-check

Three-run median comparison, same host and GCC `-O2`, prior full archive vs current working tree (columns: ATS_no, ATS_acc, VM_no, VM_acc; seconds):

| Case | Prior 5-run median | Current 5-run median |
|---|---|---|
| `micro.isort` | 0.215, 0.006, 0.306, 0.007 | 0.233, 0.006, 0.301, 0.007 |
| `classic.isort` | 0.072, 0.067, 0.039, 0.041 | 0.066, 0.070, 0.039, 0.038 |
| `stress.call_loop` | 0.430, 0.425, 0.769, 0.737 | 0.423, 0.427, 0.744, 0.739 |

The five-run spot-check does not show a consistent VM regression: `stress.call_loop` VM_no improves about 3.3%, VM_acc is effectively unchanged; `micro.isort` VM_no improves about 1.6%; `classic.isort` VM_no is unchanged. ATS timing varies (notably `micro.isort`), so no blanket zero-cost claim is made. The workload is not a direct libc stress test; add dedicated string/allocator/stdio benchmarks before drawing conclusions about libc wrapper overhead.

## Remaining high-priority work

1. **Pointer provenance and object bounds:** detect out-of-bounds reads/writes for stack/global/aggregate objects and source ranges passed to `memcpy`, `memmove`, `str*`, `fread`/`fwrite`; current heap tracker does not prove arbitrary pointer validity. `BabycHeapCheckWrite` also has a detailed-check cap when more than 128 tracked allocations are live.
2. **Allocator tracking completeness:** the fixed table is larger but still bounded. APIs/extensions that return host-allocated memory must register it before guest `free`/`realloc`; table saturation needs a non-false-positive strategy.
3. **`printf`/`scanf` formats:** scanf now pre-parses conversion syntax and counts required destinations, catching missing arguments and malformed/unsupported conversions before the host CRT. Exhaustive destination-type compatibility after length modifiers, `%n` pointed-to type/size, and destination capacity for unbounded `sprintf`/`gets` remain open. `gets` remains inherently unsafe and is not yet replaced/disabled.
4. **Remaining C standard wrappers:** finish review of every `stdio`, `stdlib`, `string`, `math`, and `time` entry, including closed-stream state and non-null but invalid pointers. This pass targets clear boundary-detectable cases; it does not certify every possible UB.
5. **POSIX and optional extensions:** `src/cstdlib/unistd.c`, `src/posix/*`, `src/cstdlib_ext/*`, and external-library bindings require a separate pass; they are not certified by this core-libc sweep.
6. Run the bytecode gate with a longer timeout, sanitizer corpus, and MSVC/Windows validation.
