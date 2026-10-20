# Diagnostic collector location discrepancy — root cause and fix (2026-10-17)

## Finding

The caret mismatch in `952_tree_pointer_param_read_uninit` and
`953_tree_pointer_alias_partial_init` did **not** bypass the common diagnostic
collector. Both failures entered `ProgramFail()` and then
`ProgramFailAtV()` → `DiagnosticReportV()` → `DiagnosticRenderLocation()`.
The collector rendered the location it was given; Linear supplied a stale
column (the `p` parameter at column 21).

The root cause was in strict Linear lowering for direct pointer-parameter
index reads (`p[i]`). The emitter generated `IrLinLoadIndex` with `Tok == 2`,
where `Imm` held only the element size and `B` held the parameter slot used by
`LinTok2Unsigned()`. This instruction form discarded the AST/source location.
At runtime the uninitialized-access check therefore had no `IrExpr` to recover
the failing index expression from and fell back to the local/parameter name.

## Fix

For this strict `p[i]` form only, the emitter now packs source line and the
parser's expression column into the high bits of `Imm`, retaining the element
size in the low nibble and preserving `B` unchanged for signedness. The AST
records the position immediately after the closing bracket; the runtime adds
one column to match the established Tree diagnostic position. Normal direct
pointer loads still use `Imm = 1/2/4/8` and are unchanged.

The Linear executor restores the packed source line/column only on error paths
(null pointer and uninitialized read), before calling `ProgramFail()`. The VM
preflight and production direct-load path decode the width nibble as well, so
the packed metadata is not mistaken for a huge element size. No new execution
engine or fallback path was introduced, and no renderer heuristic was added.

## Verification

- GCC build: succeeded.
- `tools/check_diagnostic_catalog.py`: passed (10 canonical message literals
  remain confined to `src/diagnostic.c`).
- Exact diagnostics for tests 1038 and 1039 now match across Tree, Linear strict,
  `BABYC_EXEC=vm_strict`, `BABYC_EXEC=vm`, token-only, and `BABYC_NO_IR=1`: same
  source line, caret column, message, and exit code 1.
- Linear strict targeted regression set: 18/18 passed, including the positive
  pointer-store test 1037 and pointer-index/type/width/recursion cases 782,
  783, 784, 787, 901, 924, 946, 948, and 983.
- The complete `tools/run_suite.py` Linear-strict run was attempted before the
  final source-location encoding change and exceeded the 180-second execution
  limit. It was not rerun after the final patch; no full-suite pass is claimed.
- No before/after performance benchmark was run. The additional source-location
  packing is limited to strict `p[i]` loads with known source coordinates; the
  runtime decodes it only in the existing direct-load/error paths.

## Architectural conclusion

A common collector guarantees consistent rendering only when callers supply
correct semantic messages and source coordinates. The right contract is:
engine-specific detection, explicit source location, then common reporting.
A stale or absent source location cannot be repaired reliably by the collector
because it cannot know which access in the source expression caused the error.
