extern const char *LinHostCallName;
/* ir_call.c - Body-IR call entry + Intrinsic pack (D1 step 4)
 *
 * Owns: IrExecuteFunctionBody entry, leaf return analysis, FuncMeta cache,
 *       Intrinsic/token Value pack (IrPack*).
 * Calls into ir_exec for IrExecuteStmt (body walk). Frame via ir_frame.
 * IrCallUserFuncCached / mono / memo / kernel remain in ir_exec until ir_fast.
 */
struct ParseState;
extern void IrVmStrictTreeFallback(struct ParseState *, const char *);
#include "picoc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ir_runtime.h"
#include "ir_storage.h"
#include "ir_lin.h"
const char *IrDebugFunc[IR_CALL_MAX_DEPTH];
struct Value *IrCurrentFunc[IR_CALL_MAX_DEPTH];

int BabycDebugEnabled(void)
{
    static int On = -1;
    if (On < 0)
        On = (getenv("BABYC_DEBUG") != NULL) ? 1 : 0;
    return On;
}

void IrDebugSetFunc(struct ParseState *Parser, struct Value *FuncValue, const char *Hint)
{
    const char *Nm = "?";
    IrFuncMeta *Meta = NULL;
    /* Name is registered at definition (ParseFunctionDefinition → Meta->Name).
     * NEVER scan GlobalTable: LibraryAdd / include can free transient keys and
     * the scan was both a UAF source under ASan and a hot-path cost. */
    (void)Parser;
    if (IrCallDepth <= 0 || IrCallDepth >= IR_CALL_MAX_DEPTH)
        return;
    IrCurrentFunc[IrCallDepth - 1] = FuncValue;
    if (Hint != NULL && Hint[0] != '\0')
    {
        Nm = Hint;
        if (FuncValue != NULL)
        {
            Meta = IrFuncMetaGet(FuncValue);
            if (Meta != NULL && Meta->Name == NULL)
                Meta->Name = Nm;
        }
        IrDebugFunc[IrCallDepth - 1] = Nm;
        return;
    }
    if (FuncValue != NULL)
    {
        Meta = IrFuncMetaGet(FuncValue);
        if (Meta != NULL && Meta->Name != NULL)
            Nm = Meta->Name;
    }
    IrDebugFunc[IrCallDepth - 1] = Nm;
}

#include "ir_bridge.h"

/* IrExecuteStmt / IrEvalExpr: static inline in ir_runtime.h (IrHookOn gate) */

#define IR_FCACHE_SIZE 128
static IrFuncMeta IrFCache[IR_FCACHE_SIZE];

IrFuncMeta *IrFuncMetaGet(struct Value *F)
{
    unsigned h;
    int i, slot;
    if (F == NULL) return NULL;
    h = (unsigned)(uintptr_t)F;
    slot = (int)(h % IR_FCACHE_SIZE);
    for (i = 0; i < IR_FCACHE_SIZE; i++)
    {
        int s = (slot + i) % IR_FCACHE_SIZE;
        if (IrFCache[s].Func == F)
            return &IrFCache[s];
        if (IrFCache[s].Func == NULL)
        {
            IrFCache[s].Func = F;
            IrFCache[s].Name = NULL;
            IrFCache[s].MemoElig = -1;
            IrFCache[s].MonoElig = -1;
            IrFCache[s].VmTreeSkip = 0;
            IrFCache[s].IsLeaf = -1;
            IrFCache[s].TplReady = 0;
            IrFCache[s].IsThinKernel = -1;
            IrFCache[s].TplNLocals = 0;
            IrFCache[s].LeafExpr = NULL;
            IrFCache[s].LeafStmt = NULL;
            return &IrFCache[s];
        }
    }
    /* table full: overwrite slot - clear Name so stale key is never reused */
    IrFCache[slot].Func = F;
    IrFCache[slot].Name = NULL;
    IrFCache[slot].MemoElig = -1;
    IrFCache[slot].MonoElig = -1;
    IrFCache[slot].VmTreeSkip = 0;
    IrFCache[slot].IsLeaf = -1;
    IrFCache[slot].TplReady = 0;
    IrFCache[slot].IsThinKernel = -1;
    IrFCache[slot].TplNLocals = 0;
    IrFCache[slot].LeafExpr = NULL;
    IrFCache[slot].LeafStmt = NULL;
    return &IrFCache[slot];
}

/* Bind debug name using already-fetched Meta - avoids second IrFuncMetaGet. */
static void IrDebugBind(struct Value *FuncValue, IrFuncMeta *Meta, const char *Hint)
{
    if (IrCallDepth <= 0 || IrCallDepth >= IR_CALL_MAX_DEPTH)
        return;
    IrCurrentFunc[IrCallDepth - 1] = FuncValue;
    if (Hint != NULL && Hint[0] != '\0')
    {
        if (Meta != NULL && Meta->Name == NULL)
            Meta->Name = Hint;
        IrDebugFunc[IrCallDepth - 1] = Hint;
        return;
    }
    IrDebugFunc[IrCallDepth - 1] = (Meta != NULL && Meta->Name != NULL) ? Meta->Name : "?";
}

void IrMetaSeedFromBody(IrFuncMeta *Meta, void *BodyIr)
{
    short nl = 0, np = 0;
    if (Meta == NULL || BodyIr == NULL || Meta->TplReady)
        return;
    if (!IrBodyFrameLookup(BodyIr, &nl, &np, Meta->SlotKind, Meta->SlotWidth))
        return;
    Meta->TplNLocals = nl;
    /* TplReady only when every slot is scalar int/ptr/fp (no array/UNK). */
    {
        int ti, ok = (nl >= 0);
        for (ti = 0; ti < nl && ti < IR_CALL_MAX_LOCALS; ti++)
        {
            unsigned char k = Meta->SlotKind[ti];
            if (k != IR_FRAME_SLOT_INT && k != IR_FRAME_SLOT_PTR && k != IR_FRAME_SLOT_FP)
            { ok = 0; break; }
        }
        if (ok)
            Meta->TplReady = 1;
    }
}


int IrIsLeafReturn(IrStmt *Body, IrExpr **RetExprOut, IrStmt **RetStmtOut)
{
    if (Body == NULL || RetExprOut == NULL)
        return 0;
    if (RetStmtOut) *RetStmtOut = NULL;
    if (Body->Kind == IrStReturn && Body->Expr != NULL)
    {
        *RetExprOut = Body->Expr;
        if (RetStmtOut) *RetStmtOut = Body;
        return 1;
    }
    if (Body->Kind == IrStBlock && Body->StmtCount == 1
        && Body->Stmts != NULL && Body->Stmts[0] != NULL
        && Body->Stmts[0]->Kind == IrStReturn
        && Body->Stmts[0]->Expr != NULL)
    {
        *RetExprOut = Body->Stmts[0]->Expr;
        if (RetStmtOut) *RetStmtOut = Body->Stmts[0];
        return 1;
    }
    return 0;
}


struct Value *IrPackAlloc(struct ParseState *Parser, struct ValueType *Typ,
                                 int DataSize)
{
    Picoc *pc = Parser->pc;
    int need;
    struct Value *NV;
    if (Typ == NULL)
        Typ = &pc->IntType;
    /* Intrinsic ABI may read Val->Pointer even when the formal type is int
     * (varargs: sqlite3_exec(db, sql, NULL, NULL, NULL)). On 64-bit,
     * packing only sizeof(int) made Pointer reads overrun AnyValue storage. */
    if (DataSize < (int)sizeof(union AnyValue))
        DataSize = (int)sizeof(union AnyValue);
    need = (int)MEM_ALIGN(sizeof(struct Value)) + DataSize;
    NV = (struct Value *)HeapAllocStack(pc, need);
    if (NV == NULL)
        ProgramFail(Parser, "stack overflow (local variables / call frames exceed stack size)");
    NV->Val = (union AnyValue *)((char *)NV + MEM_ALIGN(sizeof(struct Value)));
    memset(NV->Val, 0, (size_t)DataSize);
    NV->Typ = Typ;
    NV->LValueFrom = NULL;
    NV->ValOnHeap = 0;
    NV->ValOnStack = 1;
    NV->AnyValOnHeap = 0;
    NV->IsLValue = 0;
    NV->IsConst = 0;
    NV->IsVolatile = 0;
    NV->IsInitialized = 1;
    NV->ElemInit = NULL;
    NV->ElemInitCount = 0;
    NV->ScopeID = 0;
    NV->OutOfScope = 0;
    NV->BitOffset = 0;
    NV->BitWidth = 0;
    NV->DeclIndex = 0;
    return NV;
}


struct Value *IrPackReturnValue(struct ParseState *Parser, struct ValueType *Typ)
{
    int Size;
    struct Value *RV;
    if (Typ == NULL)
        Typ = &Parser->pc->VoidType;
    Size = TypeSize(Typ, Typ->ArraySize, FALSE);
    if (Size < 0)
        Size = 0;
    if (Size < (int)sizeof(union AnyValue) && Typ->Base != TypeVoid)
        Size = (int)sizeof(union AnyValue);
    RV = IrPackAlloc(Parser, Typ, Size);
    RV->IsLValue = FALSE;
    RV->IsInitialized = FALSE; /* return not written yet */
    return RV;
}


struct Value *IrPackIntArg(struct ParseState *Parser, long long V, IrExpr *A)
{
    int use_ll = 0;
    struct Value *PV;
    struct ValueType *UseTyp = &Parser->pc->IntType;
    if (V > 2147483647LL || V < (-2147483647LL - 1))
        use_ll = 1;
    if (A != NULL && A->Kind == IrExIdent && IrCallDepth > 0)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        struct ValueType *LT = NULL;
        if (A->LocalSlot >= 0 && A->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            LT = Fr->LocalTyp[A->LocalSlot];
            if (LT == NULL)
                LT = Fr->LocalFromType[A->LocalSlot];
        }
        else if (A->ParamSlot >= 0 && A->ParamSlot < Fr->NumParams)
            LT = Fr->ParamTyp[A->ParamSlot];
        if (LT != NULL && (LT->Base == TypeLongLong || LT->Base == TypeUnsignedLongLong
            || LT->Base == TypeLong || LT->Base == TypeUnsignedLong))
        {
            use_ll = 1;
            UseTyp = LT;
        }
        else if (LT != NULL && (LT->Base == TypeInt || LT->Base == TypeUnsignedInt
                 || LT->Base == TypeShort || LT->Base == TypeUnsignedShort
                 || LT->Base == TypeChar || LT->Base == TypeUnsignedChar))
            UseTyp = LT;
    }
    if (use_ll)
    {
        if (UseTyp->Base != TypeLongLong && UseTyp->Base != TypeUnsignedLongLong
            && UseTyp->Base != TypeLong && UseTyp->Base != TypeUnsignedLong)
            UseTyp = &Parser->pc->LongLongType;
        PV = IrPackAlloc(Parser, UseTyp, (int)sizeof(long long));
        PV->Val->LongLongInteger = V;
    }
    else
    {
        PV = IrPackAlloc(Parser, UseTyp, (int)sizeof(int));
        PV->Val->Integer = (int)V;
    }
    return PV;
}


struct Value *IrPackPtrArg(struct ParseState *Parser, long long Bits, IrExpr *A)
{
    struct ValueType *UseTyp = Parser->pc->CharPtrType;
    struct Value *PV;
    if (A != NULL && A->Kind == IrExIdent && IrCallDepth > 0)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        struct ValueType *LT = NULL;
        if (A->LocalSlot >= 0 && A->LocalSlot < IR_CALL_MAX_LOCALS)
            LT = Fr->LocalTyp[A->LocalSlot];
        else if (A->ParamSlot >= 0 && A->ParamSlot < Fr->NumParams)
            LT = Fr->ParamTyp[A->ParamSlot];
        if (LT != NULL && LT->Base == TypePointer)
            UseTyp = LT;
    }
    PV = IrPackAlloc(Parser, UseTyp, (int)sizeof(void *));
    PV->Val->Pointer = (void *)(intptr_t)Bits;
    return PV;
}

/* Prototype conversion for host (Intrinsic) calls, as C does for a declared
 * parameter: int argument to a double parameter (sqrt(k), pow(2, 10)) and
 * double argument to an integer parameter. Variadic tail (i >= NumParams)
 * keeps the default promotions. Shared by the tree (ir_eval.c) and linear
 * (IrCallHostFromRegs) host call paths. */
void IrHostCoerceParams(struct ParseState *Parser, struct Value *FuncValue,
                        struct Value **ParamArray, int ArgCount)
{
    int i, np;
    struct ValueType **PT;
    if (FuncValue == NULL || FuncValue->Val == NULL || ParamArray == NULL)
        return;
    np = FuncValue->Val->FuncDef.NumParams;
    PT = FuncValue->Val->FuncDef.ParamType;
    if (PT == NULL)
        return;
    for (i = 0; i < ArgCount && i < np; i++)
    {
        struct Value *A = ParamArray[i];
        struct ValueType *T = PT[i];
        if (A == NULL || A->Typ == NULL || T == NULL)
            continue;
#ifndef NO_FP
        if (T->Base == TypeFP && A->Typ->Base != TypeFP && IS_INTEGER_NUMERIC(A))
        {
            union { double d; long long l; } u;
            u.d = IrTypeIsUnsignedBase(A->Typ->Base)
                ? (double)(unsigned long long)IrValueAsInt64(A)
                : (double)IrValueAsInt64(A);
            ParamArray[i] = IrPackFPArg(Parser, u.l);
        }
        else if (A->Typ->Base == TypeFP && IS_INTEGER_NUMERIC_TYPE(T))
            ParamArray[i] = IrPackIntArg(Parser, (long long)A->Val->FP, NULL);
#endif
        if ((T->Base == TypePointer || T->Base == TypeArray)
            && IS_INTEGER_NUMERIC(A))
            /* address bits packed as an integer (pointer-valued expression
             * not classified as pointer): the parameter is a pointer. */
            ParamArray[i] = IrPackPtrArg(Parser, IrValueAsInt64(A), NULL);
    }
}

/* Scan printf format: classify each conversion as int(0)/ptr(1)/fp(2).
 * Handles * width/precision as separate int slots. */
static int IrFmtClassify(const char *fmt, unsigned char *kinds, int maxk)
{
    int nk = 0;
    const char *p = fmt;
    if (p == NULL || kinds == NULL || maxk <= 0) return 0;
    while (*p && nk < maxk)
    {
        if (*p != '%') { p++; continue; }
        p++;
        if (*p == '%') { p++; continue; }
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        if (*p == '*') { if (nk < maxk) kinds[nk++] = 0; p++; }
        else while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') {
            p++;
            if (*p == '*') { if (nk < maxk) kinds[nk++] = 0; p++; }
            else while (*p >= '0' && *p <= '9') p++;
        }
        if (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'z' || *p == 'j' || *p == 't') {
            char c = *p++;
            if ((c == 'h' || c == 'l') && *p == c) p++;
        }
        if (*p == '\0') break;
        {
            char c = *p++;
            unsigned char k = 0;
            if (c == 's' || c == 'p' || c == 'n') k = 1;
            else if (c == 'f' || c == 'F' || c == 'e' || c == 'E'
                     || c == 'g' || c == 'G' || c == 'a' || c == 'A')
                k = 2;
            else
                k = 0;
            if (nk < maxk) kinds[nk++] = k;
        }
    }
    return nk;
}

/* Linear/VM host call: pack regs → PicocCallIntrinsic. Side-effecting
 * (printf); only invoke under EXEC=vm / BABYC_LOWER_CALL (not EXEC=both). */
long long IrCallHostFromRegs(struct ParseState *Parser, struct Value *FuncValue,
    long long *ArgVals, int ArgCount, const unsigned char *IsPtr,
    const unsigned char *IsFp)
{
    struct Value *ReturnValue;
    struct Value **ParamArray;
    long long Result = 0;
    enum RunMode SavedMode;
    int i;
    if (Parser == NULL || FuncValue == NULL || FuncValue->Val == NULL
        || FuncValue->Val->FuncDef.Intrinsic == NULL)
        return 0;
    if (ArgCount < 0) ArgCount = 0;
    if (ArgCount > 16) ArgCount = 16;

    /* (5) Fast-path host printf for common simple formats - no Heap pack.
     * Only for actual print functions: free(p)/malloc also have a pointer
     * first arg and must not be treated as printf(fmt). */
    if (ArgVals != NULL && ArgCount >= 1 && IsPtr != NULL && IsPtr[0]
        && LinHostCallName != NULL
        && strcmp(LinHostCallName, "printf") == 0)
    {
        const char *fmt = (const char *)(intptr_t)ArgVals[0];
        if (fmt != NULL)
        {
            if (ArgCount == 1)
            {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#endif
                return (long long)printf(fmt);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            }
            if (ArgCount == 2 && IsPtr[1] == 0)
            {
                if (fmt[0]=='%' && fmt[1]=='d' && fmt[2]=='\n' && fmt[3]=='\0')
                { printf("%d\n", (int)ArgVals[1]); return 0; }
                if (fmt[0]=='%' && fmt[1]=='d' && fmt[2]=='\0')
                { printf("%d", (int)ArgVals[1]); return 0; }
                if (fmt[0]=='%' && fmt[1]=='u' && fmt[2]=='\n' && fmt[3]=='\0')
                { printf("%u\n", (unsigned)ArgVals[1]); return 0; }
                if (fmt[0]=='%' && fmt[1]=='u' && fmt[2]=='\0')
                { printf("%u", (unsigned)ArgVals[1]); return 0; }
                if (fmt[0]=='%' && fmt[1]=='l' && fmt[2]=='l' && fmt[3]=='d'
                    && fmt[4]=='\n' && fmt[5]=='\0')
                { printf("%lld\n", (long long)ArgVals[1]); return 0; }
                if (fmt[0]=='%' && fmt[1]=='l' && fmt[2]=='d' && fmt[3]=='\n'
                    && fmt[4]=='\0')
                { printf("%ld\n", (long)ArgVals[1]); return 0; }
                if (fmt[0]=='%' && fmt[1]=='f' && fmt[2]=='\n' && fmt[3]=='\0')
                {
                    union { long long l; double d; } u;
                    u.l = ArgVals[1];
                    printf("%f\n", u.d);
                    return 0;
                }
                if (fmt[0]=='%' && fmt[1]=='f' && fmt[2]=='\0')
                {
                    union { long long l; double d; } u;
                    u.l = ArgVals[1];
                    printf("%f", u.d);
                    return 0;
                }
            }
            if (ArgCount == 3 && IsPtr[1] == 0 && IsPtr[2] == 0
                && fmt[0]=='%' && fmt[1]=='d' && fmt[2]==' ' && fmt[3]=='%'
                && fmt[4]=='d' && fmt[5]=='\n' && fmt[6]=='\0')
            {
                printf("%d %d\n", (int)ArgVals[1], (int)ArgVals[2]);
                return 0;
            }
            if (ArgCount == 3 && IsPtr[1] == 0 && IsPtr[2] == 0
                && fmt[0]=='%' && fmt[1]=='f' && fmt[2]==' '
                && fmt[3]=='%' && fmt[4]=='f' && fmt[5]=='\n' && fmt[6]=='\0')
            {
                union { long long l; double d; } a, b;
                a.l = ArgVals[1]; b.l = ArgVals[2];
                printf("%f %f\n", a.d, b.d);
                return 0;
            }
            if (ArgCount == 4 && IsPtr[1] == 0 && IsPtr[2] == 0 && IsPtr[3] == 0
                && fmt[0]=='%' && fmt[1]=='f' && fmt[2]==' '
                && fmt[3]=='%' && fmt[4]=='f' && fmt[5]==' '
                && fmt[6]=='%' && fmt[7]=='f' && fmt[8]=='\n' && fmt[9]=='\0')
            {
                union { long long l; double d; } a, b, c;
                a.l = ArgVals[1]; b.l = ArgVals[2]; c.l = ArgVals[3];
                printf("%f %f %f\n", a.d, b.d, c.d);
                return 0;
            }
            if (ArgCount == 5
                && IsPtr[1] == 0 && IsPtr[2] == 0 && IsPtr[3] == 0 && IsPtr[4] == 0
                && fmt[0]=='%' && fmt[1]=='f' && fmt[2]==' '
                && fmt[3]=='%' && fmt[4]=='f' && fmt[5]==' '
                && fmt[6]=='%' && fmt[7]=='f' && fmt[8]==' '
                && fmt[9]=='%' && fmt[10]=='f' && fmt[11]=='\n' && fmt[12]=='\0')
            {
                union { long long l; double d; } a, b, c, d;
                a.l = ArgVals[1]; b.l = ArgVals[2];
                c.l = ArgVals[3]; d.l = ArgVals[4];
                printf("%f %f %f %f\n", a.d, b.d, c.d, d.d);
                return 0;
            }
        }
    }

    SavedMode = Parser->Mode;
    HeapPushStackFrame(Parser->pc);
    ReturnValue = IrPackReturnValue(Parser, FuncValue->Val->FuncDef.ReturnType);
    ParamArray = HeapAllocStack(Parser->pc,
        sizeof(struct Value *) * (ArgCount + 1));
    if (ParamArray == NULL)
        ProgramFail(Parser, DiagnosticMsgOutOfMemory);
    /* Format-aware kinds for printf/sprintf family when IsFp missing slots */
    {
        unsigned char fmtk[16];
        int nfk = 0;
        int is_printf_family = 0;
        if (LinHostCallName != NULL
            && (strcmp(LinHostCallName, "printf") == 0
                || strcmp(LinHostCallName, "sprintf") == 0
                || strcmp(LinHostCallName, "snprintf") == 0
                || strcmp(LinHostCallName, "fprintf") == 0))
            is_printf_family = 1;
        if (is_printf_family && ArgVals != NULL && ArgCount >= 1
            && IsPtr != NULL && IsPtr[0])
        {
            const char *fmt = (const char *)(intptr_t)ArgVals[0];
            nfk = IrFmtClassify(fmt, fmtk, 16);
        }
        for (i = 0; i < ArgCount; i++)
        {
            long long V = ArgVals ? ArgVals[i] : 0;
            int as_ptr = (IsPtr != NULL && IsPtr[i]) ? 1 : 0;
            int as_fp = (IsFp != NULL && IsFp[i]) ? 1 : 0;
            /* fmt conversions start at arg 1 for printf; arg 2 for sprintf/fprintf */
            if (!as_ptr && !as_fp && nfk > 0)
            {
                int fi = -1;
                if (LinHostCallName != NULL && strcmp(LinHostCallName, "printf") == 0)
                    fi = i - 1; /* skip format */
                else if (LinHostCallName != NULL
                         && (strcmp(LinHostCallName, "sprintf") == 0
                             || strcmp(LinHostCallName, "fprintf") == 0))
                    fi = i - 2; /* skip dest/stream + format */
                else if (LinHostCallName != NULL && strcmp(LinHostCallName, "snprintf") == 0)
                    fi = i - 3; /* buf, n, fmt */
                if (fi >= 0 && fi < nfk)
                {
                    if (fmtk[fi] == 2) as_fp = 1;
                    else if (fmtk[fi] == 1) as_ptr = 1;
                }
            }
            if (as_ptr)
                ParamArray[i] = IrPackPtrArg(Parser, V, NULL);
            else if (as_fp)
                ParamArray[i] = IrPackFPArg(Parser, V);
            else
                ParamArray[i] = IrPackIntArg(Parser, V, NULL);
        }
    }
    {
        const char *host_nm = "host";
        if (LinHostCallName != NULL && LinHostCallName[0] != '\0')
            host_nm = LinHostCallName;
        else if (FuncValue->Typ != NULL && FuncValue->Typ->Identifier != NULL
            && FuncValue->Typ->Identifier[0] != '\0')
            host_nm = FuncValue->Typ->Identifier;
        IrHostCoerceParams(Parser, FuncValue, ParamArray, ArgCount);
        PicocCallIntrinsic(Parser, FuncValue->Val->FuncDef.Intrinsic,
            ReturnValue, ParamArray, ArgCount, host_nm);
    }
    if (ReturnValue->Typ != &Parser->pc->VoidType
        && ReturnValue->Typ->Base != TypeVoid)
        Result = IR_RET_BITS(ReturnValue);
    HeapPopStackFrame(Parser->pc);
    Parser->Mode = SavedMode;
    return Result;
}

void IrExecuteFunctionBody(struct ParseState *Parser, struct Value *FuncValue,
                           struct Value **ParamArray, int ArgCount)
{
    IrCallFrame *F;
    const char *SavedSource;
    char *SavedFile;
    int SavedLine;
    int SavedCharPos;
    const unsigned char *SavedTokPos;
    int pi;

    if (FuncValue == NULL || FuncValue->Val->FuncDef.BodyIr == NULL)
        return;
    if (IrCallDepth >= IR_CALL_MAX_DEPTH)
        ProgramFail(Parser, "IR call stack overflow");

    /* Diagnostics (caret/column) must use the function's original source, not
     * the call-site fragment (e.g. startup "main();").
     * Also save Pos: nested IR for/call must not clobber the caller's lexer. */
    SavedSource = Parser->SourceText;
    SavedFile = Parser->FileName;
    SavedLine = Parser->Line;
    SavedCharPos = Parser->CharacterPos;
    SavedTokPos = Parser->Pos;
    if (FuncValue->Val->FuncDef.Body.SourceText != NULL)
        Parser->SourceText = FuncValue->Val->FuncDef.Body.SourceText;
    if (FuncValue->Val->FuncDef.Body.FileName != NULL)
        Parser->FileName = FuncValue->Val->FuncDef.Body.FileName;

    F = IrFramePush(Parser);
    if (F == NULL)
        ProgramFail(Parser, "IR call stack overflow");
    /* Point3b: compile-time frame shape → Meta.Tpl* before body runs */
    {
        IrFuncMeta *Meta = IrFuncMetaGet(FuncValue);
        IrDebugBind(FuncValue, Meta, NULL);
        if (Meta != NULL && !Meta->TplReady)
            IrMetaSeedFromBody(Meta, FuncValue->Val->FuncDef.BodyIr);
        if (Meta != NULL && Meta->TplReady
            && Meta->TplNLocals >= 0 && Meta->TplNLocals <= IR_CALL_MAX_LOCALS)
        {
            int ti;
            IrFrameSetLocalExtent(F, Meta->TplNLocals);
            F->CompactClear = 1; /* 1b: hot-only pop clear */
            for (ti = 0; ti < Meta->TplNLocals; ti++)
            {
                /* P0: SlotKind → LocalIsPtr via descriptor */
                IrStorageDescApplySlotKind(F, ti, Meta->SlotKind[ti]);
                F->LocalInit[ti] = 0;
            }
        }
    }

    /* ParamArray: wire real arguments into IR slots (ParamArray / HANDOFF §15) */
    if (ParamArray != NULL && ArgCount > 0)
    {
        int N = ArgCount;
        if (N > FuncValue->Val->FuncDef.NumParams)
            N = FuncValue->Val->FuncDef.NumParams;
        if (N > IR_CALL_MAX_PARAMS)
            N = IR_CALL_MAX_PARAMS;
        F->NumParams = N;
        for (pi = 0; pi < N; pi++)
        {
            struct Value *PV = ParamArray[pi];
            struct ValueType *PT = FuncValue->Val->FuncDef.ParamType
                ? FuncValue->Val->FuncDef.ParamType[pi] : NULL;
            const char *PName = FuncValue->Val->FuncDef.ParamName
                ? FuncValue->Val->FuncDef.ParamName[pi] : NULL;
            /* P0: unify ParamName/Typ/IsPtr meta write */
            IrStorageDescApplyParamMeta(F, pi, PT, PName, 0);
            if (PV == NULL)
            {
                F->ParamVal[pi] = 0;
                continue;
            }
            /* Function designator / function-pointer arg: pass the persistent
             * Func Value* (trampoline for pthread_create / CreateThread).
             * Must run before void* formal packing, which would read an unset
             * Pointer field on TypeFunction values. */
            if (PV->Typ != NULL && PV->Typ->Base == TypeFunction)
            {
                F->ParamIsPtr[pi] = 1; /* deliver as Pointer to Intrinsic */
                F->ParamVal[pi] = (long long)(intptr_t)(void *)PV;
            }
            else if (PT != NULL && PT->Base == TypePointer
                && PT->FromType != NULL && PT->FromType->Base == TypeFunction)
            {
                F->ParamIsPtr[pi] = 1;
                if (PV->Typ != NULL && PV->Typ->Base == TypePointer
                         && PV->Val != NULL)
                    F->ParamVal[pi] = (long long)(intptr_t)PV->Val->Pointer;
                else
                    F->ParamVal[pi] = (long long)(intptr_t)PV;
            }
            else if (PT != NULL && (PT->Base == TypePointer || PT->Base == TypeArray))
            {
                F->ParamIsPtr[pi] = 1;
                if (PT->Base == TypeArray)
                    F->ParamVal[pi] = (long long)(intptr_t)(void *)PV->Val;
                else
                    F->ParamVal[pi] = (long long)(intptr_t)PV->Val->Pointer;
            }
            else if (PV->Typ != NULL && (PV->Typ->Base == TypePointer || PV->Typ->Base == TypeArray))
            {
                F->ParamIsPtr[pi] = 1;
                if (PV->Typ->Base == TypeArray)
                    F->ParamVal[pi] = (long long)(intptr_t)(void *)PV->Val;
                else
                    F->ParamVal[pi] = (long long)(intptr_t)PV->Val->Pointer;
            }
            else if ((PT != NULL && (PT->Base == TypeStruct || PT->Base == TypeUnion))
                     || (PV->Typ != NULL && (PV->Typ->Base == TypeStruct || PV->Typ->Base == TypeUnion)))
            {
                /* struct/union by value: pass address of payload (callee reads fields) */
                struct ValueType *ST = PT != NULL ? PT : PV->Typ;
                IrStorageDescApplyParamMeta(F, pi, ST, PName, 1);
                if (PV->Typ != NULL && (PV->Typ->Base == TypeStruct || PV->Typ->Base == TypeUnion))
                    F->ParamVal[pi] = (long long)(intptr_t)(void *)PV->Val;
                else if (PV->Val != NULL)
                    F->ParamVal[pi] = (long long)(intptr_t)PV->Val->Pointer;
                else
                    F->ParamVal[pi] = 0;
            }
#ifndef NO_FP
            else if (PV->Typ != NULL && PV->Typ->Base == TypeFP)
            {
#ifndef NO_FP
                union { long long l; double d; } u;
                u.d = PV->Val->FP;
                F->ParamVal[pi] = u.l;
#else
                F->ParamVal[pi] = (long long)PV->Val->FP;
#endif
            }
#endif
            else
                F->ParamVal[pi] = IrValueAsInt64(PV);
        }
    }

    {
        IrStmt *Body = (IrStmt *)FuncValue->Val->FuncDef.BodyIr;
        int GotoGuard = 0;
        IrGotoTarget = NULL;
        /* Plan §2: body-level VM when EXEC=vm and integer-eligible; else tree. */
        if (!IrTryExecBodyVm(Parser, Body))
        {
            BabycTreeBodyRuns++;
            IrVmStrictTreeFallback(Parser, "body");
            IrExecuteStmt(Parser, Body);
        }
        /* Goto within Body IR: rescan from function root until label is found.
         * Mode may have been clobbered to Return by a skipped return path -
         * restore Goto if a target is still pending. Clear HasReturn so the
         * block walker does not stop early on rewalk. */
        if (IrGotoTarget != NULL) {
            Parser->Mode = RunModeGoto;
            if (IrCallDepth > 0)
                IrCallStack[IrCallDepth-1]->HasReturn = 0;
        }
        while (Parser->Mode == RunModeGoto && IrGotoTarget != NULL && GotoGuard++ < 64)
            IrExecuteStmt(Parser, Body);
        if (Parser->Mode == RunModeGoto)
            ProgramFail(Parser, "label '%s' not found", IrGotoTarget ? IrGotoTarget : "?");
        IrGotoTarget = NULL;
        IrLowerGotoFinished(Parser);
    }
    /* Nested call may have left Mode=Return incorrectly (pre-fix). Clear it
     * unless THIS frame returned. */
    if (Parser->Mode == RunModeReturn && !F->HasReturn)
        Parser->Mode = RunModeRun;
    /* FnCode/Body-IR return: always publish into token ReturnValue.
     * FnCodeRun sets Mode=Return + F->ReturnVal but does not fill the
     * token stack ReturnValue - top-level callers would otherwise see 0. */
    if (F->HasReturn
        && Parser->pc->TopStackFrame != NULL
        && Parser->pc->TopStackFrame->ReturnValue != NULL
        && Parser->pc->TopStackFrame->ReturnValue->Typ != NULL
        && Parser->pc->TopStackFrame->ReturnValue->Typ->Base != TypeVoid)
    {
        struct Value *RV = Parser->pc->TopStackFrame->ReturnValue;
        long long V = F->ReturnVal;
        /* Scalars (incl. double: was missing, so a token-path caller of a
         * Body-IR double function read the IEEE bits as (int) → 0.00) via the
         * shared IrValueStoreBits; arrays decay to their address. */
        if (RV->Typ->Base == TypeArray)
            RV->Val->Pointer = (void *)(intptr_t)V;
        else if (!IrValueStoreBits(RV, RV->Typ->Base, V))
        {
            /* struct/union: V is pointer to stable callee blob (IrStructRetBufIR). */
            if (V != 0 && RV->Val != NULL)
            {
                int sz = TypeSizeValue(RV, FALSE);
                if (sz <= 0 && RV->Typ != NULL)
                    sz = RV->Typ->Sizeof;
                if (sz > 0)
                    memcpy((void *)RV->Val, (void *)(intptr_t)V, (size_t)sz);
            }
        }
        RV->IsInitialized = TRUE;
        Parser->Mode = RunModeReturn;
    }
    IrFrameFreeOwned(F);
    IrFramePop();
    Parser->SourceText = SavedSource;
    Parser->FileName = SavedFile;
    Parser->Line = SavedLine;
    Parser->CharacterPos = SavedCharPos;
    Parser->Pos = SavedTokPos;
}


void IrExecuteFunction(struct ParseState *Parser, struct Value *FuncValue,
                       struct Value **ParamArray, int ArgCount)
{
    IrExecuteFunctionBody(Parser, FuncValue, ParamArray, ArgCount);
}


void IrExecuteFunctionBodyNoArgs(struct ParseState *Parser, struct Value *FuncValue)
{
    IrExecuteFunctionBody(Parser, FuncValue, NULL, 0);
}


/* ---- CallUserFunc (D1 post: from ir_exec) ---- */
long long IrCallUserFuncCached(struct ParseState *Parser, struct Value *FuncValue,
                                      long long *ArgVals, struct ValueType **ArgTypes, int ArgCount)
{
    long long Result = 0;
    enum RunMode SavedMode = Parser->Mode;
    int Count;

    if (FuncValue == NULL || FuncValue->Typ->Base != TypeFunction)
        ProgramFail(Parser, "not a function");
    if (FuncValue->Val->FuncDef.Intrinsic != NULL)
        ProgramFail(Parser, "IR call of intrinsic not supported");
    if (ArgCount < FuncValue->Val->FuncDef.NumParams)
        ProgramFail(Parser, "not enough arguments");
    if (!FuncValue->Val->FuncDef.VarArgs
        && ArgCount > FuncValue->Val->FuncDef.NumParams)
        ProgramFail(Parser, "too many arguments to function");
    if (FuncValue->Val->FuncDef.VarArgs
        && ArgCount > FuncValue->Val->FuncDef.NumParams + 32)
        ProgramFail(Parser, "too many arguments to function (max %d varargs)", 32);

    {
    IrFuncMeta *Meta = IrFuncMetaGet(FuncValue);
        /* Variadic calls need the full argument vector and a real per-depth
         * frame. Skip scalar leaf/kernel/memo shortcuts; the generic shared
         * frame path preserves the complete ABI without copying VM state. */
        if (FuncValue->Val->FuncDef.VarArgs) Meta = NULL;
        if (Meta != NULL && !Meta->TplReady
            && FuncValue->Val->FuncDef.BodyIr != NULL)
            IrMetaSeedFromBody(Meta, FuncValue->Val->FuncDef.BodyIr);
int np = FuncValue->Val->FuncDef.NumParams;
    int memo_on = 0;
    long long memo_hit = 0;
    int np_m = np;

    /* Discover leaf once */
    if (Meta != NULL && Meta->IsLeaf < 0 && FuncValue->Val->FuncDef.BodyIr != NULL)
    {
        IrExpr *LE = NULL;
        IrStmt *LS = NULL;
        if (IrIsLeafReturn((IrStmt *)FuncValue->Val->FuncDef.BodyIr, &LE, &LS))
        {
            Meta->IsLeaf = 1;
            Meta->LeafExpr = LE;
            Meta->LeafStmt = LS;
        }
        else
            Meta->IsLeaf = 0;
    }

    /* Leaf fast path: skip mono + memo body walks */
    if (Meta != NULL && Meta->IsLeaf == 1
        /* A direct indexed return may read through a pointer parameter into
         * a caller-owned partially initialized object.  The leaf shortcut
         * has no caller-alias initialization check, so preserve the ordinary
         * expression/body path for this memory-observing case. */
        && !(Meta->LeafExpr != NULL && Meta->LeafExpr->Kind == IrExIndex)
        && !IrFuncUsesFP(FuncValue)
        && FuncValue->Val->FuncDef.BodyIr != NULL
        && np <= IR_CALL_MAX_PARAMS
        && ArgCount >= np
        && IrCallDepth < IR_CALL_MAX_DEPTH)
    {
        IrCallFrame *F = IrFramePush(Parser);
        if (F == NULL)
            ProgramFail(Parser, "IR call stack overflow");
        IrDebugBind(FuncValue, Meta, NULL);
        F->Arena = NULL;
        F->ArenaUsed = 0;
        F->ArenaCap = 0;
        IrExpr *LeafRet = Meta->LeafExpr;
        IrStmt *LeafStmt = Meta->LeafStmt;
        const char *LeafSrc;
        char *LeafFile;
        int LeafLine, LeafChar;
        F->NumParams = np;
        if (FuncValue->Val->FuncDef.VarArgs)
            IrCallAbiBindSlots(F, ArgVals, ArgTypes, ArgCount, FuncValue->Val->FuncDef.ParamType, FuncValue->Val->FuncDef.NumParams);
        else
            F->CallAbiArguments = NULL;
        F->CallAbiArgumentCount = ArgCount;
        F->CallAbiFixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
        F->CallAbiIsVariadic = FuncValue->Val->FuncDef.VarArgs;
        IrFrameSetLocalExtent(F, 0);
        F->CompactClear = 1; /* leaf: no cold meta */
        F->HasReturn = 0;
        F->ReturnVal = 0;
        for (Count = 0; Count < np; Count++)
        {
            struct ValueType *PT = NULL;
            const char *PName = NULL;
            if (FuncValue->Val->FuncDef.ParamType != NULL)
                PT = FuncValue->Val->FuncDef.ParamType[Count];
            if (FuncValue->Val->FuncDef.ParamName != NULL)
                PName = FuncValue->Val->FuncDef.ParamName[Count];
            /* P0: unified ArgVals param meta + value */
            IrStorageDescBindParamArg(F, Count, PT, PName, ArgVals[Count]);
        }
        Parser->Mode = RunModeRun;
        LeafSrc = Parser->SourceText;
        LeafFile = Parser->FileName;
        LeafLine = Parser->Line;
        LeafChar = Parser->CharacterPos;
        if (FuncValue->Val->FuncDef.Body.SourceText != NULL)
            Parser->SourceText = FuncValue->Val->FuncDef.Body.SourceText;
        if (FuncValue->Val->FuncDef.Body.FileName != NULL)
            Parser->FileName = FuncValue->Val->FuncDef.Body.FileName;
        if (LeafStmt != NULL && LeafStmt->SourceLine > 0)
            Parser->Line = LeafStmt->SourceLine;
        else if (LeafRet != NULL && LeafRet->SourceLine > 0)
            Parser->Line = LeafRet->SourceLine;
        if (LeafRet != NULL && LeafRet->Kind == IrExBinOp
            && LeafRet->Left != NULL && LeafRet->Right != NULL
            && LeafRet->Left->Kind == IrExIdent
            && LeafRet->Right->Kind == IrExIdent
            && LeafRet->Left->ParamSlot >= 0
            && LeafRet->Right->ParamSlot >= 0
            && LeafRet->Left->ParamSlot < np
            && LeafRet->Right->ParamSlot < np)
        {
            Result = IrApplyBinOp(Parser, LeafRet->Op,
                F->ParamVal[LeafRet->Left->ParamSlot],
                F->ParamVal[LeafRet->Right->ParamSlot],
                LeafRet);
        }
        else if (LeafRet != NULL && LeafRet->Kind == IrExIdent
                 && LeafRet->ParamSlot >= 0 && LeafRet->ParamSlot < np)
            Result = F->ParamVal[LeafRet->ParamSlot];
        else if (LeafRet != NULL && LeafRet->Kind == IrExConst)
            Result = LeafRet->ConstVal;
        else if (LeafRet != NULL && LeafRet->Kind == IrExUnary
                 && LeafRet->Left != NULL
                 && LeafRet->Left->Kind == IrExIdent
                 && LeafRet->Left->ParamSlot >= 0
                 && LeafRet->Left->ParamSlot < np
                 && !F->ParamIsPtr[LeafRet->Left->ParamSlot])
            Result = IrApplyUnary(LeafRet->Op,
                F->ParamVal[LeafRet->Left->ParamSlot]);
        else if (LeafRet != NULL)
            Result = IrEvalExpr(Parser, LeafRet);
        else
            Result = 0;
        Parser->SourceText = LeafSrc;
        Parser->FileName = LeafFile;
        Parser->Line = LeafLine;
        Parser->CharacterPos = LeafChar;
        IrFramePop();
        Parser->Mode = SavedMode;
        return Result;
    }

/* Thin kernel body: whole function is kernel-safe (scalar decls, no nested
     * calls, global scalar/array index ok). Avoids Body-IR tree walk + scopes. */
    if (FuncValue->Val->FuncDef.BodyIr != NULL
        && !IrFuncUsesFP(FuncValue)
        && np <= IR_CALL_MAX_PARAMS
        && IrCallDepth < IR_CALL_MAX_DEPTH
        && Meta != NULL)
    {
        IrStmt *TBody = (IrStmt *)FuncValue->Val->FuncDef.BodyIr;
        if (Meta->IsThinKernel < 0)
        {
            int ok = IrKernelStmtOk(TBody, 0);
            /* Reject global side-effects for thin path: IrKernelExecStmt global
             * coverage is incomplete vs Body-IR (see 662_fp_thin_if_call). */
            if (ok)
            {
                const char *Lnames[64];
                int nl = IrCollectLocals(TBody, Lnames, 64, 0);
                if (IrBodyTouchesGlobal(Parser, TBody, FuncValue, Lnames, nl))
                    ok = 0;
            }
            /* Pointer formals allowed only if pointee is scalar (out-param / T* index).
             * Nested pointer, array, struct, function formals stay Body-IR. */
            if (ok && FuncValue->Val->FuncDef.ParamType != NULL)
            {
                int pi;
                for (pi = 0; pi < np; pi++)
                {
                    struct ValueType *PT = FuncValue->Val->FuncDef.ParamType[pi];
                    struct ValueType *Ft;
                    if (PT == NULL) continue;
                    if (PT->Base == TypeArray || PT->Base == TypeStruct
                        || PT->Base == TypeUnion || PT->Base == TypeFunction)
                    { ok = 0; break; }
                    if (PT->Base != TypePointer) continue;
                    Ft = PT->FromType;
                    if (Ft == NULL
                        || Ft->Base == TypePointer || Ft->Base == TypeArray
                        || Ft->Base == TypeStruct || Ft->Base == TypeUnion
                        || Ft->Base == TypeFunction)
                    { ok = 0; break; }
                    /* scalar pointee: char/short/int/long/fp OK for *p and a[i] */
                }
            }
            Meta->IsThinKernel = ok ? 1 : 0;
        }
        if (Meta->IsThinKernel == 1)
        {
            IrCallFrame *F = IrFramePush(Parser);
            if (F == NULL)
                ProgramFail(Parser, "IR call stack overflow");
            IrDebugBind(FuncValue, Meta, NULL);
            int flow = 0, ti;
            F->Arena = NULL;
            F->ArenaUsed = 0;
            F->ArenaCap = 0;
            F->NumParams = np;
            if (FuncValue->Val->FuncDef.VarArgs)
                IrCallAbiBindSlots(F, ArgVals, ArgTypes, ArgCount, FuncValue->Val->FuncDef.ParamType, FuncValue->Val->FuncDef.NumParams);
            else
                F->CallAbiArguments = NULL;
            F->CallAbiArgumentCount = ArgCount;
            F->CallAbiFixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
        F->CallAbiIsVariadic = FuncValue->Val->FuncDef.VarArgs;
            IrFrameSetLocalExtent(F, 0);
            F->HasReturn = 0;
            F->ReturnVal = 0;
            for (Count = 0; Count < np; Count++)
            {
                struct ValueType *PT = NULL;
                const char *PName = NULL;
                if (FuncValue->Val->FuncDef.ParamType != NULL)
                    PT = FuncValue->Val->FuncDef.ParamType[Count];
                if (FuncValue->Val->FuncDef.ParamName != NULL)
                    PName = FuncValue->Val->FuncDef.ParamName[Count];
                IrStorageDescBindParamArg(F, Count, PT, PName, ArgVals[Count]);
            }
            /* Pre-clear scalar template slots if known */
            if (Meta->TplReady
                && Meta->TplNLocals >= 0 && Meta->TplNLocals <= IR_CALL_MAX_LOCALS)
            {
                for (ti = 0; ti < Meta->TplNLocals; ti++)
                {
                    F->LocalVal[ti] = 0;
                    F->LocalInit[ti] = 0;
                    IrFrameSetLocalOwned(F, ti, 0);
                    F->LocalInitMap[ti] = NULL;
                    F->LocalArraySize[ti] = 0;
                    F->LocalIsPtr[ti] = 0;
                    F->LocalIsFptr[ti] = 0;
                    F->LocalElemIsPtr[ti] = 0;
                }
                IrFrameSetLocalExtent(F, Meta->TplNLocals);
                F->CompactClear = 1;
            }
            Parser->Mode = RunModeRun;
            IrKernelGCacheReset();
            if (IrKernelExecStmt(Parser, TBody, F, &flow))
            {
                Result = F->HasReturn ? F->ReturnVal : 0;
                /* Capture template once */
                if (!Meta->TplReady && F->NumLocals > 0
                    && F->NumLocals <= IR_CALL_MAX_LOCALS && F->Arena == NULL)
                {
                    int ok = 1;
                    for (ti = 0; ti < F->NumLocals; ti++)
                    {
                        if (F->LocalOwned[ti] || F->LocalInitMap[ti] != NULL
                            || F->LocalArraySize[ti] > 0)
                        { ok = 0; break; }
                    }
                    if (ok)
                    {
                        Meta->TplNLocals = (short)F->NumLocals;
                        for (ti = 0; ti < F->NumLocals; ti++)
                        {
                            unsigned char k = 1, w = 4;
                            if (F->LocalIsFptr[ti] || F->LocalIsPtr[ti]
                                || F->LocalElemIsPtr[ti])
                            { k = 2; w = (unsigned char)sizeof(void *); }
                            else if (F->LocalFromType[ti] != NULL
                                     && F->LocalFromType[ti]->Base == TypeFP)
                            { k = 3; w = (unsigned char)sizeof(double); }
                            else if (F->LocalElemSize[ti] == 1) w = 1;
                            else if (F->LocalElemSize[ti] == 2) w = 2;
                            else if (F->LocalElemSize[ti] == 8) w = 8;
                            Meta->SlotKind[ti] = k;
                            Meta->SlotWidth[ti] = w;
                        }
                        Meta->TplReady = 1;
                    }
                }
                IrFrameFreeOwned(F);
                IrFramePop();
                Parser->Mode = SavedMode;
                g_fp_thin++;
                return Result;
            }
            /* Kernel body failed unexpectedly - fall back (rare) */
            IrFrameFreeOwned(F);
            IrFramePop();
            Parser->Mode = SavedMode;
            Meta->IsThinKernel = 0;
        }
    }

    
    /* Mono eligibility is a property of the function body, not the call.
     * Cache the structural walk in FuncMeta: repeated calls in tight loops
     * previously re-walked the same Body-IR on every invocation. */
    if (Meta != NULL && FuncValue->Val->FuncDef.BodyIr != NULL
        && np >= 1 && np <= 2 && ArgCount >= np)
    {
        if (Meta->MonoElig < 0)
            Meta->MonoElig = (!IrFuncUsesFP(FuncValue) && IrMonoIsEligible(FuncValue->Val->FuncDef.BodyIr, np)) ? 1 : 0;
        if (Meta->MonoElig == 1)
        {
            long long P[8];
            int mi;
            for (mi = 0; mi < np && mi < 8; mi++)
                P[mi] = ArgVals[mi];
            return IrMonoRun(FuncValue, P, np);
        }
    }

    /* Memo: cache eligibility (body purity walk is expensive) */
    if (Meta != NULL)
    {
        if (Meta->MemoElig < 0)
            Meta->MemoElig = (!IrFuncUsesFP(FuncValue) && IrMemoEligible(FuncValue, ArgCount)) ? 1 : 0;
        if (Meta->MemoElig == 1)
        {
            if (IrMemoLookup(FuncValue, ArgVals, np_m, &memo_hit))
                return memo_hit;
            memo_on = 1;
        }
    }

    if (FuncValue->Val->FuncDef.BodyIr != NULL
        && FuncValue->Val->FuncDef.NumParams <= IR_CALL_MAX_PARAMS
        && IrCallDepth < IR_CALL_MAX_DEPTH)
    {
        IrCallFrame *F = IrFramePush(Parser);
        if (F == NULL)
            ProgramFail(Parser, "IR call stack overflow");
        IrDebugBind(FuncValue, Meta, NULL);
        IrStmt *Body = (IrStmt *)FuncValue->Val->FuncDef.BodyIr;
        F->Arena = NULL;
        F->ArenaUsed = 0;
        F->ArenaCap = 0;
        F->NumParams = np;
        if (FuncValue->Val->FuncDef.VarArgs)
            IrCallAbiBindSlots(F, ArgVals, ArgTypes, ArgCount, FuncValue->Val->FuncDef.ParamType, FuncValue->Val->FuncDef.NumParams);
        else
            F->CallAbiArguments = NULL;
        F->CallAbiArgumentCount = ArgCount;
        F->CallAbiFixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
        F->CallAbiIsVariadic = FuncValue->Val->FuncDef.VarArgs;
        IrFrameSetLocalExtent(F, 0);
        F->HasReturn = 0;
        F->ReturnVal = 0;
        /* Generic: reuse Tpl only for pure scalar-int frames (no ptr/array).
         * Ptr/struct locals still need Decl path for names/types. */
        if (Meta != NULL && !Meta->TplReady && FuncValue->Val->FuncDef.BodyIr)
            IrMetaSeedFromBody(Meta, FuncValue->Val->FuncDef.BodyIr);
        if (Meta != NULL && Meta->TplReady
            && Meta->TplNLocals >= 0 && Meta->TplNLocals <= IR_CALL_MAX_LOCALS)
        {
            int ti, pure = 1;
            for (ti = 0; ti < Meta->TplNLocals; ti++)
                if (Meta->SlotKind[ti] != IR_FRAME_SLOT_INT)
                { pure = 0; break; }
            if (pure)
            {
                IrFrameSetLocalExtent(F, Meta->TplNLocals);
                F->CompactClear = 1;
                for (ti = 0; ti < Meta->TplNLocals; ti++)
                {
                    F->LocalVal[ti] = 0;
                    F->LocalInit[ti] = 0;
                    IrFrameSetLocalOwned(F, ti, 0);
                    F->LocalInitMap[ti] = NULL;
                    F->LocalArraySize[ti] = 0;
                    F->LocalIsPtr[ti] = 0;
                    F->LocalIsFptr[ti] = 0;
                    F->LocalElemIsPtr[ti] = 0;
                }
            }
        }
        for (Count = 0; Count < np; Count++)
        {
            struct ValueType *PT = NULL;
            const char *PName = NULL;
            if (FuncValue->Val->FuncDef.ParamType != NULL)
                PT = FuncValue->Val->FuncDef.ParamType[Count];
            if (FuncValue->Val->FuncDef.ParamName != NULL)
                PName = FuncValue->Val->FuncDef.ParamName[Count];
            IrStorageDescBindParamArg(F, Count, PT, PName, ArgVals[Count]);
        }
        Parser->Mode = RunModeRun;

        /* Plan §2: body-level VM when EXEC=vm and integer-eligible.
         * Same gate as IrExecuteFunctionBody - covers the recursive IR
         * hot path (fib_ptr / call_loop) that never goes through the
         * token entry IrExecuteFunctionBody. */
        if (!(IrExecIsVmProd() && Meta != NULL && Meta->VmTreeSkip
              && BabycLinExecDepth == 0)
            && IrTryExecBodyVm(Parser, Body))
        {
            Result = F->HasReturn ? F->ReturnVal : 0;
        }
        else if (Body->Kind == IrStBlock && Body->StmtCount == 2
            && Body->Stmts[0]->Kind == IrStIf
            && Body->Stmts[1]->Kind == IrStReturn)
        {
            IrStmt *IfS = Body->Stmts[0];
            IrStmt *RetS = Body->Stmts[1];
            if (IrEvalExpr(Parser, IfS->Expr))
            {
                IrStmt *Br = IfS->ThenStmt;
                if (Br && Br->Kind == IrStReturn)
                    F->ReturnVal = Br->Expr ? IrEvalExpr(Parser, Br->Expr) : 0;
                else if (Br)
                    IrExecuteStmt(Parser, Br);
            }
            else
            {
                F->ReturnVal = RetS->Expr ? IrEvalExpr(Parser, RetS->Expr) : 0;
            }
            F->HasReturn = 1;
            Result = F->ReturnVal;
        }
        else
        {
            IrExecuteStmt(Parser, Body);
            Result = F->HasReturn ? F->ReturnVal : 0;
        }
        if (Meta != NULL && !Meta->TplReady && F->NumLocals > 0
            && F->NumLocals <= IR_CALL_MAX_LOCALS && F->Arena == NULL)
        {
            int ti, ok = 1;
            for (ti = 0; ti < F->NumLocals; ti++)
                if (F->LocalOwned[ti] || F->LocalInitMap[ti] != NULL || F->LocalArraySize[ti] > 0)
                { ok = 0; break; }
            if (ok)
            {
                Meta->TplNLocals = (short)F->NumLocals;
                for (ti = 0; ti < F->NumLocals; ti++)
                {
                    unsigned char k = 1, w = 4;
                    if (F->LocalIsFptr[ti] || F->LocalIsPtr[ti] || F->LocalElemIsPtr[ti])
                    { k = 2; w = (unsigned char)sizeof(void *); }
                    else if (F->LocalFromType[ti] != NULL && F->LocalFromType[ti]->Base == TypeFP)
                    { k = 3; w = (unsigned char)sizeof(double); }
                    else if (F->LocalElemSize[ti] == 1) w = 1;
                    else if (F->LocalElemSize[ti] == 2) w = 2;
                    else if (F->LocalElemSize[ti] == 8) w = 8;
                    Meta->SlotKind[ti] = k;
                    Meta->SlotWidth[ti] = w;
                }
                Meta->TplReady = 1;
            }
        }
        IrFrameFreeOwned(F);
        IrFramePop();
        Parser->Mode = SavedMode;
        /* Nested return must not leave outer Mode=Return after the call. */
        if (Parser->Mode == RunModeReturn && SavedMode == RunModeRun)
            Parser->Mode = RunModeRun;
        if (memo_on)
            IrMemoStore(FuncValue, ArgVals, np_m, Result);
        return Result;
    }

    } /* memo_on scope */

    /* Fallback: token body (or BodyIr with > max IR params) */
    {
        struct Value *ReturnValue;
        struct Value **ParamArray;
        struct ParseState FuncParser;
        int OldScopeID;
        int SavedIrDepth;
        HeapPushStackFrame(Parser->pc);
        ReturnValue = IrPackReturnValue(Parser, FuncValue->Val->FuncDef.ReturnType); /* B.3 */
        ParamArray = HeapAllocStack(Parser->pc, sizeof(struct Value *) *
            (FuncValue->Val->FuncDef.NumParams + 32));
        if (ParamArray == NULL)
            ProgramFail(Parser, "stack overflow (function call frame)");
        for (Count = 0; Count < FuncValue->Val->FuncDef.NumParams; Count++)
        {
            struct ValueType *PTyp = FuncValue->Val->FuncDef.ParamType[Count];
            int psz = (int)sizeof(union AnyValue);
            if (PTyp != NULL && PTyp->Sizeof > psz)
                psz = PTyp->Sizeof;
            /* B.3: IrPackAlloc instead of VariableAllocValueFromType */
            ParamArray[Count] = IrPackAlloc(Parser, PTyp, psz);
            ParamArray[Count]->IsLValue = TRUE;
            /* Pointer args: ExpressionAssignInt ignores TypePointer (no-op) */
            if (PTyp != NULL && (PTyp->Base == TypePointer || PTyp->Base == TypeArray))
                ParamArray[Count]->Val->Pointer = (void *)(intptr_t)ArgVals[Count];
#ifndef NO_FP
            else if (PTyp != NULL && PTyp->Base == TypeFP)
            {
                union { long long l; double d; } u;
                u.l = ArgVals[Count];
                ParamArray[Count]->Val->FP = u.d;
            }
#endif
            else
                ExpressionAssignInt(Parser, ParamArray[Count],
                    (PTR_TYPE_INT_SIGNED)ArgVals[Count], FALSE);
            ParamArray[Count]->IsInitialized = TRUE;
        }
        /* Preserve the complete variadic call ABI for stdarg consumers.
         * Extras arrive as the shared evaluator's 64-bit payloads; va_arg
         * interprets the payload using its requested promoted type. */
        for (Count = FuncValue->Val->FuncDef.NumParams; Count < ArgCount; Count++)
        {
            ParamArray[Count] = IrPackAlloc(Parser, &Parser->pc->IntType,
                (int)sizeof(union AnyValue));
            if (ParamArray[Count] == NULL)
                ProgramFail(Parser, "out of memory building variadic call frame");
            ParamArray[Count]->Val->LongLongInteger = ArgVals[Count];
            ParamArray[Count]->IsInitialized = TRUE;
        }
        VariableStackFrameAdd(Parser, "?", 0);
        Parser->pc->TopStackFrame->NumParams = ArgCount;
        Parser->pc->TopStackFrame->CallAbi.Arguments = ParamArray;
        Parser->pc->TopStackFrame->CallAbi.ArgumentCount = ArgCount;
        Parser->pc->TopStackFrame->CallAbi.FixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
        Parser->pc->TopStackFrame->CallAbi.IsVariadic = FuncValue->Val->FuncDef.VarArgs;
        Parser->pc->TopStackFrame->ReturnValue = ReturnValue;
        OldScopeID = Parser->ScopeID;
        /* B.1.3 / B.3: Body-IR params on frame; token body binds via VariableBindParams. */
        if (FuncValue->Val->FuncDef.BodyIr == NULL)
            VariableBindParams(Parser->pc, Parser,
                FuncValue->Val->FuncDef.ParamName, ParamArray,
                FuncValue->Val->FuncDef.NumParams);
        Parser->ScopeID = OldScopeID + 1;

        if (FuncValue->Val->FuncDef.BodyIr != NULL)
        {
            Parser->Mode = RunModeRun;
            IrExecuteFunctionBody(Parser, FuncValue, ParamArray,
                FuncValue->Val->FuncDef.NumParams);
        }
        else
        {
            if (FuncValue->Val->FuncDef.Body.Pos == NULL)
                ProgramFail(Parser, DiagnosticMsgFunctionUndefined);
            ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
            FuncParser.Mode = RunModeRun;
            /* Token body must not see caller IR locals (name collisions).
             * Zeroing IrCallDepth lets nested Body-IR reuse frames 0.. and
             * overwrite EVERY outer frame. Snapshot+restore all of them. */
            {
                IrCallFrame *SavedFrames;
                SavedIrDepth = IrCallDepth;
                SavedFrames = IrTokenCallSaveFrames(SavedIrDepth);
                IrCallDepth = 0;
                if (ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                    ProgramFail(&FuncParser, "function body error");
                IrCallDepth = SavedIrDepth;
                IrTokenCallRestoreFrames(SavedFrames, SavedIrDepth);
            }
            Parser->Mode = FuncParser.Mode;
        }
        if (ReturnValue != NULL && ReturnValue->Typ->Base != TypeVoid)
        {
            if (ReturnValue->Typ->Base == TypePointer
                || ReturnValue->Typ->Base == TypeArray)
                Result = (long long)(intptr_t)ReturnValue->Val->Pointer;
            else if ((ReturnValue->Typ->Base == TypeStruct
                      || ReturnValue->Typ->Base == TypeUnion)
                     && ReturnValue->Val != NULL)
            {
                /* struct-by-value return: copy payload to stable buffer so
                 * caller can memcpy after HeapPopStackFrame. */
                static char IrStructRetBuf[512];
                int sz = TypeSizeValue(ReturnValue, FALSE);
                if (sz > 0 && sz <= (int)sizeof(IrStructRetBuf))
                {
                    memcpy(IrStructRetBuf, ReturnValue->Val, (size_t)sz);
                    Result = (long long)(intptr_t)IrStructRetBuf;
                }
                else
                    Result = 0;
            }
            else
                Result = IR_RET_BITS(ReturnValue);
        }
        VariableStackFramePop(Parser);
        HeapPopStackFrame(Parser->pc);
        Parser->Mode = SavedMode;
        return Result;
    }
}

long long IrCallUserFunc(struct ParseState *Parser, const char *Name,
                                long long *ArgVals, struct ValueType **ArgTypes, int ArgCount)
{
    struct Value *FuncValue;
    struct Value *ReturnValue;
    struct Value **ParamArray;
    struct ParseState FuncParser;
    int Count;
    int OldScopeID;
    enum RunMode SavedMode = Parser->Mode;
    long long Result = 0;

    IrVarTableGet(Parser, Name, &FuncValue); /* global/func resolve */
    if (FuncValue->Typ->Base != TypeFunction)
        ProgramFail(Parser, "'%s' is not a function", Name);
    if (FuncValue->Val->FuncDef.Intrinsic != NULL)
        ProgramFail(Parser, "IR call of intrinsic not supported");
    if (ArgCount < FuncValue->Val->FuncDef.NumParams)
        ProgramFail(Parser, "not enough arguments to '%s'", Name);
    if (!FuncValue->Val->FuncDef.VarArgs
        && ArgCount > FuncValue->Val->FuncDef.NumParams)
        ProgramFail(Parser, "too many arguments to '%s'", Name);
    if (FuncValue->Val->FuncDef.VarArgs
        && ArgCount > FuncValue->Val->FuncDef.NumParams + 32)
        ProgramFail(Parser, "too many arguments to '%s' (max %d varargs)", Name, 32);

    /* Fast path: BodyIr + small int params - no VariableDefine / heap frames */
    if (FuncValue->Val->FuncDef.BodyIr != NULL
        && FuncValue->Val->FuncDef.NumParams <= IR_CALL_MAX_PARAMS
        && IrCallDepth < IR_CALL_MAX_DEPTH)
    {
        IrCallFrame *F = IrFramePush(Parser);
        if (F == NULL)
            ProgramFail(Parser, "IR call stack overflow");
        {
            IrFuncMeta *MetaN = IrFuncMetaGet(FuncValue);
            IrDebugBind(FuncValue, MetaN, Name);
        }
        int np = FuncValue->Val->FuncDef.NumParams;
        F->NumParams = np;
        if (FuncValue->Val->FuncDef.VarArgs)
            IrCallAbiBindSlots(F, ArgVals, ArgTypes, ArgCount, FuncValue->Val->FuncDef.ParamType, FuncValue->Val->FuncDef.NumParams);
        else
            F->CallAbiArguments = NULL;
        F->CallAbiArgumentCount = ArgCount;
        F->CallAbiFixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
        F->CallAbiIsVariadic = FuncValue->Val->FuncDef.VarArgs;
        IrFrameSetLocalExtent(F, 0);
        F->HasReturn = 0;
        F->ReturnVal = 0;
        for (Count = 0; Count < np; Count++)
        {
            struct ValueType *PT = FuncValue->Val->FuncDef.ParamType
                ? FuncValue->Val->FuncDef.ParamType[Count] : NULL;
            const char *PName = FuncValue->Val->FuncDef.ParamName
                ? FuncValue->Val->FuncDef.ParamName[Count] : NULL;
            IrStorageDescBindParamArg(F, Count, PT, PName, ArgVals[Count]);
        }
        if (Parser->Mode != RunModeRun)
            Parser->Mode = RunModeRun;
        {
            IrStmt *Body = (IrStmt *)FuncValue->Val->FuncDef.BodyIr;
            int GotoGuard = 0;
            IrGotoTarget = NULL;
            IrExecuteStmt(Parser, Body);
            if (IrGotoTarget != NULL) {
                Parser->Mode = RunModeGoto;
                F->HasReturn = 0;
            }
            while (Parser->Mode == RunModeGoto && IrGotoTarget != NULL && GotoGuard++ < 64)
                IrExecuteStmt(Parser, Body);
            if (Parser->Mode == RunModeGoto)
                ProgramFail(Parser, "label '%s' not found", IrGotoTarget ? IrGotoTarget : "?");
            IrGotoTarget = NULL;
        }
        Result = F->HasReturn ? F->ReturnVal : 0;
        IrFramePop();
        if (SavedMode != RunModeRun)
            Parser->Mode = SavedMode;
        return Result;
    }

    HeapPushStackFrame(Parser->pc);
    ReturnValue = IrPackReturnValue(Parser, FuncValue->Val->FuncDef.ReturnType); /* B.3 */
    ParamArray = HeapAllocStack(Parser->pc, sizeof(struct Value *) * (FuncValue->Val->FuncDef.NumParams + 4));
    if (ParamArray == NULL)
        ProgramFail(Parser, "stack overflow (function call frame)");

    for (Count = 0; Count < FuncValue->Val->FuncDef.NumParams; Count++)
    {
        struct ValueType *PTyp = FuncValue->Val->FuncDef.ParamType[Count];
        int psz = (int)sizeof(union AnyValue);
        if (PTyp != NULL && PTyp->Sizeof > psz)
            psz = PTyp->Sizeof;
        /* B.3: IrPackAlloc instead of VariableAllocValueFromType */
        ParamArray[Count] = IrPackAlloc(Parser, PTyp, psz);
        ParamArray[Count]->IsLValue = TRUE;
        if (PTyp != NULL && (PTyp->Base == TypePointer || PTyp->Base == TypeArray))
            ParamArray[Count]->Val->Pointer = (void *)(intptr_t)ArgVals[Count];
#ifndef NO_FP
        else if (PTyp != NULL && PTyp->Base == TypeFP)
        {
            union { long long l; double d; } u;
            u.l = ArgVals[Count];
            ParamArray[Count]->Val->FP = u.d;
        }
#endif
        else
            ExpressionAssignInt(Parser, ParamArray[Count], (PTR_TYPE_INT_SIGNED)ArgVals[Count], FALSE);
        ParamArray[Count]->IsInitialized = TRUE;
    }

    VariableStackFrameAdd(Parser, Name, 0);
    Parser->pc->TopStackFrame->NumParams = ArgCount;
    Parser->pc->TopStackFrame->ReturnValue = ReturnValue;
    OldScopeID = Parser->ScopeID;
    /* B.1.3: Body-IR params on frame; token body binds via VariableBindParams. */
    if (FuncValue->Val->FuncDef.BodyIr == NULL)
        VariableBindParams(Parser->pc, Parser,
            FuncValue->Val->FuncDef.ParamName, ParamArray,
            FuncValue->Val->FuncDef.NumParams);
    Parser->ScopeID = OldScopeID + 1;

    if (FuncValue->Val->FuncDef.BodyIr != NULL)
    {
        Parser->Mode = RunModeRun;
        IrExecuteFunctionBody(Parser, FuncValue, ParamArray,
            FuncValue->Val->FuncDef.NumParams);
    }
    else
    {
        int SavedIrDepth;
        if (FuncValue->Val->FuncDef.Body.Pos == NULL)
            ProgramFail(Parser, DiagnosticMsgFunctionUndefined);
        ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
        FuncParser.Mode = RunModeRun;
        /* Token body must not see the caller's IR locals (same names, e.g. i).
         * Snapshot all outer IR frames across IrCallDepth=0 (nested Body-IR
         * would otherwise overwrite main/intermediate HasReturn). */
        {
            IrCallFrame *SavedFrames;
            SavedIrDepth = IrCallDepth;
            SavedFrames = IrTokenCallSaveFrames(SavedIrDepth);
            IrCallDepth = 0;
            if (ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                ProgramFail(&FuncParser, "function body error");
            IrCallDepth = SavedIrDepth;
            IrTokenCallRestoreFrames(SavedFrames, SavedIrDepth);
        }
        Parser->Mode = FuncParser.Mode;
    }

    if (ReturnValue != NULL && ReturnValue->Typ->Base != TypeVoid)
        Result = IR_RET_BITS(ReturnValue);

    VariableStackFramePop(Parser);
    HeapPopStackFrame(Parser->pc);
    Parser->Mode = SavedMode;
    return Result;
}

