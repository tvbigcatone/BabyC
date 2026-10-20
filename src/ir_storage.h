/* ir_storage.h — P0 unified storage descriptor (compile-time + frame layout).
 *
 * Goal: one structured view of "what is this local/param slot" so
 * CompLocal*, FnCodeSlot meta, and IrCallFrame stamping share a single
 * fill path. Existing parallel arrays remain as dual-write sinks until
 * call sites are fully migrated.
 *
 * PERF: helpers are cold (Decl lower / FnCode compile / body entry).
 * Hot paths keep direct CompLocal* / Frame field access.
 */
#ifndef BABYC_IR_STORAGE_H
#define BABYC_IR_STORAGE_H

#include "interpreter.h"
#include "ir.h"
#include "ir_runtime.h"

/* Compile-time multidim / type stamps (defined in ir_lower.c).
 * Declared here so ir_storage and lower sites share one contract. */
extern unsigned char CompLocalNDims[IR_CALL_MAX_LOCALS];
extern int CompLocalDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
extern int CompLocalLeafEs[IR_CALL_MAX_LOCALS];
extern int CompLocalPointeeSize[IR_CALL_MAX_LOCALS];
extern unsigned char CompLocalElemIsPtr[IR_CALL_MAX_LOCALS];
extern unsigned char CompLocalIsConst[IR_CALL_MAX_LOCALS];
extern struct ValueType *CompLocalFromType[IR_CALL_MAX_LOCALS];

/* Offset-only access for callers that already have the member Value and do
 * not need type/bitfield metadata. This keeps the common runtime path minimal. */
static inline int IrResolveMemberOffsetValue(struct Value *MV, int *OffOut)
{
    if (MV == NULL || MV->Val == NULL)
        return 0;
    if (OffOut) *OffOut = MV->Val->Integer;
    return 1;
}

/* Shared metadata decoder. Keep inline: it is used by member accesses in
 * runtime loops, where an out-of-line call per field would be measurable. */
static inline int IrResolveMemberValueLayout(struct Value *MV, int *OffOut,
                                              int *EsOut, int *EbOut,
                                              int *IsPtrOut)
{
    int base;
    if (!IrResolveMemberOffsetValue(MV, OffOut))
        return 0;
    if (IsPtrOut)
        *IsPtrOut = (MV->Typ != NULL && MV->Typ->Base == TypePointer) ? 1 : 0;
    if (MV->Typ != NULL)
    {
        base = MV->Typ->Base;
        if (base == TypeChar || base == TypeUnsignedChar)
        { if (EsOut) *EsOut = 1; if (EbOut) *EbOut = base; }
        else if (base == TypeShort || base == TypeUnsignedShort)
        { if (EsOut) *EsOut = 2; if (EbOut) *EbOut = base; }
        else if (base == TypePointer)
        { if (EsOut) *EsOut = (int)sizeof(void *); if (EbOut) *EbOut = base; }
        else if (base == TypeLongLong || base == TypeUnsignedLongLong
                 || base == TypeLong || base == TypeUnsignedLong)
        { if (EsOut) *EsOut = (int)sizeof(long long); if (EbOut) *EbOut = base; }
        else if (base == TypeFP)
        { if (EsOut) *EsOut = TypeFPStorageSize(MV->Typ); if (EbOut) *EbOut = TypeFP; }
        else if (base == TypeStruct || base == TypeUnion)
        {
            if (EsOut)
                *EsOut = (MV->Typ->Sizeof > 0) ? (int)MV->Typ->Sizeof : 4;
            if (EbOut) *EbOut = base;
        }
        else
        { if (EsOut) *EsOut = 4; if (EbOut) *EbOut = base; }
    }
    else
    { if (EsOut) *EsOut = 4; if (EbOut) *EbOut = TypeInt; }
    return 1;
}

typedef struct IrStorageDesc {
    unsigned char IsPtr;       /* slot value is a pointer */
    unsigned char IsFptr;      /* function pointer */
    unsigned char ElemIsPtr;   /* array-of-pointer (or fptr array) */
    unsigned char IsConst;
    unsigned char IsStatic;
    unsigned char NDims;       /* 0 = scalar / unset */
    int ArraySize;             /* DeclArraySize (0 scalar, >0 array) */
    int Dims[IR_MAX_ARRAY_DIMS];
    int ElemSize;              /* leaf element bytes (CompLocalLeafEs) */
    int PointeeSize;           /* sizeof(*T) when IsPtr */
    struct ValueType *FromType;/* element or pointee type when known */
} IrStorageDesc;

/* Flags for IrStorageDescApplyFrame */
#define IR_STAMP_FRAME_PTR        0x01  /* LocalIsPtr = 1 */
#define IR_STAMP_FRAME_FPTR       0x02  /* LocalIsFptr = 1 */
#define IR_STAMP_FRAME_INIT       0x04  /* IrLiveMarkInit */
#define IR_STAMP_FRAME_SKIP_OWNED 0x08  /* skip if owned value-blob slot */
#define IR_STAMP_FRAME_ELEMSIZE   0x10  /* peel pointee → LocalElemSize */
#define IR_STAMP_FRAME_NAME       0x20  /* set LocalName from name arg */
#define IR_STAMP_FRAME_TYPE       0x40  /* set LocalFromType / LocalTyp */

void IrStorageDescClear(IrStorageDesc *D);

/* Fill from IrStDecl fields. Does not touch CompLocal* / Frame. */
void IrStorageDescFromDecl(const IrStmt *S, IrStorageDesc *Out);

/* Minimal fill from a ValueType (assign-to-ptr, param meta). */
void IrStorageDescFromType(struct ValueType *T, int is_ptr, int is_fptr,
                           IrStorageDesc *Out);

/* Optional override of leaf elem size after resolution (array path). */
void IrStorageDescSetLeafEs(IrStorageDesc *D, int es);

/* Resolve pointee/element byte size from a ValueType (pointer peel). */
int IrStorageDescElemSizeFromType(struct ValueType *FT);

/* Formal/param: pointer-like formal types (ptr, array, struct, union). */
int IrStorageDescParamIsPtrType(struct ValueType *PT);

/* Dual-write sinks (slot must be in range). */
void IrStorageDescStampCompLocal(int slot, const IrStorageDesc *D);
/* Writes CompileSlot->SlotIsPtr/IsFptr/FromType/Name when CompileSlot set. */
void IrStorageDescStampFnSlot(int slot, const IrStorageDesc *D, const char *name);

/* Runtime frame stamp (LocalIsPtr/Fptr/FromType/Typ/ElemSize/Name). */
void IrStorageDescApplyFrame(IrCallFrame *Fr, int slot, const IrStorageDesc *D,
                             const char *name, int flags);

/* Seed LocalIsPtr from IrFuncMeta SlotKind (template path). */
void IrStorageDescApplySlotKind(IrCallFrame *Fr, int slot, unsigned char kind);

/* Param metadata only (ParamIsPtr / ParamTyp / ParamName). Value packing stays
 * at the call site — this unifies the meta write after is_ptr is decided. */
void IrStorageDescApplyParamMeta(IrCallFrame *Fr, int pi, struct ValueType *PT,
                                 const char *name, int is_ptr);

/* ArgVals-style bind: name + type + is_ptr from PT (+ heuristic if PT NULL). */
void IrStorageDescBindParamArg(IrCallFrame *Fr, int pi, struct ValueType *PT,
                               const char *name, long long arg_bits);

/* Convenience: FromDecl + StampCompLocal (+ FnSlot FromType). */
void IrStorageDescStampDecl(int slot, const IrStmt *S);
void IrStorageDescStampDeclEs(int slot, const IrStmt *S, int leaf_es);

/* FromDecl + CompLocal + FnSlot + optional Frame (when Fr non-NULL). */
void IrStorageDescStampDeclFull(int slot, const IrStmt *S, IrCallFrame *Fr,
                                int frame_flags);

/* Seed LocalIsPtr/Fptr/FromType from compiled FnCodeSlot (nested Call).
 * GUARD: use this instead of hand-written SlotIsPtr loops. */
/* CS is FnCodeSlot* (ir_fncode.h). void* avoids header cycles. */
void IrStorageDescSeedFrameFromFnSlot(IrCallFrame *Fr, void *CS);


/* --- P0.25 dual-read accessors (zero-cost static inline) ---
 * Prefer CompLocal* (function under compile), then live Frame.
 * Order matches historical emit/lower sites — do not invert.
 * GUARD: use these instead of hand-rolled CompLocal+Frame pairs.
 */
static inline struct ValueType *IrCompLocalFromTypeOrFrame(int slot)
{
    struct ValueType *T;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return NULL;
    T = CompLocalFromType[slot];
    if (T != NULL)
        return T;
    if (IrCallDepth > 0)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        if (Fr->LocalFromType[slot] != NULL)
            return Fr->LocalFromType[slot];
        if (Fr->LocalTyp[slot] != NULL)
            return Fr->LocalTyp[slot];
    }
    return NULL;
}

/* Pointee stride has both a compile-time sink and a live-frame sink.
 * Compile metadata wins during lowering; the frame is consulted when the
 * current function did not stamp a compile-time value. */
static inline int IrCompLocalPointeeSizeOrFrame(int slot)
{
    int es;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return 0;
    es = CompLocalPointeeSize[slot];
    if (es > 0)
        return es;
    if (IrCallDepth > 0)
    {
        es = IrFrameGetPointeeSize(IrCallStack[IrCallDepth - 1], slot);
        if (es > 0)
            return es;
    }
    return 0;
}

static inline int IrCompLocalLeafEsOrFrame(int slot)
{
    int es;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return 0;
    es = CompLocalLeafEs[slot];
    if (es > 0)
        return es;
    if (IrCallDepth > 0)
    {
        es = IrCallStack[IrCallDepth - 1]->LocalElemSize[slot];
        if (es > 0)
            return es;
    }
    return 0;
}

static inline int IrCompLocalNDimsOrFrame(int slot)
{
    int nd;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return 0;
    nd = (int)CompLocalNDims[slot];
    if (nd > 0)
        return nd;
    if (IrCallDepth > 0)
        return (int)IrCallStack[IrCallDepth - 1]->LocalNDims[slot];
    return 0;
}

/* Read one dimension with the same compile-local-first policy. The frame is
 * consulted only when compile metadata has no positive dimension. */
static inline int IrCompLocalDimOrFrame(int slot, int dim)
{
    int n;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS
        || dim < 0 || dim >= IR_MAX_ARRAY_DIMS)
        return 0;
    n = CompLocalDims[slot][dim];
    if (n > 0)
        return n;
    if (IrCallDepth > 0)
        return IrCallStack[IrCallDepth - 1]->LocalDims[slot][dim];
    return 0;
}

/* Array-of-pointer classification also has compile and live-frame sinks. */
static inline int IrCompLocalElemIsPtrOrFrame(int slot)
{
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return 0;
    if (CompLocalElemIsPtr[slot])
        return 1;
    if (IrCallDepth > 0)
        return IrCallStack[IrCallDepth - 1]->LocalElemIsPtr[slot] ? 1 : 0;
    return 0;
}

#endif /* BABYC_IR_STORAGE_H */
