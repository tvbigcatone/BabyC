# Gap 2 — attempt17 root fixes (2026-10-20)

## Scope
Cumulative over attempt16. Retains the attempt16 native-call argument packing change that classifies expressions statically and evaluates the argument once. This turn addresses canonical type reconstruction for local array declarations, side-effect-free escape marking, and Linear VM variable-index array-member decay.

## Source changes
- `src/ir_compile.c`: reconstruct complete fixed-size local array types from declaration metadata; canonical `Index` typing now uses the array element type rather than treating `PointeeType` as the expression result; unary dereference consults the reconstructed array type; pointer-element leaf classification remains pointer-valued.
- `src/ir_exec.c`: escape bookkeeping refuses to resolve/re-evaluate member paths rooted in calls, `va_arg`, assignments, increments/decrements, or anything not rooted in a local frame slot. This avoids second evaluation after native calls.
- `src/ir_eval.c`: native member-argument packing uses `CanonicalBase` when valid, retaining single evaluation.
- `src/ir_lin_emit.c`: variable outer index of a struct-array member decay is lowered and passed in instruction A.
- `src/ir_lin_exec.c`: variable-index member-array decay bypasses `IrSafeMemberAddr` (which would evaluate the index AST again) and supports global as well as local arrays; member offset lookup no longer requires `MV->Val` to be non-NULL.
- Added numbered tests 1066 and 1067; retained 1064 and 1065.

## Build and validation
- Clean GCC build: success, 0 warnings and 0 errors.
- Canonical type gate: all six configurations report `1051/1051`, `invalid_nodes=0`.
- Tree A0/A1: `1051/1051` both.
- Linear STRICT A0/A1: `1050/1051` both; remaining failure is `1066_args_canonical_types` due strict Tree fallback on the test body.
- Linear non-STRICT with `--require-no-tree-fallback` A0/A1: `1050/1051` both; `tree_body=1` in each due the same 1066 body. Test 1067 passes after the variable-index fix.
- Targeted tests 1064/1065 pass in Tree and Linear output semantics; 1066 passes Tree but currently is not supported fully by Linear STRICT; 1067 passes Tree and produces expected output in Linear non-STRICT after the runtime fix (matrix still flags only 1066).
- MSVC/LLP64 was not run in this environment.
- A/B performance matrix with seven alternating pairs was not completed in this turn; no performance conclusion is claimed.

## Status
**Not closed.** Root fixes resolve the five Tree regressions and the repeated-evaluation / variable-index defect, but attempt17 remains red because test 1066 exposes a Linear STRICT unsupported-body path. The failure is reported rather than masked. The separate struct brace-initializer issue described by the user remains out of scope.
