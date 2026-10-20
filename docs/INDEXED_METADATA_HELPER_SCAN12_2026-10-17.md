# Indexed metadata helper audit — SCAN12 (2026-10-17)

## Scope
Continued the audit of direct `CompLocalFromType[]` and `LocalElemSize[]` reads in `src/ir_lin_emit.c` and `src/ir_lower.c`, starting from SCAN11 overlay applied to `babyc(20261017-074837).zip`.

## Build
Applied the SCAN11 overlay to the available source archive and ran `make -j2` in `babyc/src` with GCC `-O2`. Build completed successfully; no compiler warnings were emitted in the captured output.

## Audit result
This scan intentionally makes no production-code substitutions. The remaining direct reads are not one semantic category and cannot safely be rewritten mechanically:

- `CompLocalFromType[slot]` is used for distinct type predicates and type-driven lowering (pointer arithmetic, aggregate width, FP handling, and structural compatibility). Some sites need the active compile template to take precedence; others are guarded by an existing `ST == NULL` fallback and have different semantics.
- `LocalElemSize[slot]` supplies runtime/frame metadata for leaf stride, aggregate copy width, pointer-cell discrimination, and inferred element-size propagation. These are not interchangeable: replacing them with `sizeof(void *)`, pointee size, or type `Sizeof` would conflate different quantities.
- `ir_lower.c` contains diagnostic/metadata tracing and array allocation-size logic; these must be evaluated separately from lowering-time type resolution.

Representative direct reads were located at `ir_lin_emit.c` lines 344, 1124–1126, 2490–2496, 2705–2706, 2919–2956, 4885–4915, 5120–5205, 5280–5284, 5560–5689, and 5914–5915. Line numbers refer to this scan's applied tree and may shift with later edits.

## Test status
The source build was verified. Focused behavioral tests and performance comparisons were not rerun in this audit-only scan, so SCAN11's reported focused results are not re-asserted here. The known Linear strict gap `454_complex_dispatch` remains open and must not be counted as passing.

## Decision
No production substitution was made because the inspected accesses have different semantic responsibilities and the evidence is insufficient to safely consolidate them in one change. Next change should target one well-bounded category, add/retain differential tests for pointer arithmetic and aggregate copies, then measure before/after performance. Do not claim zero cost or a performance gain without measurement.
