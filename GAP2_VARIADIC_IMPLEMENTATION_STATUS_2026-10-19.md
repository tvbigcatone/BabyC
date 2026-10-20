# Gap 2 — user-defined variadic functions: root fix for 1035 (2026-10-19)

## Result

**1035 is fixed at the shared call-ABI boundary.** The solution carries the complete variadic argument vector through both the Value-based token/Tree ABI and the raw 64-bit IR/Linear ABI. `va_start` identifies the actual variadic function frame using an explicit `IsVariadic` flag; it does not infer variadicness from whether extra arguments happened to be passed, so zero-extra calls can initialize a cursor too.

`va_arg` is parsed as a typed operation rather than an ordinary macro whose `ap` argument is evaluated before substitution. The same cursor/bit-reading helper is shared by Tree and Linear, and Linear has its own emitted `IrLinVaArg` instruction. `va_copy` duplicates cursor state independently, and `va_end` invalidates/frees the cursor and clears the list handle. Variadic functions skip scalar-only call shortcuts and use a per-depth ABI frame; non-variadic hot calls keep their existing fast paths.

The implementation uses an opaque pointer representation for `va_list` so the same cursor can be stored in a token `Value` or a Linear local slot. Macro definitions are registered individually under `__babyc_va_list_ok`, with `VariableDefined` checks per name; no `#undef` or repeated redefinition is used.

## Validation

- Full clean Linux/GCC build: PASS, zero `warning:` and `error:` diagnostics.
- Tests 1032–1038: **35/35** expected-output checks pass across Token, Tree ACCEL=0/1, and Linear STRICT ACCEL=0/1.
- Variadic-specific tests 1035, 1037, 1042, 1043, 1044: **25/25** expected-output checks pass across Token, Tree ACCEL=0/1, and Linear STRICT ACCEL=0/1.
- Linear STRICT coverage for 1035: `fncode_body=1`, `tree_body=0` for both ACCEL=0 and ACCEL=1; no Tree body fallback.
- A temporary zero-extra-argument variadic smoke program passes in Token and Linear STRICT; Tree mode uses its expected body fallback for that temporary source. No new repository tests were added.
- MSVC and the full performance benchmark suite were not run in this environment; no benchmark speedup is claimed.

## Root causes closed

1. Generic macro argument evaluation treated `va_arg(ap, T)` as an ordinary expression call and read the uninitialized `va_list` before expansion.
2. The IR fallback populated the fixed-parameter slots but left the variadic slots null even while reporting the full argument count.
3. Linear call frames did not expose the active variadic ABI payload to `va_start`/`va_arg`.
4. Detecting a variadic function from `ArgumentCount > FixedArgumentCount` incorrectly rejected legal zero-extra calls; explicit variadic metadata now travels with each call frame.

## Remaining qualification

The current IR ABI transports variadic arguments as 64-bit payloads and interprets them according to the requested `va_arg` type. This covers the exercised integer, promoted narrow-integer, floating-point, pointer, copy, and forwarding cases; a broader cross-platform MSVC run remains outstanding.
