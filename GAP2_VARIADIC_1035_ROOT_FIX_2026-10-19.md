# BabyC Gap 2 — 1035 root fix, 2026-10-19

## Code changed

- `src/cstdlib_ext/stdarg.c`: guarded one-time, individually checked macro registration; opaque cursor; shared cursor creation/copy/end and typed payload reading; `va_start` uses explicit variadic metadata.
- `src/interpreter.h`, `src/variable.c`, `src/expression.c`: shared Value call ABI records complete arguments and an explicit `IsVariadic` bit.
- `src/ir_runtime.h`, `src/ir_frame.c`, `src/ir_call.c`: raw-payload call ABI on per-depth IR frames, including all variadic arguments; scalar-only shortcuts are bypassed for variadic functions.
- `src/ir.h`, `src/ir_compile.c`, `src/ir_eval.c`, `src/ir_lin.h`, `src/ir_lin_emit.c`, `src/ir_lin_exec.c`: typed `va_arg` expression node and dedicated Linear instruction.
- `src/include.c`: no separate `SetupCSource` parse of stdarg macros, avoiding duplicate definitions.

## Validation

- Clean build: `cd src && make clean && make -j8 babyc`; exit 0, zero warnings/errors.
- 1032–1038: 35/35 pass over five configurations (Token, Tree ACCEL 0/1, Linear STRICT ACCEL 0/1).
- 1035, 1037, 1042–1044: 25/25 pass over the same five configurations.
- Linear STRICT 1035 and 1037–1044 variadic gates: `tree_body=0`, `fncode_body=1`.
- No new repository tests; a temporary zero-extra variadic call was also checked in Token and Linear STRICT.
- Not run: MSVC build and full performance benchmark suite.
