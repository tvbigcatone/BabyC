# Indexed metadata helper scan — continued (2026-10-17)

## Scope

This pass continues the scan of indexed-array compile metadata after the stride widening. It focuses on whether the common accessors are actually used by active Linear lowering consumers, rather than merely declared.

## Changes in this pass

- `src/ir_lin_emit.c`: replaced direct reads of `CompLocalNDims`, `CompLocalDims`, `CompLocalLeafEs`, and `CompLocalElemIsPtr` with `IrCompLocalNDimsOrFrame()`, `IrCompLocalDimOrFrame()`, `IrCompLocalLeafEsOrFrame()`, and `IrCompLocalElemIsPtrOrFrame()` throughout the emitter. This ensures compile-local metadata has a consistent live-frame fallback instead of each emitter branch implementing its own precedence.
- `src/ir_lin_ptr.c`: the dimension-copy path now uses the shared dimension accessors; direct array declarations are no longer used as a second metadata source.
- `src/ir_lower.c`: the indexed-metadata classification reads leaf element size through `IrCompLocalLeafEsOrFrame()` instead of directly peeking at the compile-local table.

## Validation

- Clean GCC rebuild: passed. Build log had no warning/error diagnostics.
- FP modes: 32/32 passed.
- Selected array/struct/pointer/bitfield corpus, Tree + Linear: 160/160 passed (80 test files per engine).
- Three-engine selected corpus (Tree/Linear/token): 235/240 passed. Five differences were token-mode caret/source-position mismatches in existing tests `244_array_elem_uninit`, `250_array_elem_incdec_uninit`, `265_char_array_partial`, `289_irb_struct_member_const_reject`, and `290_struct_member_const_reject`; messages/exit codes were diagnostic failures as expected, but caret columns differed from `.expect`. These are recorded, not altered in this pass.
- Test `961_pointee_stride_large_struct`: Linear strict returned 0; token returned 0. Tree strict reports `strict: tree fallback (body)`, as strict mode explicitly forbids Tree fallback; this is not a claim that Tree strict supports the case.
- The full `tools/run_suite.py src` run exceeded the 300-second tool limit; no full-suite pass is claimed.

## Remaining audit findings

1. `CompLocalFromType[]` is still read directly in many Linear emitter branches (roughly 70 references across source declarations and consumers). There is already `IrCompLocalFromTypeOrFrame()`. This is the next high-value helper connection, but it needs careful review because some sites compare compile-time type identity while others need a runtime fallback.
2. `LocalElemSize[]` remains read directly across memory/evaluation paths. Some are legitimate hot-path reads from the active frame, while others duplicate type/stride resolution. These must be classified by semantic role (array element stride, pointer pointee stride, leaf storage size) before replacing them; they are not interchangeable.
3. `IdxMeta*` is a separate per-instruction cache, not a duplicate of frame metadata. Its initialization/invalidating lifecycle still needs a write/read pairing audit.
4. `CompLocal*` table reset and nested-frame save/restore need an explicit lifecycle audit; helper use alone cannot prevent stale metadata leaking across reused slots.

## Static inventory snapshot

CompLocalPointeeSize: 4 textual references
  src/ir_storage.c:140: CompLocalPointeeSize[slot] = D->PointeeSize > 0 ? D->PointeeSize : 0;
  src/ir_lower.c:71: int CompLocalPointeeSize[IR_CALL_MAX_LOCALS];
  src/ir_storage.h:23: extern int CompLocalPointeeSize[IR_CALL_MAX_LOCALS];
  src/ir_storage.h:184: es = CompLocalPointeeSize[slot];
CompLocalNDims: 5 textual references
  src/ir_storage.c:132: CompLocalNDims[slot] = D->NDims;
  src/ir_lin_ptr.c:15: extern unsigned char CompLocalNDims[];
  src/ir_lower.c:68: unsigned char CompLocalNDims[IR_CALL_MAX_LOCALS];
  src/ir_storage.h:20: extern unsigned char CompLocalNDims[IR_CALL_MAX_LOCALS];
CompLocalDims: 5 textual references
  src/ir_storage.c:134: CompLocalDims[slot][di] = D->Dims[di];
  src/ir_lin_ptr.c:16: extern int CompLocalDims[][IR_MAX_ARRAY_DIMS];
  src/ir_lower.c:69: int CompLocalDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
  src/ir_storage.h:21: extern int CompLocalDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
CompLocalLeafEs: 5 textual references
  src/ir_storage.c:138: CompLocalLeafEs[slot] = D->ElemSize;
  src/ir_lin_ptr.c:17: extern int CompLocalLeafEs[];
  src/ir_lower.c:70: int CompLocalLeafEs[IR_CALL_MAX_LOCALS];
  src/ir_storage.h:22: extern int CompLocalLeafEs[IR_CALL_MAX_LOCALS];
CompLocalElemIsPtr: 5 textual references
  src/ir_storage.c:143: CompLocalElemIsPtr[slot] = 1;
  src/ir_lin_ptr.c:18: extern unsigned char CompLocalElemIsPtr[];
  src/ir_lower.c:72: unsigned char CompLocalElemIsPtr[IR_CALL_MAX_LOCALS];
  src/ir_storage.h:24: extern unsigned char CompLocalElemIsPtr[IR_CALL_MAX_LOCALS];
CompLocalFromType: 72 textual references
  src/ir_storage.c:146: CompLocalFromType[slot] = D->FromType;
  src/ir_lin_ptr.c:19: extern struct ValueType *CompLocalFromType[];
  src/ir_lower.c:73: struct ValueType *CompLocalFromType[IR_CALL_MAX_LOCALS];
  src/ir_storage.h:25: extern struct ValueType *CompLocalFromType[IR_CALL_MAX_LOCALS];
LocalPointeeSize: 8 textual references
  src/ir_storage.c:140: CompLocalPointeeSize[slot] = D->PointeeSize > 0 ? D->PointeeSize : 0;
  src/ir_runtime.h:85: unsigned short LocalPointeeSize[IR_CALL_MAX_LOCALS]; /* sizeof(*T), 0 if unknown/too large */
  src/ir_runtime.h:272: return (int)Fr->LocalPointeeSize[slot];
  src/ir_runtime.h:279: Fr->LocalPointeeSize[slot] = IrFramePointeeSizeValue(size);
LocalElemSize: 187 textual references
  src/ir_storage.c:214: Fr->LocalElemSize[slot] = es;
  src/ir_runtime.h:84: int LocalElemSize[IR_CALL_MAX_LOCALS];
  src/ir_lin_ptr.c:487: es_arr = Fr->LocalElemSize[ls] > 0 ? (int)Fr->LocalElemSize[ls] : 0;
  src/ir_lin_ptr.c:827: else if (Fr->LocalArraySize[si] > 0 && Fr->LocalElemSize[si] > 0)
LocalNDims: 38 textual references
  src/ir_storage.c:132: CompLocalNDims[slot] = D->NDims;
  src/ir_runtime.h:89: unsigned char LocalNDims[IR_CALL_MAX_LOCALS];
  src/ir_lin_ptr.c:15: extern unsigned char CompLocalNDims[];
  src/ir_lin_ptr.c:197: nd = (int)Fr->LocalNDims[Slot];
LocalDims: 39 textual references
  src/ir_storage.c:134: CompLocalDims[slot][di] = D->Dims[di];
  src/ir_runtime.h:90: int LocalDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
  src/ir_lin_ptr.c:16: extern int CompLocalDims[][IR_MAX_ARRAY_DIMS];
  src/ir_lin_ptr.c:199: dims[d] = Fr->LocalDims[Slot][d];
LocalElemIsPtr: 34 textual references
  src/ir_storage.c:143: CompLocalElemIsPtr[slot] = 1;
  src/ir_runtime.h:81: char LocalElemIsPtr[IR_CALL_MAX_LOCALS];
  src/ir_lin_ptr.c:18: extern unsigned char CompLocalElemIsPtr[];
  src/ir_lower.c:72: unsigned char CompLocalElemIsPtr[IR_CALL_MAX_LOCALS];
IdxMetaEs: 29 textual references
  src/ir_lin_tabs.h:39: extern int IdxMetaEs[32];
  src/ir_lin_ptr.c:927: es = IdxMetaEs[im] > 0 ? (int)IdxMetaEs[im] : 4;
  src/ir_lower.c:193: int IdxMetaEs[32];
  src/ir_lower.c:284: IdxMetaEs[i] = 0;
IdxMetaN: 18 textual references
  src/ir_lin_tabs.h:40: extern int IdxMetaN[32];
  src/ir_lower.c:194: int IdxMetaN[32];
  src/ir_lower.c:285: IdxMetaN[i] = 0;
  src/ir_lower.c:324: IdxMetaN[i] = Fr->LocalArraySize[B->LocalSlot];
IdxMetaDim: 0 textual references
