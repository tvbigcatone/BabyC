/* ir_storage.c — P0 unified storage descriptor helpers. */
#include "picoc.h"
#include "ir_storage.h"
#include "ir_fncode.h"
#include <stdint.h>
#include <string.h>

/* Defined in ir_lower.c; used only on cold Decl/frame stamp paths. */
void IrLiveMarkInit(IrCallFrame *Fr, int slot);

/* H11 canonical lookup: validate aggregate type and resolve member once. */
int IrResolveMemberLayout(struct ValueType *ST, const char *Name,
                          int *OffOut, int *EsOut, int *EbOut, int *IsPtrOut)
{
    struct Value *MV = NULL;
    if (ST == NULL || Name == NULL || ST->Members == NULL)
        return 0;
    if (ST->Base == TypePointer && ST->FromType != NULL)
        ST = ST->FromType;
    if (ST->Base != TypeStruct && ST->Base != TypeUnion)
        return 0;
    if (!TableGet(ST->Members, (char *)Name, &MV, NULL, NULL, NULL))
        return 0;
    return IrResolveMemberValueLayout(MV, OffOut, EsOut, EbOut, IsPtrOut);
}


void IrStorageDescClear(IrStorageDesc *D)
{
    if (D == NULL)
        return;
    memset(D, 0, sizeof(*D));
}

void IrStorageDescFromDecl(const IrStmt *S, IrStorageDesc *Out)
{
    int di;
    struct ValueType *FT;

    IrStorageDescClear(Out);
    if (S == NULL || S->Kind != IrStDecl)
        return;

    Out->IsPtr = S->DeclIsPtr ? 1 : 0;
    Out->IsFptr = S->DeclIsFptr ? 1 : 0;
    Out->ElemIsPtr = S->DeclElemIsPtr ? 1 : 0;
    Out->IsConst = S->DeclIsConst ? 1 : 0;
    Out->IsStatic = S->DeclIsStatic ? 1 : 0;
    Out->ArraySize = S->DeclArraySize;
    Out->ElemSize = S->DeclElemSize;
    Out->PointeeSize = S->DeclPointeeSize;

    if (S->DeclNDims > 0)
    {
        Out->NDims = (unsigned char)S->DeclNDims;
        for (di = 0; di < S->DeclNDims && di < IR_MAX_ARRAY_DIMS; di++)
            Out->Dims[di] = S->DeclDims[di];
    }
    else if (S->DeclArraySize > 0)
    {
        Out->NDims = 1;
        Out->Dims[0] = S->DeclArraySize;
    }

    FT = S->DeclFromType;
    if (FT != NULL && FT->Base == TypeArray && FT->FromType != NULL)
        FT = FT->FromType;
    Out->FromType = FT;

    /* Array of pointer / fptr: leaf is always a pointer word. */
    if ((S->DeclElemIsPtr || S->DeclIsFptr) && S->DeclArraySize > 0)
    {
        Out->ElemIsPtr = 1;
        if (Out->ElemSize <= 0)
            Out->ElemSize = (int)sizeof(void *);
    }
}

void IrStorageDescFromType(struct ValueType *T, int is_ptr, int is_fptr,
                           IrStorageDesc *Out)
{
    IrStorageDescClear(Out);
    Out->IsPtr = is_ptr ? 1 : 0;
    Out->IsFptr = is_fptr ? 1 : 0;
    Out->FromType = T;
    if (is_ptr && !is_fptr && T != NULL && T->Base == TypePointer
        && T->FromType != NULL)
        Out->PointeeSize = IrStorageDescElemSizeFromType(T->FromType);
}

void IrStorageDescSetLeafEs(IrStorageDesc *D, int es)
{
    if (D == NULL)
        return;
    if (es > 0)
        D->ElemSize = es;
}

int IrStorageDescElemSizeFromType(struct ValueType *FT)
{
    if (FT == NULL)
        return 0;
    /* int *p → pointee size, not pointer width. Array layers are metadata
     * about shape; the stride is the storage size of the leaf element. */
    if (FT->Base == TypePointer && FT->FromType != NULL)
        FT = FT->FromType;
    FT = IrTypePeelArray(FT);
    if (FT == NULL)
        return 0;
    if (FT->Base == TypeFP)
        return TypeFPStorageSize(FT);
    if (FT->Sizeof > 0)
        return FT->Sizeof;
    if (FT->Base == TypeChar || FT->Base == TypeUnsignedChar)
        return 1;
    if (FT->Base == TypeShort || FT->Base == TypeUnsignedShort)
        return 2;
    if (FT->Base == TypeLongLong || FT->Base == TypeUnsignedLongLong)
        return 8;
    return (int)sizeof(int);
}

void IrStorageDescStampCompLocal(int slot, const IrStorageDesc *D)
{
    int di;

    if (D == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;

    if (D->NDims > 0)
    {
        CompLocalNDims[slot] = D->NDims;
        for (di = 0; di < (int)D->NDims && di < IR_MAX_ARRAY_DIMS; di++)
            CompLocalDims[slot][di] = D->Dims[di];
    }

    if (D->ElemSize > 0)
        CompLocalLeafEs[slot] = D->ElemSize;

    CompLocalPointeeSize[slot] = D->PointeeSize > 0 ? D->PointeeSize : 0;

    if (D->ElemIsPtr)
        CompLocalElemIsPtr[slot] = 1;

    if (D->IsConst)
        CompLocalIsConst[slot] = 1;

    if (D->FromType != NULL)
        CompLocalFromType[slot] = D->FromType;
}

void IrStorageDescStampFnSlot(int slot, const IrStorageDesc *D, const char *name)
{
    if (CompileSlot == NULL || D == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;

    if (D->IsFptr)
        CompileSlot->SlotIsFptr[slot] = 1;
    else if (D->IsPtr)
        CompileSlot->SlotIsPtr[slot] = 1;

    if (D->FromType != NULL)
        CompileSlot->SlotFromType[slot] = D->FromType;

    if (name != NULL)
        CompileSlot->SlotName[slot] = name;
}

void IrStorageDescApplyFrame(IrCallFrame *Fr, int slot, const IrStorageDesc *D,
                             const char *name, int flags)
{
    struct ValueType *FT;
    int es;

    if (Fr == NULL || D == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;

    if ((flags & IR_STAMP_FRAME_SKIP_OWNED)
        && Fr->LocalOwned[slot] && Fr->LocalArraySize[slot] <= 0)
        return;

    if (flags & IR_STAMP_FRAME_PTR)
        Fr->LocalIsPtr[slot] = 1;
    if (flags & IR_STAMP_FRAME_FPTR)
        Fr->LocalIsFptr[slot] = 1;

    if ((flags & IR_STAMP_FRAME_NAME) && name != NULL)
        Fr->LocalName[slot] = (char *)name;

    if (flags & IR_STAMP_FRAME_INIT)
        IrLiveMarkInit(Fr, slot);

    if (D->PointeeSize > 0)
        IrFrameSetPointeeSize(Fr, slot, D->PointeeSize);

    if (flags & IR_STAMP_FRAME_TYPE)
    {
        FT = D->FromType;
        if (FT != NULL)
        {
            Fr->LocalFromType[slot] = FT;
            if (Fr->LocalTyp[slot] == NULL)
                Fr->LocalTyp[slot] = FT;
        }
    }

    if (flags & IR_STAMP_FRAME_ELEMSIZE)
    {
        /* Prefer explicit PointeeSize / ElemSize from descriptor when set. */
        if (D->PointeeSize > 0)
            es = D->PointeeSize;
        else if (D->ElemSize > 0 && !D->IsPtr)
            es = D->ElemSize;
        else
            es = IrStorageDescElemSizeFromType(D->FromType);
        if (es > 0)
            Fr->LocalElemSize[slot] = es;
    }
}

void IrStorageDescApplyParamMeta(IrCallFrame *Fr, int pi, struct ValueType *PT,
                                 const char *name, int is_ptr)
{
    if (Fr == NULL || pi < 0 || pi >= IR_CALL_MAX_PARAMS)
        return;
    Fr->ParamIsPtr[pi] = is_ptr ? 1 : 0;
    if (PT != NULL)
        Fr->ParamTyp[pi] = PT;
    if (name != NULL)
        Fr->ParamName[pi] = (char *)name;
}

int IrStorageDescParamIsPtrType(struct ValueType *PT)
{
    if (PT == NULL)
        return 0;
    return (PT->Base == TypePointer || PT->Base == TypeArray
            || PT->Base == TypeStruct || PT->Base == TypeUnion) ? 1 : 0;
}

void IrStorageDescApplySlotKind(IrCallFrame *Fr, int slot, unsigned char kind)
{
    IrStorageDesc D;
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;
    if (kind == IR_FRAME_SLOT_PTR)
    {
        IrStorageDescFromType(NULL, 1, 0, &D);
        IrStorageDescApplyFrame(Fr, slot, &D, NULL, IR_STAMP_FRAME_PTR);
    }
    /* IR_FRAME_SLOT_FP: LocalVal bitcast when written — no frame flag yet */
}

void IrStorageDescBindParamArg(IrCallFrame *Fr, int pi, struct ValueType *PT,
                               const char *name, long long arg_bits)
{
    int is_ptr;
    if (Fr == NULL || pi < 0 || pi >= IR_CALL_MAX_PARAMS)
        return;
    is_ptr = IrStorageDescParamIsPtrType(PT);
    /* Heuristic: non-zero bits with unknown type often means pointer payload */
    if (!is_ptr && arg_bits != 0 && PT == NULL)
        is_ptr = 1;
    IrStorageDescApplyParamMeta(Fr, pi, PT, name, is_ptr);
    Fr->ParamVal[pi] = arg_bits;
}

void IrStorageDescStampDecl(int slot, const IrStmt *S)
{
    IrStorageDesc D;

    if (S == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;
    IrStorageDescFromDecl(S, &D);
    IrStorageDescStampCompLocal(slot, &D);
    IrStorageDescStampFnSlot(slot, &D, S->DeclIdent);
}

void IrStorageDescStampDeclEs(int slot, const IrStmt *S, int leaf_es)
{
    IrStorageDesc D;

    if (S == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;
    IrStorageDescFromDecl(S, &D);
    IrStorageDescSetLeafEs(&D, leaf_es);
    /* Array path without DeclNDims: ensure 1D stamp from ArraySize. */
    if (D.NDims == 0 && S->DeclArraySize > 0)
    {
        D.NDims = 1;
        D.Dims[0] = S->DeclArraySize;
    }
    IrStorageDescStampCompLocal(slot, &D);
    IrStorageDescStampFnSlot(slot, &D, S->DeclIdent);
}

void IrStorageDescStampDeclFull(int slot, const IrStmt *S, IrCallFrame *Fr,
                                int frame_flags)
{
    IrStorageDesc D;

    if (S == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;
    IrStorageDescFromDecl(S, &D);
    IrStorageDescStampCompLocal(slot, &D);
    IrStorageDescStampFnSlot(slot, &D, S->DeclIdent);
    if (Fr != NULL)
        IrStorageDescApplyFrame(Fr, slot, &D, S->DeclIdent, frame_flags);
}

void IrStorageDescSeedFrameFromFnSlot(IrCallFrame *Fr, void *CSv)
{
    FnCodeSlot *CS = (FnCodeSlot *)CSv;
    int si;
    if (Fr == NULL || CS == NULL)
        return;
    {
        int n = CS->MetaLocalN;
        if (n < 0) n = 0;
        if (n > IR_CALL_MAX_LOCALS) n = IR_CALL_MAX_LOCALS;
        for (si = 0; si < n; si++)
        {
        int flags;
        IrStorageDesc D;
        if (!CS->SlotIsPtr[si] && !CS->SlotIsFptr[si] && CS->SlotFromType[si] == NULL)
            continue;
        IrStorageDescClear(&D);
        D.IsPtr = CS->SlotIsPtr[si] ? 1 : 0;
        D.IsFptr = CS->SlotIsFptr[si] ? 1 : 0;
        D.FromType = CS->SlotFromType[si];
        flags = 0;
        if (D.IsPtr)
            flags |= IR_STAMP_FRAME_PTR;
        if (D.IsFptr)
            flags |= IR_STAMP_FRAME_FPTR;
        if (D.FromType != NULL)
            flags |= IR_STAMP_FRAME_TYPE;
        if (flags != 0)
            IrStorageDescApplyFrame(Fr, si, &D, CS->SlotName[si],
                flags | (CS->SlotName[si] ? IR_STAMP_FRAME_NAME : 0));
        else if (CS->SlotFromType[si] != NULL)
        {
            Fr->LocalFromType[si] = CS->SlotFromType[si];
            Fr->LocalTyp[si] = CS->SlotFromType[si];
        }
        }
    }
}

/* C8 — shared owned-blob alloc for DECL_STRUCT / re-decl (tree + lin). */
void *IrAllocOwnedBlob(IrCallFrame *Fr, int slot, size_t nbytes)
{
    void *mem;
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return NULL;
    if (nbytes == 0)
        nbytes = 1;
    if (Fr->LocalOwned[slot] && Fr->LocalVal[slot] != 0)
    {
        free((void *)(uintptr_t)Fr->LocalVal[slot]);
        IrFrameSetLocalOwned(Fr, slot, 0);
        Fr->LocalVal[slot] = 0;
    }
    mem = calloc(1, nbytes);
    if (mem == NULL)
        return NULL;
    Fr->LocalVal[slot] = (long long)(uintptr_t)mem;
    IrFrameSetLocalOwned(Fr, slot, 1);
    return mem;
}

