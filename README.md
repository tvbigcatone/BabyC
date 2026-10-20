# IR: *f() / f()[i] / (*f()).m / fptr array / multilevel

## New
- Unary `*` on `IrExCall` (`*f() = v`)
- Index on Call (`mid()[i]`)
- `int *(*tab[N])(void)` fptr array decl
- `(*f()).m` member resolve (call returns struct*)
- Prior cov-push included

## 100% IR suite wins
451, 455, 456, 457, 458 (+ prior 67, 247, 061, 082, ...)

## Residual
bitfield return, typedef complex (121), fprintf, diagnostics, stack overflow intentional

## Phase 6 canonical expression types

See `PHASE6_CANONICAL_TYPE_REPORT.md` for the canonical result/operation type metadata, removal of legacy signedness/width inference, new tests 1030/1031, six matrix results and the A/B caveat. The runner's `CANONICAL_TYPE_GATE` is enabled automatically by `tools/run_numbered_matrix.py` and requires zero evaluated/emitted nodes without `IR_CF_VALID`.
