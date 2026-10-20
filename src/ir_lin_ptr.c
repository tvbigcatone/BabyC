/* ir_lin_ptr.c — pointer/member/index lower helpers (extract from ir_lower.c) */
#include "picoc.h"
#include "ir_lin_ptr.h"
#include "ir_lin_light.h"
#include "ir_lin_tabs.h"
#include "ir_fncode.h"
#include "ir_runtime.h"
#include "ir_storage.h"
#include "ir_lin.h"
#include <string.h>
#include <stdint.h>

extern struct ParseState *LinParser;
extern int LinCompileActive;
extern unsigned char CompLocalNDims[];
extern int CompLocalDims[][IR_MAX_ARRAY_DIMS];
extern int CompLocalLeafEs[];
extern unsigned char CompLocalElemIsPtr[];
extern struct ValueType *CompLocalFromType[];
int AllocReg(int *Next);
int Emit(IrLinInst *Code, int Cap, int *N, IrLinInst In);
int LowerRec(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next, int *Dest);
int IrVmStrictOn(void);
extern unsigned char LinRegU[IR_LIN_REGS];
int GlobPush(IrExpr *E);

struct ValueType *IrLowerCallReturnStruct(IrExpr *Call)
{
    struct ValueType *RT = NULL;
    struct Value *CF;
    if (Call == NULL || Call->Kind != IrExCall)
        return NULL;
    CF = Call->CachedFunc;
    if (CF == NULL && Call->Left != NULL)
        CF = Call->Left->CachedFunc;
    if (CF != NULL && CF->Val != NULL)
        RT = CF->Val->FuncDef.ReturnType;
    if (RT == NULL && LinParser != NULL && LinParser->pc != NULL)
    {
        const char *Name = NULL;
        struct Value *GV = NULL;
        if (Call->Left != NULL && Call->Left->Kind == IrExIdent)
            Name = Call->Left->Ident;
        else if (Call->Ident != NULL)
            Name = Call->Ident;
        if (Name != NULL && VariableDefined(LinParser->pc, (char *)Name)
            && TableGet(&LinParser->pc->GlobalTable, (char *)Name, &GV, NULL, NULL, NULL)
            && GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeFunction
            && GV->Val != NULL)
            RT = GV->Val->FuncDef.ReturnType;
    }
    if (RT == NULL)
        return NULL;
    if (RT->Base == TypePointer && RT->FromType != NULL)
        return RT->FromType;
    return RT;
}


struct ValueType *IrLowerCallReturnType(IrExpr *Call)
{
    struct ValueType *RT = NULL;
    struct Value *CF;
    if (Call == NULL || Call->Kind != IrExCall)
        return NULL;
    CF = Call->CachedFunc;
    if (CF == NULL && Call->Left != NULL)
        CF = Call->Left->CachedFunc;
    if (CF != NULL && CF->Val != NULL)
        RT = CF->Val->FuncDef.ReturnType;
    if (RT == NULL && LinParser != NULL && LinParser->pc != NULL)
    {
        const char *Name = NULL;
        struct Value *GV = NULL;
        if (Call->Left != NULL && Call->Left->Kind == IrExIdent)
            Name = Call->Left->Ident;
        else if (Call->Ident != NULL)
            Name = Call->Ident;
        if (Name != NULL && VariableDefined(LinParser->pc, (char *)Name)
            && TableGet(&LinParser->pc->GlobalTable, (char *)Name, &GV, NULL, NULL, NULL)
            && GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeFunction
            && GV->Val != NULL)
            RT = GV->Val->FuncDef.ReturnType;
    }
    return RT;
}



int IrLowerMemberOffEs(struct ValueType *ST, const char *Name,
                              int *OffOut, int *EsOut, int *EbOut)
{
    /* GUARD: thin wrapper → IrResolveMemberLayout */
    return IrResolveMemberLayout(ST, Name, OffOut, EsOut, EbOut, NULL);
}


/* Row = a[i] where each element of a is a pointer (local T *a[N] stamped
 * from its Decl; global T *g[N] / T **g by declared type). Returns T, the
 * pointee type of a[i][j], or NULL when the declaration does not prove it.
 * Shared by the a[i][j] load and store lowerings. */
struct ValueType *IrLowerPtrRowPointee(IrExpr *Row)
{
    IrExpr *B;
    struct ValueType *T;
    if (Row == NULL || Row->Kind != IrExIndex || Row->Right == NULL)
        return NULL;
    if (Row->PointeeType != NULL)
        return Row->PointeeType; /* local T *a[N], stamped at annotate */
    B = Row->Left;
    if (B == NULL || B->Kind != IrExIdent || B->LocalSlot >= 0
        || B->ParamSlot >= 0 || B->Ident == NULL || LinParser == NULL)
        return NULL;
    {
        struct Value *GV = IrGlobalValSoft(LinParser, B);
        T = (GV != NULL) ? GV->Typ : NULL;
    }
    if (T == NULL || (T->Base != TypeArray && T->Base != TypePointer)
        || T->FromType == NULL || T->FromType->Base != TypePointer)
        return NULL;
    return T->FromType->FromType;
}

int IrLowerIndexRowIsPointer(IrExpr *Row)
{
    IrExpr *Root = Row;
    struct ValueType *T = NULL;
    int nd = 0;
    if (Row == NULL || Row->Kind != IrExIndex)
        return 0;
    while (Root != NULL && Root->Kind == IrExIndex)
        Root = Root->Left;
    if (Root == NULL || Root->Kind != IrExIdent)
        return 0;
    /* Multi-dim T *a[N][M] is a flat blob of pointer words — flatten, not aop. */
    if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        /* GUARD: dual-read NDims */
        nd = IrCompLocalNDimsOrFrame(Root->LocalSlot);
        if (nd >= 2)
            return 0;
    }
    if (Row->TypeFlags & IR_TF_PTR)
        return 1;
    if (IrLowerPtrRowPointee(Row) != NULL)
        return 1;
    /* Locals first: a local also carries Ident, and the global lookup used
     * to shadow the local's own type (never reached for locals). */
    if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        /* GUARD: dual-read FromType */
        T = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
    }
    else if (Root->Ident != NULL && LinParser != NULL)
    {
        struct Value *GV = IrGlobalValSoft(LinParser, Root);
        if (GV != NULL) T = GV->Typ;
    }
    if (T != NULL && T->Base == TypeArray && T->FromType != NULL)
    {
        if (T->FromType->Base == TypeArray)
            return 0;
        return T->FromType->Base == TypePointer;
    }
    return 0;
}


int IrLowerInferAggregateSize(struct ValueType *T)
{
    int i, end = 0, align = 1;
    if (T == NULL || (T->Base != TypeStruct && T->Base != TypeUnion)
        || T->Members == NULL || T->Members->HashTable == NULL || T->Members->Size <= 0)
        return 0;
    for (i = 0; i < T->Members->Size; ++i)
    {
        struct TableEntry *E;
        for (E = T->Members->HashTable[i]; E != NULL; E = E->Next)
        {
            struct Value *M = E->p.v.Val;
            int off, sz, al, finish;
            if (M == NULL || M->Val == NULL || M->Typ == NULL) continue;
            if (!IrResolveMemberOffsetValue(M, &off)) continue;
            sz = M->Typ->Base == TypeFP ? TypeFPStorageSize(M->Typ) : M->Typ->Sizeof;
            if (sz <= 0 && (M->Typ->Base == TypeStruct || M->Typ->Base == TypeUnion))
                sz = IrLowerInferAggregateSize(M->Typ);
            if (M->BitWidth > 0 && sz <= 0) sz = (int)sizeof(int);
            if (off < 0 || sz <= 0) continue;
            finish = T->Base == TypeUnion ? sz : off + sz;
            if (finish > end) end = finish;
            al = (M->Typ->Base == TypeFP ? TypeFPStorageAlign(M->Typ) : M->Typ->AlignBytes) > 0
                ? (M->Typ->Base == TypeFP ? TypeFPStorageAlign(M->Typ) : M->Typ->AlignBytes) : sz;
            if (al > align && al <= 16) align = al;
        }
    }
    if (end <= 0) return 0;
    return align > 1 ? ((end + align - 1) / align) * align : end;
}


int LowerLocalStructArrayAddress(IrExpr *E, int Slot, int ElemBytes,
                                        IrLinInst *Code, int Cap, int *N, int *Next, int *Addr)
{
    IrExpr *Chain[IR_MAX_ARRAY_DIMS];
    IrExpr *Root = E;
    int dims[IR_MAX_ARRAY_DIMS] = {0};
    int idx[IR_MAX_ARRAY_DIMS];
    int nc = 0, nd = 0, d, flat, base, scale, off;
    IrLinInst In;
    while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
    {
        Chain[nc++] = Root;
        Root = Root->Left;
    }
    if (Root == NULL || Root->Kind != IrExIdent || Root->LocalSlot != Slot
        || nc <= 0 || ElemBytes <= 4)
        return 0;
    for (d = 0; d < nc / 2; ++d)
    {
        IrExpr *tmp = Chain[d];
        Chain[d] = Chain[nc - 1 - d];
        Chain[nc - 1 - d] = tmp;
    }
    if (IrCallDepth > 0)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        nd = (int)Fr->LocalNDims[Slot];
        for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; ++d)
            dims[d] = Fr->LocalDims[Slot][d];
    }
    if (nd < nc && IrCompLocalNDimsOrFrame(Slot) >= nc)
    {
        nd = (int)IrCompLocalNDimsOrFrame(Slot);
        for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; ++d)
            dims[d] = IrCompLocalDimOrFrame(Slot, d);
    }
    if (nc > 1)
    {
        if (nd != nc) return 0;
        for (d = 1; d < nd; ++d) if (dims[d] <= 0) return 0;
    }
    for (d = 0; d < nc; ++d)
        if (!LowerRec(Chain[d]->Right, Code, Cap, N, Next, &idx[d])) return 0;
    flat = idx[0];
    for (d = 1; d < nc; ++d)
    {
        int dimreg = AllocReg(Next), mul = AllocReg(Next), sum = AllocReg(Next);
        if (dimreg < 0 || mul < 0 || sum < 0) return 0;
        memset(&In, 0, sizeof(In));
        In.Op = IrLinImm; In.Dest = (unsigned char)dimreg; In.Imm = dims[d];
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)mul;
        In.A = (unsigned char)flat; In.B = (unsigned char)dimreg; In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)sum;
        In.A = (unsigned char)mul; In.B = (unsigned char)idx[d]; In.Tok = TokenPlus; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        flat = sum;
    }
    base = AllocReg(Next); scale = AllocReg(Next); off = AllocReg(Next);
    if (base < 0 || scale < 0 || off < 0) return 0;
    memset(&In, 0, sizeof(In));
    In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base; In.Imm = Slot;
    if (!Emit(Code, Cap, N, In)) return 0;
    In.Op = IrLinImm; In.Dest = (unsigned char)scale; In.Imm = ElemBytes;
    if (!Emit(Code, Cap, N, In)) return 0;
    In.Op = IrLinBin; In.Dest = (unsigned char)off;
    In.A = (unsigned char)flat; In.B = (unsigned char)scale; In.Tok = TokenAsterisk; In.Imm = 0;
    if (!Emit(Code, Cap, N, In)) return 0;
    *Addr = AllocReg(Next);
    if (*Addr < 0) return 0;
    In.Op = IrLinBin; In.Dest = (unsigned char)*Addr;
    In.A = (unsigned char)base; In.B = (unsigned char)off; In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
    return Emit(Code, Cap, N, In);
}


int LowerGlobalStructArrayAddress(IrExpr *E, IrExpr *Root, int ElemBytes,
                                         IrLinInst *Code, int Cap, int *N, int *Next, int *Addr)
{
    IrExpr *Chain[IR_MAX_ARRAY_DIMS];
    struct Value *GV;
    struct ValueType *T;
    int dims[IR_MAX_ARRAY_DIMS] = {0};
    int idx[IR_MAX_ARRAY_DIMS];
    int nc = 0, nd = 0, d, flat, base, scale, off, gi;
    IrLinInst In;
    IrExpr *Walk = E;
    while (Walk != NULL && Walk->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
    {
        Chain[nc++] = Walk;
        Walk = Walk->Left;
    }
    if (Root == NULL || Root->Kind != IrExIdent || nc <= 0
        || Walk != Root || ElemBytes <= 4 || LinParser == NULL)
        return 0;
    for (d = 0; d < nc / 2; ++d)
    {
        IrExpr *tmp = Chain[d]; Chain[d] = Chain[nc - 1 - d]; Chain[nc - 1 - d] = tmp;
    }
    GV = IrGlobalValSoft(LinParser, Root);
    if (GV == NULL || GV->Typ == NULL) return 0;
    T = GV->Typ;
    while (T != NULL && T->Base == TypeArray && nd < IR_MAX_ARRAY_DIMS)
    {
        dims[nd++] = T->ArraySize;
        T = T->FromType;
    }
    if (nd != nc || T == NULL
        || (T->Base != TypeStruct && T->Base != TypeUnion)
        || T->Sizeof != (unsigned long)ElemBytes)
        return 0;
    for (d = 0; d < nc; ++d)
        if (dims[d] <= 0 || !LowerRec(Chain[d]->Right, Code, Cap, N, Next, &idx[d]))
            return 0;
    flat = idx[0];
    for (d = 1; d < nc; ++d)
    {
        int dimreg = AllocReg(Next), mul = AllocReg(Next), sum = AllocReg(Next);
        if (dimreg < 0 || mul < 0 || sum < 0) return 0;
        memset(&In, 0, sizeof(In));
        In.Op = IrLinImm; In.Dest = (unsigned char)dimreg; In.Imm = dims[d];
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)mul;
        In.A = (unsigned char)flat; In.B = (unsigned char)dimreg; In.Tok = TokenAsterisk;
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)sum;
        In.A = (unsigned char)mul; In.B = (unsigned char)idx[d]; In.Tok = TokenPlus;
        if (!Emit(Code, Cap, N, In)) return 0;
        flat = sum;
    }
    gi = GlobPush(Root);
    if (gi < 0) return 0;
    base = AllocReg(Next); scale = AllocReg(Next); off = AllocReg(Next);
    if (base < 0 || scale < 0 || off < 0) return 0;
    memset(&In, 0, sizeof(In));
    In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base;
    In.Tok = 1; In.Imm = gi;
    if (!Emit(Code, Cap, N, In)) return 0;
    In.Op = IrLinImm; In.Dest = (unsigned char)scale; In.Imm = ElemBytes;
    if (!Emit(Code, Cap, N, In)) return 0;
    In.Op = IrLinBin; In.Dest = (unsigned char)off;
    In.A = (unsigned char)flat; In.B = (unsigned char)scale;
    In.Tok = TokenAsterisk; In.Imm = 0;
    if (!Emit(Code, Cap, N, In)) return 0;
    *Addr = AllocReg(Next);
    if (*Addr < 0) return 0;
    In.Op = IrLinBin; In.Dest = (unsigned char)*Addr;
    In.A = (unsigned char)base; In.B = (unsigned char)off;
    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
    return Emit(Code, Cap, N, In);
}


struct ValueType *IrLowerCallLocalType(int slot)
{
    IrCallFrame *Fr;
    struct Value *FV = NULL;
    FnCodeSlot *CS = NULL;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS || IrCallDepth <= 0)
        return NULL;
    Fr = IrCallStack[IrCallDepth - 1];
    if (Fr != NULL)
    {
        if (Fr->LocalFromType[slot] != NULL) return Fr->LocalFromType[slot];
        if (Fr->LocalTyp[slot] != NULL) return Fr->LocalTyp[slot];
    }
    FV = IrCurrentFunc[IrCallDepth - 1];
    if (FV != NULL)
        CS = FnCodeFindByFunc(FV);
    /* Never compile from this runtime query: compiling mutates the active
     * lowering tables. Missing metadata must conservatively reject the VM. */
    if (CS != NULL)
        return CS->SlotFromType[slot];
    return NULL;
}


/* --- extract from ir_lower --- */
int IrSafeMemberAddr(struct ParseState *Parser, IrExpr *E, void **Addr,
                            int *Es, int *Eb)
{
    IrExpr *Root;
    IrCallFrame *Fr;
    struct ValueType *ST;
    struct Value *MV = NULL;
    void *Base;
    int base = TypeInt;
    if (E == NULL || E->Kind != IrExMember || E->Left == NULL || E->Ident == NULL)
        return 0;
    Root = E->Left;
    if (IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    /* v[i].m / v[i][j].m: Index chain of aos. Const indices only here. */
    if (Root->Kind == IrExIndex && Root->Left != NULL)
    {
        long long idx = 0;
        int es_arr = 0;
        void *arr = NULL;
        IrExpr *IdxChain[IR_MAX_ARRAY_DIMS];
        int ni = 0, di;
        IrExpr *baseE;
        /* Collect Index chain; require all const for this helper path. */
        {
            IrExpr *w = Root;
            while (w != NULL && w->Kind == IrExIndex && ni < IR_MAX_ARRAY_DIMS)
            {
                if (w->Right == NULL || w->Right->Kind != IrExConst)
                    return 0; /* variable: Step Tok=1 path */
                IdxChain[ni++] = w;
                w = w->Left;
            }
            baseE = w;
        }
        if (ni < 1 || baseE == NULL)
            return 0;
        /* Only Ident or Member under indices (same as before, plus nested Index). */
        if (!(baseE->Kind == IrExIdent || baseE->Kind == IrExMember))
            return 0;
        /* Flatten const indices row-major using LocalDims / TypeArray sizes. */
        {
            int dims[IR_MAX_ARRAY_DIMS];
            int nd = 0;
            long long flat = 0;
            for (di = 0; di < IR_MAX_ARRAY_DIMS; di++) dims[di] = 0;
            if (baseE->Kind == IrExIdent && baseE->LocalSlot >= 0
                && baseE->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                if (Fr->LocalNDims[baseE->LocalSlot] >= 1)
                {
                    nd = (int)Fr->LocalNDims[baseE->LocalSlot];
                    for (di = 0; di < nd && di < IR_MAX_ARRAY_DIMS; di++)
                        dims[di] = Fr->LocalDims[baseE->LocalSlot][di];
                }
                if (nd < ni)
                {
                    struct ValueType *T = Fr->LocalFromType[baseE->LocalSlot];
                    if (T == NULL) T = Fr->LocalTyp[baseE->LocalSlot];
                    nd = 0;
                    while (T != NULL && T->Base == TypeArray && nd < IR_MAX_ARRAY_DIMS)
                    {
                        dims[nd++] = T->ArraySize > 0 ? (int)T->ArraySize : 0;
                        T = T->FromType;
                    }
                }
            }
            else if (baseE->Kind == IrExIdent && baseE->Ident != NULL && Parser != NULL)
            {
                struct Value *GV = IrGlobalValSoft(Parser, baseE);
                struct ValueType *T = (GV && GV->Typ) ? GV->Typ : NULL;
                while (T != NULL && T->Base == TypeArray && nd < IR_MAX_ARRAY_DIMS)
                {
                    dims[nd++] = T->ArraySize > 0 ? (int)T->ArraySize : 0;
                    T = T->FromType;
                }
            }
            /* IdxChain[0]=innermost; outer first in flat formula */
            for (di = 0; di < ni; di++)
            {
                long long ix = IdxChain[ni - 1 - di]->Right->ConstVal;
                int stride = 1, k;
                for (k = di + 1; k < ni; k++)
                    stride *= (k < nd && dims[k] > 0) ? dims[k] : 1;
                flat += ix * (long long)stride;
            }
            idx = flat;
        }

        if (baseE->Kind == IrExMember)
        {
            /* G.pts[i].m / L.pts[i].m — resolve array member base + index. */
            void *mbase = NULL;
            int mes = 0, meb = 0;
            if (!IrSafeMemberAddr(Parser, baseE, &mbase, &mes, &meb) || !mbase)
                return 0;
            {
                struct ValueType *MST = NULL;
                struct Value *MM = NULL;
                IrExpr *Id = baseE->Left;
                while (Id != NULL && Id->Kind == IrExMember) Id = Id->Left;
                if (Id != NULL && Id->Kind == IrExIdent)
                {
                    if (Id->LocalSlot >= 0 && Id->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        struct ValueType *LT = Fr->LocalFromType[Id->LocalSlot];
                        if (LT == NULL) LT = Fr->LocalTyp[Id->LocalSlot];
                        if (LT != NULL && LT->Members
                            && TableGet(LT->Members, baseE->Ident, &MM, NULL, NULL, NULL)
                            && MM && MM->Typ)
                            MST = MM->Typ;
                    }
                    else if (Id->Ident != NULL && Parser)
                    {
                        struct Value *GV = IrGlobalValSoft(Parser, Id);
                        if (GV && GV->Typ && GV->Typ->Members
                            && TableGet(GV->Typ->Members, baseE->Ident, &MM, NULL, NULL, NULL)
                            && MM && MM->Typ)
                            MST = MM->Typ;
                    }
                }
                if (MST != NULL && MST->Base == TypeArray && MST->FromType)
                {
                    es_arr = MST->FromType->Sizeof > 0 ? (int)MST->FromType->Sizeof : 0;
                    ST = MST->FromType;
                }
                else
                    return 0;
            }
            if (es_arr <= 0 || ST == NULL) return 0;
            Base = (char *)mbase + (ptrdiff_t)idx * es_arr;
        }
        else if (baseE->Kind == IrExIdent && baseE->LocalSlot >= 0
            && baseE->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            int ls = baseE->LocalSlot;
            es_arr = Fr->LocalElemSize[ls] > 0 ? (int)Fr->LocalElemSize[ls] : 0;
            ST = Fr->LocalFromType[ls];
            if (ST == NULL) ST = Fr->LocalTyp[ls];
            /* Multidim: peel all TypeArray to leaf struct. */
            while (ST != NULL && ST->Base == TypeArray && ST->FromType != NULL)
                ST = ST->FromType;
            arr = (void *)(intptr_t)Fr->LocalVal[ls];
            if (arr == NULL || ST == NULL) return 0;
            if (es_arr <= 0)
                es_arr = ST->Sizeof > 0 ? (int)ST->Sizeof : 0;
            if (es_arr <= 0) return 0;
            Base = (char *)arr + (ptrdiff_t)idx * es_arr;
        }
        else if (baseE->Kind == IrExIdent && baseE->Ident != NULL && Parser != NULL)
        {
            struct Value *GV = IrGlobalVal(Parser, baseE);
            if (GV == NULL || GV->Typ == NULL || GV->Val == NULL) return 0;
            ST = GV->Typ;
            while (ST != NULL && ST->Base == TypeArray && ST->FromType != NULL)
            {
                struct ValueType *next = ST->FromType;
                if (next->Base != TypeArray)
                    es_arr = next->Sizeof > 0 ? (int)next->Sizeof : 0;
                ST = next;
            }
            if (es_arr <= 0 && ST != NULL && ST->Sizeof > 0)
                es_arr = (int)ST->Sizeof;
            if (es_arr <= 0 || ST == NULL) return 0;
            arr = (void *)GV->Val;
            Base = (char *)arr + (ptrdiff_t)idx * es_arr;
        }
        else
            return 0;
    }
    else if (Root->Kind == IrExUnary && Root->Op == TokenAsterisk
             && Root->Left != NULL && Root->Left->Kind == IrExIdent)
    {
        /* (*gp).m — peel to Ident pointer base. */
        Root = Root->Left;
        if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            ST = Fr->LocalTyp[Root->LocalSlot];
            if (ST == NULL)
                ST = Fr->LocalFromType[Root->LocalSlot];
            Base = (void *)(intptr_t)Fr->LocalVal[Root->LocalSlot];
        }
        else if (Root->ParamSlot >= 0 && Root->ParamSlot < IR_CALL_MAX_PARAMS
                 && Root->ParamSlot < Fr->NumParams)
        {
            ST = Fr->ParamTyp[Root->ParamSlot];
            /* Light frames keep params in LightParam, not ParamVal. */
            if (LightSp > 0)
                Base = (void *)(intptr_t)LightParam[LightSp - 1][Root->ParamSlot];
            else
                Base = (void *)(intptr_t)Fr->ParamVal[Root->ParamSlot];
        }
        else if (Root->Ident != NULL && Parser != NULL)
        {
            struct Value *GV = IrGlobalVal(Parser, Root);
            if (GV == NULL || GV->Typ == NULL || GV->Val == NULL) return 0;
            ST = GV->Typ;
            if (ST->Base == TypePointer && ST->FromType != NULL)
                Base = GV->Val->Pointer;
            else
                Base = (void *)GV->Val;
        }
        else
            return 0;
    }
    else if (Root->Kind == IrExIdent)
    {
        /* Local owned blob, param by-value, or global struct. */
        if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            ST = Fr->LocalTyp[Root->LocalSlot];
            if (ST == NULL)
                ST = Fr->LocalFromType[Root->LocalSlot];
            Base = (void *)(intptr_t)Fr->LocalVal[Root->LocalSlot];
        }
        else if (Root->ParamSlot >= 0 && Root->ParamSlot < IR_CALL_MAX_PARAMS
                 && Root->ParamSlot < Fr->NumParams)
        {
            ST = Fr->ParamTyp[Root->ParamSlot];
            /* Light frames keep params in LightParam, not ParamVal. */
            if (LightSp > 0)
                Base = (void *)(intptr_t)LightParam[LightSp - 1][Root->ParamSlot];
            else
                Base = (void *)(intptr_t)Fr->ParamVal[Root->ParamSlot];
        }
        else if (Root->Ident != NULL && Parser != NULL)
        {
            struct Value *GV = IrGlobalVal(Parser, Root);
            if (GV == NULL || GV->Typ == NULL || GV->Val == NULL) return 0;
            ST = GV->Typ;
            /* Global T* gp: Val holds the pointer cell — use .Pointer as base
             * for gp->m. Global struct/array: Val is the blob itself. */
            if (ST->Base == TypePointer && ST->FromType != NULL)
                Base = GV->Val->Pointer;
            else
                Base = (void *)GV->Val;
        }
        else
            return 0;
    }
    else if (Root->Kind == IrExMember)
    {
        /* Nested p->next->value / o.in.x.
         * Recurse for Root fully; result is address of Root's field.
         * If that field is a pointer (arrow chain), load it to get the
         * pointee base. Do NOT re-walk the whole Member chain — recurse
         * already applied every intermediate load (fixes a->next->next). */
        int sub_es = 0, sub_eb = 0;
        void *Sub = NULL;
        struct Value *SubMV = NULL;
        struct ValueType *FieldST = NULL;
        IrExpr *Grand;
        if (!IrSafeMemberAddr(Parser, Root, &Sub, &sub_es, &sub_eb) || Sub == NULL)
            return 0;
        Base = Sub;
        /* Resolve type of Root->Ident (the field we just addressed). */
        Grand = Root->Left;
        while (Grand != NULL && Grand->Kind == IrExMember)
            Grand = Grand->Left;
        if (Grand != NULL && Grand->Kind == IrExIndex)
            Grand = Grand->Left;
        /* (*pp)->next->value: peel Unary* so Grand is the pointer Ident.
         * DeclFromType of pp may be T* (multi-level pointee); the peel below
         * still reaches the struct for field lookup. */
        if (Grand != NULL && Grand->Kind == IrExUnary && Grand->Op == TokenAsterisk
            && Grand->Left != NULL)
            Grand = Grand->Left;
        if (Grand != NULL && Grand->Kind == IrExIdent)
        {
            struct ValueType *ParentST = NULL;
            if (Grand->LocalSlot >= 0 && Grand->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                ParentST = Fr->LocalFromType[Grand->LocalSlot];
                if (ParentST == NULL)
                    ParentST = Fr->LocalTyp[Grand->LocalSlot];
            }
            else if (Grand->ParamSlot >= 0 && Grand->ParamSlot < IR_CALL_MAX_PARAMS
                     && Grand->ParamSlot < Fr->NumParams)
                ParentST = Fr->ParamTyp[Grand->ParamSlot];
            else if (Grand->Ident != NULL && Parser != NULL)
            {
                struct Value *GV = IrGlobalValSoft(Parser, Grand);
                if (GV != NULL) ParentST = GV->Typ;
            }
            if (ParentST != NULL && ParentST->Base == TypeArray && ParentST->FromType)
                ParentST = ParentST->FromType;
            if (ParentST != NULL && ParentST->Base == TypePointer && ParentST->FromType)
                ParentST = ParentST->FromType;
            /* Walk ONLY the Member path from Grand to Root to get Root field type. */
            if (ParentST != NULL)
            {
                IrExpr *Chain[8];
                int nc = 0, ci;
                IrExpr *C = Root;
                while (C != NULL && C->Kind == IrExMember && nc < 8)
                {
                    Chain[nc++] = C;
                    C = C->Left;
                }
                for (ci = nc - 1; ci >= 0; ci--)
                {
                    struct Value *CM = NULL;
                    if (ParentST == NULL || ParentST->Members == NULL)
                    { ParentST = NULL; break; }
                    if (!TableGet(ParentST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                        || CM == NULL || CM->Typ == NULL)
                    { ParentST = NULL; break; }
                    ParentST = CM->Typ;
                    /* For intermediate chain elements, peel pointer types only
                     * for typing; Base already points at Root field storage. */
                    if (ci > 0 && ParentST->Base == TypePointer && ParentST->FromType)
                        ParentST = ParentST->FromType;
                }
                FieldST = ParentST;
            }
        }
        if (FieldST != NULL && FieldST->Base == TypePointer && FieldST->FromType)
        {
            void *loaded = NULL;
            memcpy(&loaded, Base, sizeof(void *));
            Base = loaded;
            ST = FieldST->FromType;
        }
        else
            ST = FieldST;
        if (ST == NULL)
            return 0;
        (void)SubMV;
    }
    else
        return 0;
    /* p->m: local is T*, peel to struct/union. s.m: local is struct (owned blob). */
    if (ST != NULL && (ST->Base == TypePointer || ST->Base == TypeArray)
        && ST->FromType != NULL)
        ST = ST->FromType;
    if (ST == NULL || ST->Members == NULL)
    {
        return 0;
    }
    if (ST->Base != TypeStruct && ST->Base != TypeUnion)
    {
        return 0;
    }
    if (Base == NULL)
        return 0;
    if (!TableGet(ST->Members, E->Ident, &MV, NULL, NULL, NULL) || MV == NULL || MV->Val == NULL)
    {
        return 0;
    }
    if (Base == NULL)
    {
        return 0;
    }
    {
        int off = 0;
        if (!IrResolveMemberOffsetValue(MV, &off))
            return 0;
        *Addr = (char *)Base + off;
    }
    /* Stamp bitfield meta on expr so LoadIndex can IrBitExtract (soft-match). */
    if (MV->BitWidth > 0)
    {
        E->BitOffset = MV->BitOffset;
        E->BitWidth = MV->BitWidth;
        E->BitSigned = 1;
        if (MV->Typ != NULL)
        {
            switch (MV->Typ->Base)
            {
                case TypeUnsignedInt: case TypeUnsignedShort:
                case TypeUnsignedLong: case TypeUnsignedLongLong:
                case TypeUnsignedChar:
                    E->BitSigned = 0;
                    break;
                default: break;
            }
        }
    }
    if (MV->Typ != NULL)
    {
        base = MV->Typ->Base;
        if (base == TypeChar || base == TypeUnsignedChar)
            *Es = 1;
        else if (base == TypeShort || base == TypeUnsignedShort)
            *Es = 2;
        else if (base == TypePointer || base == TypeArray || base == TypeFunction)
            *Es = (int)sizeof(void *);
        else if (base == TypeLongLong || base == TypeUnsignedLongLong
                 || base == TypeLong || base == TypeUnsignedLong)
            *Es = (int)sizeof(long long);
        else if (base == TypeFP)
            *Es = TypeFPStorageSize(MV->Typ);
        else
            *Es = (int)sizeof(int);
        /* Prefer annotate TypeFlags UNSIGNED if Typ Base is ambiguous (plain char). */
        if ((E->TypeFlags & IR_TF_UNSIGNED)
            || base == TypeUnsignedChar || base == TypeUnsignedShort
            || base == TypeUnsignedInt || base == TypeUnsignedLong
            || base == TypeUnsignedLongLong)
        {
            if (base == TypeChar) base = TypeUnsignedChar;
            else if (base == TypeShort) base = TypeUnsignedShort;
            else if (base == TypeInt) base = TypeUnsignedInt;
            else if (base == TypeLong) base = TypeUnsignedLong;
            else if (base == TypeLongLong) base = TypeUnsignedLongLong;
        }
    }
    else
    {
        *Es = (int)sizeof(int);
        if (E->TypeFlags & IR_TF_UNSIGNED)
            base = TypeUnsignedInt;
    }
    if (Eb) *Eb = base;
    return 1;
}


void IrLinCheckOwnedPointerAccess(void *Ptr, int Size, IrExpr *IX, int Mark)
{
    int di;
    if (Ptr == NULL || Size <= 0 || IrCallDepth <= 0
        || (!Mark && !IrUninitChecksOn()))
        return;
    for (di = IrCallDepth - 1; di >= 0; di--)
    {
        IrCallFrame *Fr = IrCallStack[di];
        int si, nlocals;
        if (Fr == NULL) continue;
        /* NumLocals is the active frame extent; scanning the fixed 128-slot
         * capacity on every pointer store made common accesses needlessly O(128).
         * Clamp defensively because frames are runtime-owned metadata. */
        nlocals = IrFrameLocalExtent(Fr);
        /* Scalar locals live in LocalVal cells. Detect that case by range and
         * alignment once, rather than comparing Ptr against every slot on each
         * pointer store. Integer-address arithmetic avoids unrelated-pointer
         * relational comparisons. */
        {
            uintptr_t p = (uintptr_t)Ptr;
            uintptr_t first = (uintptr_t)&Fr->LocalVal[0];
            uintptr_t end = (uintptr_t)&Fr->LocalVal[IR_CALL_MAX_LOCALS];
            if (p >= first && p < end
                && (p - first) % sizeof(Fr->LocalVal[0]) == 0)
            {
                si = (int)((p - first) / sizeof(Fr->LocalVal[0]));
                if (si < nlocals && !Fr->LocalOwned[si]
                    && Fr->LocalArraySize[si] <= 0 && !Fr->LocalIsPtr[si])
                {
                    if (Mark) Fr->LocalInit[si] = 1;
                    else if (!Fr->LocalInit[si] && LinParser != NULL)
                    {
                        if (IX != NULL && IX->SourceLine > 0) LinParser->Line = IX->SourceLine;
                        if (IX != NULL && IX->Ident != NULL) ParserSetCaretOnIdent(LinParser, IX->Ident);
                        ProgramFail(LinParser, DiagnosticMsgUninitializedVariable);
                    }
                    return;
                }
            }
        }
        /* Frames with no owned locals cannot match an allocation range. This
         * shared count avoids scanning every slot on the overwhelmingly common
         * pointer-store path while preserving the scalar-cell check above. */
        if (Fr->NumOwnedLocals <= 0)
            continue;
        /* Ownership metadata is updated centrally by IrFrameSetLocalOwned. */
        for (si = 0; si < nlocals; si++)
        {
            char *Base;
            int Off, Span = 0, ByteMap;
            struct ValueType *LT;
            if (!Fr->LocalOwned[si] || Fr->LocalVal[si] == 0) continue;
            Base = (char *)(intptr_t)Fr->LocalVal[si];
            ByteMap = IrFrameIsByteMap(Fr, si);
            LT = Fr->LocalFromType[si] ? Fr->LocalFromType[si] : Fr->LocalTyp[si];
            if (ByteMap)
                Span = IrFrameInitMapLen(Fr, si);
            else if (Fr->LocalArraySize[si] > 0 && Fr->LocalElemSize[si] > 0)
                Span = Fr->LocalArraySize[si] * (int)Fr->LocalElemSize[si];
            else if (LT != NULL && LT->Sizeof > 0)
                Span = (int)LT->Sizeof;
            if (Span <= 0 || (char *)Ptr < Base || (char *)Ptr >= Base + Span)
                continue;
            Off = (int)((char *)Ptr - Base);
            if (Mark)
            {
                if (ByteMap && Fr->LocalInitMap[si] != NULL)
                {
                    int i, n = IrFrameInitMapLen(Fr, si);
                    for (i = 0; i < Size && Off + i < n; i++)
                        if (Off + i >= 0) Fr->LocalInitMap[si][Off + i] = 1;
                }
                else if (!ByteMap && Fr->LocalArraySize[si] > 0
                         && Fr->LocalElemSize[si] > 0)
                {
                    int n = Fr->LocalArraySize[si];
                    int es = (int)Fr->LocalElemSize[si], ei = Off / es;
                    if (Fr->LocalInitMap[si] == NULL && n < 65536)
                        Fr->LocalInitMap[si] = (unsigned char *)calloc((size_t)n, 1);
                    if (Fr->LocalInitMap[si] != NULL && ei >= 0 && ei < n)
                        Fr->LocalInitMap[si][ei] = 1;
                }
                else if (Fr->LocalInitMap[si] != NULL && Ptr == Base
                         && Size >= Span)
                {
                    /* A whole-aggregate copy initializes its aggregate map.
                     * Partial member writes must not mark unrelated fields. */
                    int n = IrFrameInitMapLen(Fr, si);
                    if (n > 0) memset(Fr->LocalInitMap[si], 1, (size_t)n);
                }
                Fr->LocalInit[si] = 1;
            }
            else
            {
                int OK = 1;
                if (ByteMap && Fr->LocalInitMap[si] != NULL)
                {
                    int i, n = IrFrameInitMapLen(Fr, si);
                    for (i = 0; i < Size && Off + i < n; i++)
                        if (Off + i >= 0 && Fr->LocalInitMap[si][Off + i] == 0)
                        { OK = 0; break; }
                }
                else if (!ByteMap && Fr->LocalArraySize[si] > 0
                         && Fr->LocalElemSize[si] > 0)
                {
                    int n = Fr->LocalArraySize[si];
                    int es = (int)Fr->LocalElemSize[si], ei = Off / es;
                    if (Fr->LocalInitMap[si] == NULL && n < 65536)
                        Fr->LocalInitMap[si] = (unsigned char *)calloc((size_t)n, 1);
                    if (Fr->LocalInitMap[si] != NULL && ei >= 0 && ei < n
                        && Fr->LocalInitMap[si][ei] == 0)
                        OK = 0;
                }
                else if (!Fr->LocalInit[si])
                    OK = 0;
                if (!OK && LinParser != NULL)
                {
                    if (IX != NULL && IX->SourceLine > 0)
                        LinParser->Line = IX->SourceLine;
                    /* For an indexed pointer access, point at the access itself
                     * (the closing bracket), not the parameter declaration/name.
                     * This mirrors Tree's index-expression diagnostic location. */
                    if (IX != NULL && IX->Kind == IrExIndex
                        && IX->Left != NULL && IX->Left->Ident != NULL)
                    {
                        ParserSetCaretOnIndexAccess(LinParser, IX->Left->Ident);
                        /* Tree diagnostics point one column after the closing
                         * bracket for pointer-param reads. */
                        LinParser->CharacterPos++;
                    }
                    else if (IX != NULL && IX->Ident != NULL)
                        ParserSetCaretOnIdent(LinParser, IX->Ident);
                    else if (Fr->LocalName[si] != NULL)
                        ParserSetCaretOnIdent(LinParser, Fr->LocalName[si]);
                    ProgramFail(LinParser, DiagnosticMsgUninitializedVariable);
                }
            }
                return;
            }
        }
    }


int LinTryParamIndexedAccess(const IrLinInst *In,
                                                            long long *R, int *Ip)
{
    int im = (int)In->Imm;
    IrExpr *IX = (im >= 0 && im < IdxN) ? IdxTab[im] : NULL;
    int ps, es, eb, typed = 0;
    char *Pbase = NULL;
    if (In->Op != IrLinLoadIndex && In->Op != IrLinStoreIndex)
        return 0;
    /* Tok=2/4 indirect stores carry a byte stride in Imm, not IdxTab index. */
    if (In->Op == IrLinStoreIndex && (In->Tok == 2 || In->Tok == 4))
        return 0;
    if (im < 0 || im >= IdxN || IdxMetaKind[im] != 2
        || IdxMetaSlot[im] < 0 || IdxMetaSlot[im] >= IR_CALL_MAX_PARAMS
        || IX == NULL || (In->Op == IrLinStoreIndex && LinParser == NULL))
        return 0;
    ps = IdxMetaSlot[im];
    es = IdxMetaEs[im] > 0 ? (int)IdxMetaEs[im] : 4;
    eb = TypeInt;
    if (LightSp > 0)
        Pbase = (char *)(intptr_t)LightParam[LightSp - 1][ps];
    else if (IrCallDepth > 0 && ps < IrCallStack[IrCallDepth - 1]->NumParams)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        struct ValueType *PT = Fr->ParamTyp[ps];
        Pbase = (char *)(intptr_t)Fr->ParamVal[ps];
        if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL)
        {
            es = PT->FromType->Sizeof > 0 ? (int)PT->FromType->Sizeof : es;
            eb = PT->FromType->Base;
            typed = 1;
        }
    }
    if (!typed)
        eb = es == 1 ? TypeChar : es == 2 ? TypeShort : es == 8 ? TypeLongLong : TypeInt;
    if (Pbase == NULL || es <= 0 || es > (int)sizeof(long long))
        return 0;
    if (In->Op == IrLinLoadIndex)
    {
        long long idx = R[In->A];
        IrLinCheckOwnedPointerAccess(Pbase + (ptrdiff_t)idx * es, es, IX, 0);
        if (IX->TypeFlags & IR_TF_UNSIGNED)
        {
            if (eb == TypeChar) eb = TypeUnsignedChar;
            else if (eb == TypeShort) eb = TypeUnsignedShort;
            else if (eb == TypeInt) eb = TypeUnsignedInt;
            else if (eb == TypeLong) eb = TypeUnsignedLong;
            else if (eb == TypeLongLong) eb = TypeUnsignedLongLong;
        }
        if (es == 1)
            R[In->Dest] = (eb == TypeUnsignedChar)
                ? (long long)((const unsigned char *)Pbase)[idx]
                : (long long)((const signed char *)Pbase)[idx];
        else if (es == 2)
            R[In->Dest] = (eb == TypeUnsignedShort)
                ? (long long)((const unsigned short *)Pbase)[idx]
                : (long long)((const short *)Pbase)[idx];
        else if (es == 4)
            R[In->Dest] = (eb == TypeUnsignedInt || eb == TypeUnsignedLong)
                ? (long long)((const unsigned int *)Pbase)[idx]
                : (long long)((const int *)Pbase)[idx];
        else if (es == 8)
            R[In->Dest] = (eb == TypeUnsignedLong || eb == TypeUnsignedLongLong)
                ? (long long)((const unsigned long long *)Pbase)[idx]
                : ((const long long *)Pbase)[idx];
        else
            R[In->Dest] = IrLoadPtrElem(eb, Pbase + (ptrdiff_t)idx * es);
        if (In->Dest < IR_LIN_REGS)
            LinRegU[In->Dest] = (eb == TypeUnsignedChar || eb == TypeUnsignedShort
                || eb == TypeUnsignedInt || eb == TypeUnsignedLong
                || eb == TypeUnsignedLongLong);
    }
    else
    {
        void *P = Pbase + (ptrdiff_t)R[In->B] * es;
        if (eb == TypeChar && (IX->TypeFlags & IR_TF_UNSIGNED)) eb = TypeUnsignedChar;
        else if (eb == TypeShort && (IX->TypeFlags & IR_TF_UNSIGNED)) eb = TypeUnsignedShort;
        else if (eb == TypeInt && (IX->TypeFlags & IR_TF_UNSIGNED)) eb = TypeUnsignedInt;
        else if (eb == TypeLong && (IX->TypeFlags & IR_TF_UNSIGNED)) eb = TypeUnsignedLong;
        else if (eb == TypeLongLong && (IX->TypeFlags & IR_TF_UNSIGNED)) eb = TypeUnsignedLongLong;
        IrStorePtrElem(LinParser, eb, P, R[In->A]);
        IrLinCheckOwnedPointerAccess(P, es, IX, 1);
        if (es == 1)
            R[In->A] = (eb == TypeUnsignedChar)
                ? (long long)*(const unsigned char *)P : (long long)*(const signed char *)P;
        else if (es == 2)
            R[In->A] = (eb == TypeUnsignedShort)
                ? (long long)*(const unsigned short *)P : (long long)*(const short *)P;
        else if (es == 4)
            R[In->A] = (eb == TypeUnsignedInt || eb == TypeUnsignedLong)
                ? (long long)*(const unsigned int *)P : (long long)*(const int *)P;
        else if (es == 8)
            R[In->A] = (eb == TypeUnsignedLong || eb == TypeUnsignedLongLong)
                ? (long long)*(const unsigned long long *)P : *(const long long *)P;
        else
            R[In->A] = IrLoadPtrElem(eb, P);
        if (In->A < IR_LIN_REGS)
            LinRegU[In->A] = (eb == TypeUnsignedChar || eb == TypeUnsignedShort
                || eb == TypeUnsignedInt || eb == TypeUnsignedLong
                || eb == TypeUnsignedLongLong);
    }
    (*Ip)++;
    return 1;
}


