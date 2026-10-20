#ifdef _MSC_VER
#include <malloc.h>
#ifndef alloca
#define alloca _alloca
#endif
#else
#include <alloca.h>
#endif
/* ir_eval.c — Body-IR expression evaluator (D1 step 6 extract from ir_exec.c) */
#include "picoc.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ir_runtime.h"
#include "ir_bridge.h"

/* Portable IR expr dispatch (default ON all platforms).
 * - GCC/Clang: computed goto (labels-as-values)
 * - MSVC / others: dense switch (compiler emits jump table at /O2|/Ox)
 * Disable: -DBABYC_NO_DISPATCH_TABLE
 */
#if !defined(BABYC_NO_DISPATCH_TABLE) && (defined(__GNUC__) || defined(__clang__))
# define BABYC_IR_EVAL_CGOTO 1
#else
# define BABYC_IR_EVAL_CGOTO 0
#endif

/* Keep caller array maps precise while evaluating arguments to interpreted calls. */
static int IrEvalPreserveArrayInitOnCallArg;

/* Send an IR-detected runtime error with the source span carried by the
 * member/index node. Do not reconstruct the caret from the root identifier. */
static void IrEvalFailUninitializedMember(struct ParseState *Parser,
                                          IrExpr *Access, IrExpr *Root)
{
    if (Parser != NULL && Access != NULL && Access->CharacterPos > 0
        && Access->SourceLine > 0)
    {
        DiagnosticLocation Location;
        Location.FileName = Parser->FileName;
        Location.SourceText = Parser->SourceText;
        Location.Line = Access->SourceLine;
        Location.Column = Access->CharacterPos;
        ProgramFailAt(Parser->pc, &Location, DiagnosticMsgUninitializedVariable);
    }
    if (Parser != NULL)
    {
        if (Access != NULL && Access->SourceLine > 0) Parser->Line = Access->SourceLine;
        if (Root != NULL && Root->Ident != NULL)
            ParserSetDiagnosticCaret(Parser, Root->Ident, NULL);
    }
    ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
}

/* Tree evaluator pointer-param reads must consult initialization maps in any
 * active owner frame. The Linear helper intentionally gates checks on its own
 * execution mode, so Tree uses this small address-based bridge instead. */
static int IrEvalHasOwnedInitMap(void *Addr)
{
    int di, si;
    uintptr_t p = (uintptr_t)Addr;
    if (Addr == NULL || IrCallDepth <= 0)
        return 0;
    for (di = IrCallDepth - 1; di >= 0; --di)
    {
        IrCallFrame *Fr = IrCallStack[di];
        int nlocals;
        if (Fr == NULL) continue;
        nlocals = IrFrameLocalExtent(Fr);
        for (si = 0; si < nlocals; ++si)
        {
            char *Base;
            int Span = 0;
            struct ValueType *LT;
            uintptr_t b;
            if (!Fr->LocalOwned[si] || Fr->LocalVal[si] == 0
                || Fr->LocalInitMap[si] == NULL)
                continue;
            Base = (char *)(intptr_t)Fr->LocalVal[si];
            b = (uintptr_t)Base;
            LT = Fr->LocalFromType[si] ? Fr->LocalFromType[si] : Fr->LocalTyp[si];
            if (IrFrameIsByteMap(Fr, si))
                Span = IrFrameInitMapLen(Fr, si);
            else if (Fr->LocalArraySize[si] > 0 && Fr->LocalElemSize[si] > 0)
                Span = Fr->LocalArraySize[si] * (int)Fr->LocalElemSize[si];
            else if (LT != NULL && LT->Sizeof > 0)
                Span = (int)LT->Sizeof;
            if (Span > 0 && p >= b && p < b + (uintptr_t)Span)
                return 1;
        }
    }
    return 0;
}

static void IrEvalCheckOwnedPointerRead(struct ParseState *Parser, void *Addr,
                                        int Size, IrExpr *Access)
{
    int di, si;
    uintptr_t p = (uintptr_t)Addr;
    if (Addr == NULL || Size <= 0 || IrCallDepth <= 0)
        return;
    for (di = IrCallDepth - 1; di >= 0; --di)
    {
        IrCallFrame *Fr = IrCallStack[di];
        int nlocals;
        if (Fr == NULL) continue;
        nlocals = IrFrameLocalExtent(Fr);
        for (si = 0; si < nlocals; ++si)
        {
            char *Base;
            int Span = 0;
            struct ValueType *LT;
            uintptr_t b;
            if (!Fr->LocalOwned[si] || Fr->LocalVal[si] == 0
                || Fr->LocalInitMap[si] == NULL)
                continue;
            Base = (char *)(intptr_t)Fr->LocalVal[si];
            b = (uintptr_t)Base;
            LT = Fr->LocalFromType[si] ? Fr->LocalFromType[si] : Fr->LocalTyp[si];
            if (IrFrameIsByteMap(Fr, si))
                Span = IrFrameInitMapLen(Fr, si);
            else if (Fr->LocalArraySize[si] > 0 && Fr->LocalElemSize[si] > 0)
                Span = Fr->LocalArraySize[si] * (int)Fr->LocalElemSize[si];
            else if (LT != NULL && LT->Sizeof > 0)
                Span = (int)LT->Sizeof;
            if (Span <= 0 || p < b || p >= b + (uintptr_t)Span)
                continue;
            if (!IrFrameLeafInit(Fr, si, Addr, Size))
            {
                if (Access != NULL && Access->SourceLine > 0)
                    Parser->Line = Access->SourceLine;
                if (Access != NULL && Access->Left != NULL
                    && Access->Left->Ident != NULL)
                {
                    ParserSetCaretOnIndexAccess(Parser, Access->Left->Ident);
                    /* Match the established Tree/token index-access column. */
                    Parser->CharacterPos++;
                }
                ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
            }
            return;
        }
    }
}

static void IrEvalMarkOwnedPointerWrite(struct ParseState *Parser, void *Addr,
                                        int Size)
{
    int di, si;
    uintptr_t p = (uintptr_t)Addr;
    if (Addr == NULL || Size <= 0 || IrCallDepth <= 0)
        return;
    for (di = IrCallDepth - 1; di >= 0; --di)
    {
        IrCallFrame *Fr = IrCallStack[di];
        int nlocals;
        if (Fr == NULL) continue;
        nlocals = IrFrameLocalExtent(Fr);
        for (si = 0; si < nlocals; ++si)
        {
            char *Base;
            int Span = 0;
            struct ValueType *LT;
            uintptr_t b;
            if (!Fr->LocalOwned[si] || Fr->LocalVal[si] == 0
                || Fr->LocalInitMap[si] == NULL)
                continue;
            Base = (char *)(intptr_t)Fr->LocalVal[si];
            b = (uintptr_t)Base;
            LT = Fr->LocalFromType[si] ? Fr->LocalFromType[si] : Fr->LocalTyp[si];
            if (IrFrameIsByteMap(Fr, si))
                Span = IrFrameInitMapLen(Fr, si);
            else if (Fr->LocalArraySize[si] > 0 && Fr->LocalElemSize[si] > 0)
                Span = Fr->LocalArraySize[si] * (int)Fr->LocalElemSize[si];
            else if (LT != NULL && LT->Sizeof > 0)
                Span = (int)LT->Sizeof;
            if (Span <= 0 || p < b || p >= b + (uintptr_t)Span)
                continue;
            IrFrameMarkBytes(Fr, si, Addr, Size);
            IrShadowMarkAddr(Parser, Fr->LocalName[si], Addr, Size);
            return;
        }
    }
}


/* Resolve each call argument's promoted static type for the variadic IR ABI.
 * This is derived from the canonical expression type, never from function
 * names or va_arg syntax. */
static struct ValueType *IrCallPromotedArgType(struct ParseState *Parser, IrExpr *A)
{
    int B;
    struct ValueType *T = NULL;
    if (A == NULL) return NULL;
    if (!(A->CanonicalFlags & IR_CF_VALID)) IrCanonicalCheckNode(A);
    B = (A->CanonicalFlags & IR_CF_VALID) ? A->CanonicalBase : A->I32;
    if (B == TypeStruct || B == TypeUnion)
        return A->CanonicalType; /* exact aggregate identity (C11 7.16.1.1) */
    if ((A->TypeFlags & IR_TF_PTR) || B == TypePointer || B == TypeArray || B == TypeFunction)
        return Parser->pc->VoidPtrType;
    switch (B)
    {
        case TypeChar: case TypeShort: case TypeUnsignedChar: case TypeUnsignedShort:
        case TypeEnum: T = &Parser->pc->IntType; break;
        case TypeInt: T = &Parser->pc->IntType; break;
        case TypeLong: T = &Parser->pc->LongType; break;
        case TypeLongLong: T = &Parser->pc->LongLongType; break;
        case TypeUnsignedInt: T = &Parser->pc->UnsignedIntType; break;
        case TypeUnsignedLong: T = &Parser->pc->UnsignedLongType; break;
        case TypeUnsignedLongLong: T = &Parser->pc->UnsignedLongLongType; break;
#ifndef NO_FP
        case TypeFP: T = &Parser->pc->FPType; break; /* float is promoted to double */
#endif
        default: break;
    }
    return T;
}

static struct ValueType **IrBuildCallArgTypes(struct ParseState *Parser, IrExpr *E,
    struct ValueType **Out, int Capacity)
{
    int i, n = E != NULL ? E->ArgCount : 0;
    if (n > Capacity) ProgramFail(Parser, "too many call argument types");
    for (i = 0; i < n; ++i) {
        Out[i] = IrCallPromotedArgType(Parser, E->Args[i]);
        if (Out[i] == NULL) ProgramFail(Parser, "unknown static type for call argument");
    }
    return Out;
}

long long IrEvalExprTree(struct ParseState *Parser, IrExpr *E)
{
    if (!(E->CanonicalFlags & IR_CF_VALID)) IrCanonicalCheckNode(E);
    struct Value *Var;
    long long L, R, Args[8], *ArgV;
    struct ValueType *CallArgTypes[IR_CALL_MAX_PARAMS + 32];
    int i;

    if (E == NULL) return 0;
    /* Aggregate lvalue designators (s.m, a[i], *p, global) evaluate to the
     * address of the designated object: the IR's struct/union value is its
     * storage address (see IrAggregateObjectAddr). */
    if (E->CanonicalType != NULL && IrExprDesignatesAggregateObject(E))
    {
        void *AggregateAddr = NULL;
        if (!IrAggregateObjectAddr(Parser, E, &AggregateAddr))
            ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
        return (long long)(intptr_t)AggregateAddr;
    }
    {
        static int FuseOff = -1;
        long long Fv;
        if (FuseOff < 0)
            FuseOff = (getenv("BABYC_NO_FUSE") != NULL) ? 1 : 0;
        /* Do not fuse indexed reads through pointer parameters/locals: the
         * fused evaluator currently has no caller-owned initialization-map
         * check, so it would bypass the semantic diagnostic below. */
        int PointerAliasIndex = 0;
        if (E->Kind == IrExIndex && E->Left != NULL
            && E->Left->Kind == IrExIdent
            && (E->Left->ParamSlot >= 0
                || (E->Left->LocalSlot >= 0 && IrCallDepth > 0
                    && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
                    && IrCallStack[IrCallDepth - 1]->LocalIsPtr[E->Left->LocalSlot]
                    && !IrCallStack[IrCallDepth - 1]->LocalOwned[E->Left->LocalSlot])))
        {
            /* Keep fusion for ordinary pointers. Only bypass it when the
             * target belongs to a tracked automatic object in an active frame. */
            void *Target = NULL;
            if (E->Left->ParamSlot >= 0 && IrCallDepth > 0
                && E->Left->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                Target = (void *)(intptr_t)IrCallStack[IrCallDepth - 1]->ParamVal[E->Left->ParamSlot];
            else if (E->Left->LocalSlot >= 0 && IrCallDepth > 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                Target = (void *)(intptr_t)IrCallStack[IrCallDepth - 1]->LocalVal[E->Left->LocalSlot];
            PointerAliasIndex = IrEvalHasOwnedInitMap(Target);
        }
        if (!FuseOff && !PointerAliasIndex && (E->TypeFlags & IR_TF_FUSABLE)
            && IrTryFuseEval(Parser, E, &Fv))
            return Fv;
    }
#if BABYC_IR_EVAL_CGOTO
    {
        static void *const ir_eval_jt[] = {
            [0] = &&L_IrEx_default,
            [IrExConst] = &&L_IrExConst,
            [IrExIdent] = &&L_IrExIdent,
            [IrExBinOp] = &&L_IrExBinOp,
            [IrExAssign] = &&L_IrExAssign,
            [IrExCall] = &&L_IrExCall,
            [IrExUnary] = &&L_IrExUnary,
            [IrExIndex] = &&L_IrExIndex,
            [IrExMember] = &&L_IrExMember,
            [IrExTernary] = &&L_IrExTernary,
            [IrExVaArg] = &&L_IrExVaArg,
        };
        unsigned k = (unsigned)E->Kind;
        if (k < (unsigned)(sizeof(ir_eval_jt)/sizeof(ir_eval_jt[0])) && ir_eval_jt[k] != 0)
            goto *ir_eval_jt[k];
        goto L_IrEx_default;
    }
# define IR_EVAL_CASE(name) L_##name:
# define IR_EVAL_DEFAULT L_IrEx_default:
#else
    switch (E->Kind)
    {
# define IR_EVAL_CASE(name) case name:
# define IR_EVAL_DEFAULT default:
#endif
        IR_EVAL_CASE(IrExConst)
            return E->ConstVal;
        IR_EVAL_CASE(IrExVaArg)
        {
            extern long long StdargVaArgBits(struct ParseState *, void *, struct ValueType *);
            long long Cursor = IrEvalExpr(Parser, E->Left);
            return StdargVaArgBits(Parser, (void *)(intptr_t)Cursor, E->VaType);
        }
        IR_EVAL_CASE(IrExIdent)
        {
            long long PV;
            if (E->LocalSlot >= 0 && IrCallDepth > 0 &&
                E->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                if (!IrCallStack[IrCallDepth - 1]->LocalInit[E->LocalSlot]
                    && !(E->TypeFlags & IR_TF_DEFINITELY_INIT)
                    && IrSafeChecksOn()
                    /* Owned blob after DECL: object storage exists; field
                     * init is tracked by LocalInitMap, not LocalInit alone. */
                    && !IrCallStack[IrCallDepth - 1]->LocalOwned[E->LocalSlot])
                {
                    if (E->SourceLine)
                        Parser->Line = E->SourceLine;
                    if (E->CharacterPos)
                        Parser->CharacterPos = E->CharacterPos;
                    { ParserSetCaretOnIdent(Parser, E->Ident ? E->Ident : "?"); ProgramFail(Parser, DiagnosticMsgUninitializedVariableNamed, E->Ident ? E->Ident : "?"); };
                }
                if (IrSafeChecksOn() && !IrEvalPreserveArrayInitOnCallArg) {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Fr->LocalIsPtr[E->LocalSlot] && Fr->LocalInitMap[E->LocalSlot]
                        && Fr->LocalArraySize[E->LocalSlot] > 0) {
                        int k;
                        for (k = 0; k < Fr->LocalArraySize[E->LocalSlot]; k++)
                            Fr->LocalInitMap[E->LocalSlot][k] = 1;
                    }
                }
                /* FP scalar: LocalVal already holds IEEE bits */
                return IrCallStack[IrCallDepth - 1]->LocalVal[E->LocalSlot];
            }
            if (E->ParamSlot >= 0 && IrCallDepth > 0)
                return IrCallStack[IrCallDepth - 1]->ParamVal[E->ParamSlot];
            if (IrLookupParam(E->Ident, &PV))
                return PV;
            /* G3/Point4: single CachedVal path via IrGlobalVal */
            Var = IrGlobalVal(Parser, E);
            if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypeFunction)
                return (long long)(intptr_t)Var;
            if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypePointer)
                return (long long)(intptr_t)Var->Val->Pointer;
            /* array name decays to pointer to first element */
            if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypeArray)
                return (long long)(intptr_t)(void *)Var->Val;
#ifndef NO_FP
            if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypeFP)
            {
                union { long long l; double d; } u;
                u.d = Var->Val->FP;
                return u.l;
            }
#endif
            return IrValueAsInt64(Var);
        }
        IR_EVAL_CASE(IrExBinOp)
            if (E->Op == TokenLogicalAnd)
            {
                L = IrEvalExpr(Parser, E->Left);
                if (!L) return 0;
                return IrEvalExpr(Parser, E->Right) ? 1 : 0;
            }
            if (E->Op == TokenLogicalOr)
            {
                L = IrEvalExpr(Parser, E->Left);
                if (L) return 1;
                return IrEvalExpr(Parser, E->Right) ? 1 : 0;
            }
            if (E->Op == TokenComma)
            {
                (void)IrEvalExpr(Parser, E->Left);
                return IrEvalExpr(Parser, E->Right);
            }
            L = IrEvalExpr(Parser, E->Left);
            R = IrEvalExpr(Parser, E->Right);
            /* Scaled pointer arithmetic: p + n / p - n / a + n / p - q / s.p + n */
            if ((E->Op == TokenPlus || E->Op == TokenMinus) && E->Left != NULL)
            {
                int Es = IrPtrElemSizeOf(E->Left);
                int EsR = (E->Right != NULL) ? IrPtrElemSizeOf(E->Right) : 0;
                if (Es <= 0 && E->Left->Kind == IrExMember)
                    Es = IrMemberPtrElemSize(Parser, E->Left);
                if (EsR <= 0 && E->Right != NULL && E->Right->Kind == IrExMember)
                    EsR = IrMemberPtrElemSize(Parser, E->Right);
                /* &a[0] / &a : pointee size from array/pointer Ident */
                if (EsR <= 0 && E->Right != NULL && E->Right->Kind == IrExUnary
                    && E->Right->Op == TokenAmpersand && E->Right->Left != NULL)
                {
                    IrExpr *T = E->Right->Left;
                    while (T != NULL && T->Kind == IrExIndex)
                        T = T->Left;
                    /* B.2.3: frame first for local/param, Get only global */
                    if (EsR <= 0 && T != NULL && T->Kind == IrExIdent
                        && T->LocalSlot >= 0 && IrCallDepth > 0
                        && T->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        int les = IrCallStack[IrCallDepth - 1]->LocalElemSize[T->LocalSlot];
                        if (les > 0) EsR = les;
                    }
                    if (EsR <= 0 && T != NULL && T->Kind == IrExIdent
                        && T->ParamSlot >= 0 && IrCallDepth > 0
                        && T->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                    {
                        struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[T->ParamSlot];
                        if (PT != NULL && PT->FromType != NULL && PT->FromType->Sizeof > 0)
                            EsR = PT->FromType->Sizeof;
                    }
                    if (EsR <= 0 && T != NULL && T->Kind == IrExIdent && T->Ident != NULL
                        && T->LocalSlot < 0 && T->ParamSlot < 0
                        && (IrGlobalValSoft(Parser, T) != NULL))
                    {
                        struct Value *AV = NULL;
                        AV = IrGlobalValSoft(Parser, T); /* soft: already gated */
                        if (AV != NULL && AV->Typ != NULL)
                        {
                            if (AV->Typ->Base == TypeArray && AV->Typ->FromType != NULL)
                                EsR = AV->Typ->FromType->Sizeof;
                            else if (AV->Typ->Base == TypePointer && AV->Typ->FromType != NULL)
                                EsR = AV->Typ->FromType->Sizeof;
                        }
                    }
                }
                /* Share scale for pointer-pointer subtract only when BOTH sides
                 * look like pointers (Ident/Member/Unary&/Index), not p - scalar. */
                if (E->Op == TokenMinus && Es > 0 && EsR <= 0
                    && E->Right != NULL
                    && (E->Right->Kind == IrExIdent
                        || E->Right->Kind == IrExMember
                        || E->Right->Kind == IrExIndex
                        || (E->Right->Kind == IrExUnary && E->Right->Op == TokenAmpersand)))
                    EsR = Es;
                if (E->Op == TokenMinus && EsR > 0 && Es <= 0
                    && E->Left != NULL
                    && (E->Left->Kind == IrExIdent
                        || E->Left->Kind == IrExMember
                        || E->Left->Kind == IrExIndex
                        || (E->Left->Kind == IrExUnary && E->Left->Op == TokenAmpersand)))
                    Es = EsR;
                /* Global array/pointer via VarGet only if Ident is NOT an IR
                 * param/local. Otherwise caller Variable "a" (e.g. Node *a) shadows
                 * callee int a and scales a+b as pointer arith (0+10*16=160). */
                if (Es <= 0 && E->Left->Kind == IrExIdent && E->Left->Ident != NULL
                    && E->Left->ParamSlot < 0 && E->Left->LocalSlot < 0
                    && (IrGlobalValSoft(Parser, E->Left) != NULL))
                {
                    struct Value *AV = NULL;
                    AV = IrGlobalValSoft(Parser, E->Left); /* soft: already gated */
                    if (AV != NULL && AV->Typ != NULL)
                    {
                        if (AV->Typ->Base == TypeArray && AV->Typ->FromType != NULL
                            && AV->Typ->FromType->Sizeof > 0)
                            Es = AV->Typ->FromType->Sizeof;
                        else if (AV->Typ->Base == TypePointer && AV->Typ->FromType != NULL
                                 && AV->Typ->FromType->Sizeof > 0)
                            Es = AV->Typ->FromType->Sizeof;
                    }
                }
                if (EsR <= 0 && E->Right != NULL && E->Right->Kind == IrExIdent
                    && E->Right->Ident != NULL
                    && E->Right->ParamSlot < 0 && E->Right->LocalSlot < 0
                    && (IrGlobalValSoft(Parser, E->Right) != NULL))
                {
                    struct Value *AV = NULL;
                    AV = IrGlobalValSoft(Parser, E->Right); /* soft: already gated */
                    if (AV != NULL && AV->Typ != NULL)
                    {
                        if (AV->Typ->Base == TypeArray && AV->Typ->FromType != NULL
                            && AV->Typ->FromType->Sizeof > 0)
                            EsR = AV->Typ->FromType->Sizeof;
                        else if (AV->Typ->Base == TypePointer && AV->Typ->FromType != NULL
                                 && AV->Typ->FromType->Sizeof > 0)
                            EsR = AV->Typ->FromType->Sizeof;
                    }
                }
                /* pointer - pointer → element count (before p - n scale) */
                if (E->Op == TokenMinus && Es > 0 && EsR > 0 && Es == EsR)
                    return (L - R) / (long long)Es;
                /* A char pointer has element size 1: it still needs the
                 * pointer-arithmetic path. Falling through to integer ops lets
                 * the signed 32-bit compatibility path truncate 64-bit addresses. */
                if (Es > 0 && EsR == 0)
                {
                    long long Scaled = R * (long long)Es;
                    if (E->Op == TokenPlus)
                        return L + Scaled;
                    return L - Scaled;
                }
                /* n + p (scalar + array/pointer), including char* (scale 1). */
                if (EsR > 0 && Es == 0 && E->Op == TokenPlus)
                {
                    long long Scaled = L * (long long)EsR;
                    return Scaled + R;
                }
            }
#ifndef NO_FP
            {
            int FpL = IrExprIsFP(Parser, E->Left);
            int FpR = IrExprIsFP(Parser, E->Right);
            if (FpL || FpR)
            {
                double Ld = FpL ? IrBitsFP(L) : (double)L;
                double Rd = FpR ? IrBitsFP(R) : (double)R;
                switch (E->Op)
                {
                    case TokenPlus: return IrFPBits(Ld + Rd);
                    case TokenMinus: return IrFPBits(Ld - Rd);
                    case TokenAsterisk: return IrFPBits(Ld * Rd);
                    case TokenSlash:
                        if (Rd == 0.0) ProgramFail(Parser, DiagnosticMsgDivisionByZero);
                        return IrFPBits(Ld / Rd);
                    case TokenEqual: return (Ld == Rd) ? 1 : 0;
                    case TokenNotEqual: return (Ld != Rd) ? 1 : 0;
                    case TokenLessThan: return (Ld < Rd) ? 1 : 0;
                    case TokenGreaterThan: return (Ld > Rd) ? 1 : 0;
                    case TokenLessEqual: return (Ld <= Rd) ? 1 : 0;
                    case TokenGreaterEqual: return (Ld >= Rd) ? 1 : 0;
                    default: break;
                }
            }
            }
#endif
            if (E->Op == TokenShiftLeft || E->Op == TokenShiftRight)
            {

                if (E->CanonicalOpFlags & IR_CF_UNSIGNED)
                {
                    int Wide = (E->CanonicalOpFlags & IR_CF_WIDE) != 0;
                    if (E->Op == TokenShiftLeft)
                    {
                        if (Wide)
                            return (long long)((unsigned long long)L << (unsigned)(R & 63));
                        return (long long)((unsigned int)L << (unsigned)(R & 31));
                    }
                    if (Wide)
                        return (long long)((unsigned long long)L >> (unsigned)(R & 63));
                    return (long long)((unsigned int)L >> (unsigned)(R & 31));
                }
            }
            {
                long long Rslt = IrApplyBinOp(Parser, E->Op, L, R, E);
                /* Narrow unsigned results to declared width (not ull). */
                if ((E->CanonicalFlags & IR_CF_UNSIGNED)
                    && !(E->CanonicalFlags & IR_CF_PTR))
                {
                    int w = (E->CanonicalFlags & IR_CF_WIDE)
                        ? (BABYC_VALUE_BITS_LLONG / 8)
                        : (E->ValWidth > 0 ? E->ValWidth : 4);
                    if (w == 1) Rslt = (long long)(unsigned char)Rslt;
                    else if (w == 2) Rslt = (long long)(unsigned short)Rslt;
                    else if (w == 4) Rslt = (long long)(unsigned int)Rslt;
                    /* w==8: leave as ull */
                }
                return Rslt;
            }
        IR_EVAL_CASE(IrExMember)
        {
            void *Addr = NULL;
            int Eb = TypeInt, Es = 1;
            /* IrResolveMember first: sets BitOffset/BitWidth for bitfields.
             * IrResolvePtr alone left BitWidth=0 → wrong full-word load. */
            if (IrResolveMember(Parser, E, &Addr, &Eb))
            {
                /* uninit member use — skip for array members (decay / index base).
                 * Element uninit is checked at Index load instead. */
                if (Eb != TypeArray && Eb != TypeStruct && Eb != TypeUnion)
                {
                    IrExpr *Root = E->Left;
                    while (Root != NULL && Root->Kind != IrExIdent
                           && Root->Left != NULL
                           && (Root->Kind == IrExIndex || Root->Kind == IrExMember))
                        Root = Root->Left;
                    if (Root != NULL && Root->Kind == IrExIdent)
                    {
                        int skip_uninit = 0;
                        /* IR local: frame LocalInitMap (byte map for struct).
                         * Only when Addr is inside this local's blob (not p->x). */
                        if (Root->LocalSlot >= 0 && IrCallDepth > 0
                            && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                        {
                            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                            int ls = Root->LocalSlot;
                            int sz = 4;
                            int is_struct_local = (Fr->LocalFromType[ls] != NULL
                                && (Fr->LocalFromType[ls]->Base == TypeStruct
                                    || Fr->LocalFromType[ls]->Base == TypeUnion));
                            char *base = (Fr->LocalVal[ls] != 0)
                                ? (char *)(intptr_t)Fr->LocalVal[ls] : NULL;
                            int nmap = IrFrameInitMapLen(Fr, ls);
                            int in_blob = (base != NULL && Addr != NULL && nmap > 0
                                && (char *)Addr >= base
                                && (char *)Addr < base + nmap);
                            if (Eb == TypeChar || Eb == TypeUnsignedChar) sz = 1;
                            else if (Eb == TypeShort || Eb == TypeUnsignedShort) sz = 2;
                            else if (Eb == TypePointer) sz = (int)sizeof(void *);
                            else if (E->BitWidth > 0) sz = 4;
                            if (Fr->LocalInitMap[ls] != NULL && in_blob)
                            {
                                if (IrFrameLeafInit(Fr, ls, Addr, sz))
                                    skip_uninit = 1;
                                else if (is_struct_local)
                                {
                                    if (E->SourceLine) Parser->Line = E->SourceLine;
                                    ParserSetCaretOnIdent(Parser, E->Ident ? E->Ident : "?");
                                    ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
                                }
                            }
                            else if (Fr->LocalInit[ls] && !is_struct_local)
                                skip_uninit = 1;
                            else if (!in_blob && is_struct_local)
                                /* o.in->v: pointee is another object */
                                skip_uninit = 1;
                        }
                        if (!skip_uninit && Root->Ident != NULL
                            && (IrGlobalValSoft(Parser, Root) != NULL))
                        {
                            struct Value *RV = NULL;
                            int sz = 4;
                            RV = IrShadowGet(Parser, Root->Ident); /* shadow bridge */
                            if (Eb == TypeChar || Eb == TypeUnsignedChar) sz = 1;
                            else if (Eb == TypeShort || Eb == TypeUnsignedShort) sz = 2;
                            else if (Eb == TypePointer) sz = (int)sizeof(void *);
                            else if (E->BitWidth > 0) sz = 4;

                            if (RV != NULL && RV->ElemInit != NULL && !RV->IsInitialized
                                && !ExpressionInitLeafAddr(RV, Addr, sz))
                            {
                                if (E->SourceLine) Parser->Line = E->SourceLine;
                                ParserSetCaretOnIdent(Parser, E->Ident ? E->Ident : "?");
                                ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
                            }
                        }

                    }
                }
                if (E->BitWidth > 0)
                    return IrBitExtract(Addr, E->BitOffset, E->BitWidth, E->BitSigned);
                if (Eb == TypeArray)
                {
                    /* Array member as RValue (call arg decay): escape → mark
                     * whole member so callee writes are not false-uninit. */
                    IrExpr *Root = E->Left;
                    int msz = 0;
                    while (Root != NULL && Root->Kind != IrExIdent
                           && Root->Left != NULL
                           && (Root->Kind == IrExIndex || Root->Kind == IrExMember))
                        Root = Root->Left;
                    /* frame: sizeof member array from LocalFromType Members */
                    if (Root != NULL && Root->Kind == IrExIdent
                        && Root->LocalSlot >= 0 && IrCallDepth > 0
                        && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                        struct ValueType *St = Fr->LocalFromType[Root->LocalSlot];
                        struct Value *MV = NULL;
                        if (St != NULL && St->Members != NULL && E->Ident != NULL
                            && TableGet(St->Members, E->Ident, &MV, NULL, NULL, NULL)
                            && MV != NULL && MV->Typ != NULL)
                            msz = MV->Typ->Sizeof;
                        if (msz <= 0) msz = 12; /* fallback: 3 ints */
                        IrFrameMarkBytes(Fr, Root->LocalSlot, Addr, msz);
                    }
                    if (Root != NULL && Root->Kind == IrExIdent && msz > 0)
                        IrShadowMarkAddr(Parser, Root->Ident, Addr, msz);
                    return (long long)(intptr_t)Addr;
                }
                if (Eb == TypePointer)
                    return IrLoadPtrElem(TypePointer, Addr);
                return IrLoadPtrElem(Eb, Addr);
            }
            if (IrResolvePtr(Parser, E, &Addr, &Eb, &Es))
            {
                if (E->BitWidth > 0)
                    return IrBitExtract(Addr, E->BitOffset, E->BitWidth, E->BitSigned);
                return IrLoadPtrElem(Eb, Addr);
            }
            return 0;
        }
        IR_EVAL_CASE(IrExTernary)
        {
            IrExpr *Chosen, *Other;
            long long R;
            int Cond = IrEvalExpr(Parser, E->Left) ? 1 : 0;
            if (Cond)
            {
                Chosen = E->Right;
                Other = (E->Args && E->ArgCount > 0) ? E->Args[0] : NULL;
            }
            else
            {
                if (E->Args == NULL || E->ArgCount < 1 || E->Args[0] == NULL)
                    return 0;
                Chosen = E->Args[0];
                Other = E->Right;
            }
            R = IrEvalExpr(Parser, Chosen);
#ifndef NO_FP
            /* usual arithmetic conversion: if either arm is FP, result is FP bits */
            if (Other != NULL && IrExprIsFP(Parser, Other) && !IrExprIsFP(Parser, Chosen))
                R = IrFPBits((double)R);
#endif
            return R;
        }
        IR_EVAL_CASE(IrExIndex)
        {
            void *Base;
            int ElemBase, ElemSize;
            long long Idx;
            char *P;
            if (!IrResolvePtr(Parser, E->Left, &Base, &ElemBase, &ElemSize))
                ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
            Idx = IrEvalExpr(Parser, E->Right);
            /* Fixed-size array bounds when base is a typed array ident */
            if (E->Left != NULL && E->Left->Kind == IrExIdent && E->Left->Ident != NULL
                && (IrGlobalValSoft(Parser, E->Left) != NULL))
            {
                struct Value *AV = NULL;
                AV = IrGlobalValSoft(Parser, E->Left); /* soft: already gated */
                if (IrSafeChecksOn() && AV != NULL && AV->Typ != NULL && AV->Typ->Base == TypeArray
                    && AV->Typ->ArraySize > 0
                    && (Idx < 0 || Idx >= AV->Typ->ArraySize))
                    ProgramFail(Parser, DiagnosticMsgArrayIndexOutOfBoundsSized,
                        (int)Idx, AV->Typ->ArraySize);
            }
            /* IR local fixed-size array: OOB before uninit (prefer clear error) */
            if (E->Left != NULL && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot >= 0 && IrCallDepth > 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                int asz = Fr->LocalArraySize[E->Left->LocalSlot];
                if (IrSafeChecksOn() && asz > 0 && (Idx < 0 || Idx >= asz))
                {
                    if (E->Left != NULL && E->Left->Ident != NULL)
                        ParserSetCaretOnIdent(Parser, E->Left->Ident);
                    else
                        IrCaretOnIndex(Parser, E);
                    ProgramFail(Parser, DiagnosticMsgArrayIndexOutOfBoundsSized,
                        (int)Idx, asz);
                }
            }
            /* IR local scalar array: per-element init check.
             * Struct arrays use Variable.ElemInit (byte map) via member path. */
            if (E->Left != NULL && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot >= 0 && IrCallDepth > 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                struct ValueType *LFT = Fr->LocalFromType[E->Left->LocalSlot];
                unsigned char *imap = Fr->LocalInitMap[E->Left->LocalSlot];
                if (imap != NULL
                    && !Fr->LocalIsFptr[E->Left->LocalSlot]
                    && (LFT == NULL
                        || (LFT->Base != TypeStruct && LFT->Base != TypeUnion)))
                {
                    if (IrSafeChecksOn() && Idx < 0)
                        ProgramFail(Parser, DiagnosticMsgArrayIndexOutOfBounds);
                    /* Row/plane decay (ElemBase==TypeArray): alias escape —
                     * mark the whole slice initialized (permissive). */
                    if (ElemBase == TypeArray && ElemSize > 0
                        && Fr->LocalElemSize[E->Left->LocalSlot] > 0)
                    {
                        int es = (int)Fr->LocalElemSize[E->Left->LocalSlot];
                        int ncell = ElemSize / (es > 0 ? es : 1);
                        int base = (int)Idx * ncell;
                        int k;
                        for (k = 0; k < ncell; k++)
                        {
                            int fi = base + k;
                            if (fi >= 0 && fi < Fr->LocalArraySize[E->Left->LocalSlot])
                                imap[(size_t)fi] = 1;
                        }
                    }
                    else if (!imap[(size_t)Idx])
                    {
                        if (E->SourceLine > 0)
                            Parser->Line = E->SourceLine;
                        else if (E->Left->SourceLine > 0)
                            Parser->Line = E->Left->SourceLine;
                        ParserSetCaretOnIdent(Parser, E->Left->Ident ? E->Left->Ident : "a");
                        ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
                    }
                }
            }
            P = (char *)Base + (ptrdiff_t)Idx * ElemSize;
            /* p[i] can refer to a caller-owned automatic array, not the current
             * frame's local slot. Check that owner map before loading the cell. */
            if (E->Left != NULL && E->Left->Kind == IrExIdent && ElemSize > 0
                && (E->Left->ParamSlot >= 0
                    || (E->Left->LocalSlot >= 0 && IrCallDepth > 0
                        && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
                        && IrCallStack[IrCallDepth - 1]->LocalIsPtr[E->Left->LocalSlot]
                        && !IrCallStack[IrCallDepth - 1]->LocalOwned[E->Left->LocalSlot])))
                IrEvalCheckOwnedPointerRead(Parser, P, ElemSize, E);
            /* Member-array element uninit: s.a[i] — frame byte map first */
            if (E->Left != NULL && E->Left->Kind == IrExMember)
            {
                IrExpr *Root = E->Left;
                int sz = ElemSize > 0 ? ElemSize : 4;
                while (Root != NULL && Root->Kind != IrExIdent
                       && Root->Left != NULL
                       && (Root->Kind == IrExIndex || Root->Kind == IrExMember))
                    Root = Root->Left;
                if (Root != NULL && Root->Kind == IrExIdent
                    && Root->LocalSlot >= 0 && IrCallDepth > 0
                    && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Fr->LocalInitMap[Root->LocalSlot] != NULL
                        && !IrFrameLeafInit(Fr, Root->LocalSlot, P, sz))
                    {
                        IrEvalFailUninitializedMember(Parser, E->Left, Root);
                    }
                }
                else if (Root != NULL && Root->Kind == IrExIdent
                    && Root->Ident != NULL
                    && (IrGlobalValSoft(Parser, Root) != NULL))
                {
                    struct Value *RV = NULL;
                    RV = IrShadowGet(Parser, Root->Ident); /* shadow bridge */
                    if (RV != NULL && RV->ElemInit != NULL && !RV->IsInitialized
                        && !ExpressionInitLeafAddr(RV, P, sz))
                    {
                        IrEvalFailUninitializedMember(Parser, E->Left, Root);
                    }
                }
            }
            /* Multi-dim leaf m[i][j]: init-check flat cell when Left is Index chain */
            if (ElemBase != TypeArray && ElemBase != TypeStruct && ElemBase != TypeUnion
                && E->Left != NULL && E->Left->Kind == IrExIndex
                && IrCallDepth > 0)
            {
                IrExpr *Root = E->Left;
                int Depth = 1;
                while (Root->Left != NULL && Root->Left->Kind == IrExIndex)
                {
                    Root = Root->Left;
                    Depth++;
                }
                if (Root->Left != NULL && Root->Left->Kind == IrExIdent
                    && Root->Left->LocalSlot >= 0
                    && Root->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    unsigned char *imap = Fr->LocalInitMap[Root->Left->LocalSlot];
                    int nd = (int)Fr->LocalNDims[Root->Left->LocalSlot];
                    if (imap != NULL && nd >= 2)
                    {
                        IrExpr *Chain[IR_MAX_ARRAY_DIMS];
                        int nc = 0, flat = 0, d, stride = 1;
                        IrExpr *T = E;
                        while (T != NULL && T->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
                        {
                            Chain[nc++] = T;
                            T = T->Left;
                        }
                        if (nc == nd)
                        {
                            long long acc = IrEvalExpr(Parser, Chain[nc - 1]->Right);
                            for (d = 1; d < nd; d++)
                            {
                                int dim = Fr->LocalDims[Root->Left->LocalSlot][d];
                                long long ix = IrEvalExpr(Parser, Chain[nc - 1 - d]->Right);
                                if (dim > 0)
                                    acc = acc * dim + ix;
                            }
                            flat = (int)acc;
                            if (flat < 0 || flat >= Fr->LocalArraySize[Root->Left->LocalSlot]
                                || !imap[(size_t)flat])
                            {
                                if (E->SourceLine > 0) Parser->Line = E->SourceLine;
                                /* Caret on the access: prefer last ']' of m[i][j]
                                 * on this line (expect col at closing bracket). */
                                {
                                    const char *Src = Parser->SourceText;
                                    const char *LinePos, *CPos, *Best = NULL;
                                    int LineCount, start = 0;
                                    if (Root->Left->Ident != NULL)
                                        ParserSetCaretOnIdent(Parser, Root->Left->Ident);
                                    start = Parser->CharacterPos > 0 ? Parser->CharacterPos - 1 : 0;
                                    if (Src != NULL)
                                    {
                                        for (LinePos = Src, LineCount = 1;
                                             *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
                                            if (*LinePos == '\n') LineCount++;
                                        for (CPos = LinePos + start; *CPos && *CPos != '\n'; CPos++)
                                        {
                                            if (*CPos == ']') Best = CPos;
                                            if (*CPos == ',' || *CPos == ';' || *CPos == ')') break;
                                        }
                                        if (Best != NULL)
                                            Parser->CharacterPos = (int)(Best - LinePos) + 1;
                                    }
                                }
                                ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
                            }
                        }
                    }
                }
            }
            /* Subarray / struct / large element: return address (array decay). */
            if (ElemBase == TypeArray || ElemBase == TypeStruct || ElemBase == TypeUnion
                || ElemSize > (int)sizeof(long long))
                return (long long)(intptr_t)P;
            /* Also detect via LocalFromType when ElemBase stayed TypeInt */
            if (E->Left != NULL && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot >= 0 && IrCallDepth > 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                struct ValueType *LFT = IrCallStack[IrCallDepth - 1]->LocalFromType[E->Left->LocalSlot];
                if (LFT != NULL
                    && (LFT->Base == TypeStruct || LFT->Base == TypeUnion))
                    return (long long)(intptr_t)P;
            }
            return IrLoadPtrElem(ElemBase, P);
        }
        IR_EVAL_CASE(IrExUnary)
        {
            IrExpr *X = E->Left;
            long long V, NV;
            /* (T*)p — pointer: passthrough value, size in ConstVal.
             * (T)x  — value cast: ConstVal==0, convert via I32 target base. */
            if (E->Op == TokenCast)
            {
                long long V = IrEvalExpr(Parser, X);
                long long R;
                if (E->ConstVal > 0)
                    return V; /* pointer cast: address unchanged */
#ifndef NO_FP
                /* (int)fp_expr: V holds IEEE bits, not integer */
                if (IrExprIsFP(Parser, X) && E->I32 != TypeFP)
                {
                    double d = IrBitsFP(V);
                    switch (E->I32)
                    {
                        case TypeChar:           R = (long long)(signed char)d; break;
                        case TypeUnsignedChar:   R = (long long)(unsigned char)d; break;
                        case TypeShort:          R = (long long)(short)d; break;
                        case TypeUnsignedShort:  R = (long long)(unsigned short)d; break;
                        case TypeInt:            R = (long long)(int)d; break;
                        case TypeUnsignedInt:    R = (long long)(unsigned int)d; break;
                        case TypeLong:           R = (long long)(long)d; break;
                        case TypeUnsignedLong:   R = (long long)(unsigned long)d; break;
                        case TypeLongLong:       R = (long long)d; break;
                        case TypeUnsignedLongLong: R = (long long)(unsigned long long)d; break;
                        default: R = (long long)d; break;
                    }
                    return R;
                }
#endif
#ifndef NO_FP
                /* Integer-to-double casts must return IEEE bits, not the
                 * integer payload interpreted as a double by the caller. */
                if (E->I32 == TypeFP)
                    return IrExprIsFP(Parser, X) ? V : IrFPBits((double)V);
#endif
                switch (E->I32)
                {
                    case TypeChar:           R = (long long)(signed char)V; break;
                    case TypeUnsignedChar:   R = (long long)(unsigned char)V; break;
                    case TypeShort:          R = (long long)(short)V; break;
                    case TypeUnsignedShort:  R = (long long)(unsigned short)V; break;
                    case TypeInt:            R = (long long)(int)V; break;
                    case TypeUnsignedInt:    R = (long long)(unsigned int)V; break;
                    case TypeLong:           R = (long long)(long)V; break;
                    case TypeUnsignedLong:   R = (long long)(unsigned long)V; break;
                    case TypeLongLong:       R = (long long)V; break;
                    case TypeUnsignedLongLong: R = (long long)(unsigned long long)V; break;
                    default: R = V; break;
                }
                return R;
            }
            if (E->Op == TokenSizeof)
            {
                /* sizeof(bitfield) is a constraint violation */
                if (X != NULL && X->Kind == IrExMember)
                {
                    void *A = NULL;
                    int Eb = TypeInt;
                    if (IrResolveMember(Parser, X, &A, &Eb) && X->BitWidth > 0)
                    {
                        if (X->Ident != NULL)
                            ParserSetCaretOnIdent(Parser, X->Ident);
                        else if (X->CharacterPos > 0)
                            Parser->CharacterPos = X->CharacterPos;
                        ProgramFail(Parser, "sizeof applied to a bitfield");
                    }
                }
                /* Prefer local/array size from IR frame; else pointer size; else sizeof(int) */
                if (X != NULL && X->Kind == IrExIdent && X->LocalSlot >= 0
                    && IrCallDepth > 0 && X->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Fr->LocalIsPtr[X->LocalSlot] && Fr->LocalArraySize[X->LocalSlot] > 0
                        && Fr->LocalElemSize[X->LocalSlot] > 0)
                        return (long long)Fr->LocalArraySize[X->LocalSlot]
                             * (long long)Fr->LocalElemSize[X->LocalSlot];
                    if (!Fr->LocalIsPtr[X->LocalSlot] && Fr->LocalElemSize[X->LocalSlot] > 0)
                        return (long long)Fr->LocalElemSize[X->LocalSlot];
                    if (Fr->LocalIsPtr[X->LocalSlot] && Fr->LocalArraySize[X->LocalSlot] == 0)
                        return (long long)sizeof(void *);
                    /* Prefer LocalTyp (full type) then LocalFromType */
                    {
                        struct ValueType *FT = Fr->LocalTyp[X->LocalSlot];
                        if (FT == NULL)
                            FT = Fr->LocalFromType[X->LocalSlot];
                        if (FT != NULL)
                        {
#ifndef NO_FP
                            if (FT->Base == TypeFP)
                                return (long long)sizeof(double);
#endif
                            if (FT->Base == TypeLongLong || FT->Base == TypeUnsignedLongLong)
                                return (long long)sizeof(long long);
                            if (FT->Base == TypeLong || FT->Base == TypeUnsignedLong)
                                return (long long)sizeof(long);
                            if (FT->Sizeof > 0)
                                return (long long)FT->Sizeof;
                        }
                    }
                    /* scalar int/long long with FromType */
                    if (!Fr->LocalIsPtr[X->LocalSlot])
                    {
                        if (Fr->LocalFromType[X->LocalSlot] != NULL
                            && Fr->LocalFromType[X->LocalSlot]->Base == TypeLongLong)
                            return (long long)sizeof(long long);
                        return (long long)sizeof(int);
                    }
                }
                if (X != NULL && X->Kind == IrExConst && X->Op == TokenStringConstant
                    && X->ConstVal != 0)
                {
                    const char *s = (const char *)(intptr_t)X->ConstVal;
                    return (long long)strlen(s) + 1;
                }
                /* sizeof(a[i]) / sizeof(*p): element size, not sizeof(int) */
                if (X != NULL && (X->Kind == IrExIndex
                    || (X->Kind == IrExUnary && X->Op == TokenAsterisk)))
                {
                    void *BaseAddr = NULL;
                    int Eb = TypeInt, Es = (int)sizeof(int);
                    IrExpr *PtrBase = X->Left;
                    if (PtrBase != NULL
                        && IrResolvePtr(Parser, PtrBase, &BaseAddr, &Eb, &Es)
                        && Es > 0)
                        return (long long)Es;
                    if (X->Kind == IrExIndex && X->Left != NULL
                        && X->Left->Kind == IrExIdent && X->Left->LocalSlot >= 0
                        && IrCallDepth > 0
                        && X->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                        int ls = X->Left->LocalSlot;
                        if (Fr->LocalArraySize[ls] > 0 && Fr->LocalElemSize[ls] > 0)
                            return (long long)Fr->LocalElemSize[ls];
                    }
                }
                if (X != NULL && X->Kind == IrExIdent && X->Ident != NULL
                    && (IrGlobalValSoft(Parser, X) != NULL))
                {
                    struct Value *GV = NULL;
                    /* global/typedef: not dual-write — allow even if BABYC_IR_SHADOW=0 */
                    GV = IrGlobalValSoft(Parser, X); /* soft: already gated */
                    if (GV != NULL && GV->Typ != NULL)
                    {
                        struct ValueType *T = GV->Typ;
                        /* typedef name: Typ is Type_Type, real type in Val->Typ */
                        if (T->Base == Type_Type && GV->Val != NULL && GV->Val->Typ != NULL)
                            T = GV->Val->Typ;
                        if (T->Sizeof > 0)
                            return (long long)TypeSize(T, T->ArraySize, TRUE);
                    }
                }
                return (long long)sizeof(int);
            }
            if (E->Op == TokenUnaryNot)
                return IrEvalExpr(Parser, X) ? 0 : 1;
            if (E->Op == TokenUnaryExor)
                return (long long)(~(int)IrEvalExpr(Parser, X));
            /* &x — address of lvalue (storage), not the loaded value */
            if (E->Op == TokenAmpersand)
            {
                void *Addr = NULL;
                int Eb = TypeInt, Es = (int)sizeof(int);
                if (X == NULL)
                    return 0;
                /* &bitfield is a constraint violation */
                if (X->Kind == IrExMember)
                {
                    void *MAddr = NULL;
                    int MEb = TypeInt;
                    if (IrResolveMember(Parser, X, &MAddr, &MEb) && X->BitWidth > 0)
                    {
                        /* Caret on the bitfield identifier (s.a → 'a'), not '&' or '.' */
                        if (X->Ident != NULL)
                            ParserSetCaretOnIdent(Parser, X->Ident);
                        else if (X->CharacterPos > 0)
                            Parser->CharacterPos = X->CharacterPos;
                        ProgramFail(Parser, "cannot take address of bitfield");
                    }
                    /* &s.m / &a[i].m: escape → mark member (and whole element
                     * for array-of-struct so *q after q=&v[i].a is not uninit). */
                    if (MAddr != NULL)
                    {
                        IrExpr *Root = X;
                        while (Root->Left != NULL
                               && (Root->Left->Kind == IrExMember
                                   || Root->Left->Kind == IrExIndex
                                   || Root->Left->Kind == IrExIdent))
                            Root = Root->Left;
                        /* IR local array/struct: mark LocalInitMap (byte map for struct) */
                        if (Root->Kind == IrExIdent && Root->LocalSlot >= 0
                            && IrCallDepth > 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                        {
                            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                            int ls = Root->LocalSlot;
                            int msz = 4;
                            if (MEb == TypeChar || MEb == TypeUnsignedChar) msz = 1;
                            else if (MEb == TypeShort || MEb == TypeUnsignedShort) msz = 2;
                            else if (MEb == TypePointer || MEb == TypeArray)
                                msz = (int)sizeof(void *);
                            else if (MEb == TypeLong || MEb == TypeUnsignedLong)
                                msz = (int)sizeof(long);
                            else if (MEb == TypeLongLong || MEb == TypeUnsignedLongLong)
                                msz = 8;
                            IrFrameMarkBytes(Fr, ls, MAddr, msz);
                            /* array-of-struct: mark whole element bytes on escape */
                            if (Fr->LocalFromType[ls] != NULL
                                && (Fr->LocalFromType[ls]->Base == TypeStruct
                                    || Fr->LocalFromType[ls]->Base == TypeUnion)
                                && Fr->LocalFromType[ls]->Sizeof > 0
                                && Fr->LocalVal[ls] != 0)
                            {
                                int ssz = Fr->LocalFromType[ls]->Sizeof;
                                char *base = (char *)(intptr_t)Fr->LocalVal[ls];
                                int off = (int)((char *)MAddr - base);
                                if (off >= 0 && ssz > 0)
                                {
                                    int e0 = (off / ssz) * ssz;
                                    IrFrameMarkBytes(Fr, ls, base + e0, ssz);
                                }
                            }
                        }
                        if (Root->Kind == IrExIdent && Root->Ident != NULL)
                        {
                            int msz = 4;
                            if (MEb == TypeChar || MEb == TypeUnsignedChar) msz = 1;
                            else if (MEb == TypeShort || MEb == TypeUnsignedShort) msz = 2;
                            else if (MEb == TypePointer || MEb == TypeArray)
                                msz = (int)sizeof(void *);
                            else if (MEb == TypeLong || MEb == TypeUnsignedLong)
                                msz = (int)sizeof(long);
                            else if (MEb == TypeLongLong || MEb == TypeUnsignedLongLong)
                                msz = 8;
                            IrShadowMarkAddr(Parser, Root->Ident, MAddr, msz);
                            /* array-of-struct element: mark whole element via shadow */
                            if (X->Left != NULL && X->Left->Kind == IrExIndex
                                && (IrGlobalValSoft(Parser, Root) != NULL))
                            {
                                struct Value *SV = NULL;
                                SV = IrShadowGet(Parser, Root->Ident); /* shadow bridge */
                                if (SV != NULL && SV->ElemInit != NULL
                                    && SV->Typ != NULL && SV->Typ->Base == TypeArray
                                    && SV->Typ->FromType != NULL
                                    && (SV->Typ->FromType->Base == TypeStruct
                                        || SV->Typ->FromType->Base == TypeUnion))
                                {
                                    int esz = SV->Typ->FromType->Sizeof;
                                    char *ebase = (char *)MAddr - ((char *)MAddr - (char *)SV->Val) % (esz > 0 ? esz : 1);
                                    if (esz > 0)
                                        IrValueMarkAddr(SV, ebase, esz);
                                }
                            }
                        }
                    }
                }
                /* IR local */
                if (X->Kind == IrExIdent && X->LocalSlot >= 0 && IrCallDepth > 0
                    && X->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    Fr->LocalInit[X->LocalSlot] = 1;
                    /* Owned blob (struct value / array): LocalVal is already &storage */
                    if (Fr->LocalOwned[X->LocalSlot]
                        || Fr->LocalArraySize[X->LocalSlot] > 0)
                        return Fr->LocalVal[X->LocalSlot];
                    /* scalar / pointer variable: address of the slot */
                    return (long long)(intptr_t)&Fr->LocalVal[X->LocalSlot];
                }
                /* IR param pointer: address of ParamVal slot */
                if (X->Kind == IrExIdent && X->ParamSlot >= 0 && IrCallDepth > 0
                    && X->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                {
                    return (long long)(intptr_t)&IrCallStack[IrCallDepth - 1]->ParamVal[X->ParamSlot];
                }
                /* Global / Variable table lvalue: address of storage, not pointee */
                if (X->Kind == IrExIdent && X->Ident != NULL
                    && (IrGlobalValSoft(Parser, X) != NULL)
                    )
                {
                    struct Value *GV = NULL;
                    GV = IrGlobalValSoft(Parser, X); /* soft: already gated */
                    if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeFunction)
                        return (long long)(intptr_t)GV;
                    if (GV != NULL && GV->Val != NULL)
                    {
                        if (GV->Typ != NULL && GV->Typ->Base == TypePointer)
                            return (long long)(intptr_t)&GV->Val->Pointer;
                        /* array / struct / scalar: payload is Val */
                        return (long long)(intptr_t)(void *)GV->Val;
                    }
                }
                /* &a[i] / &a[0]: mark local array elements escaped/init via ptr */
                if (X->Kind == IrExIndex && X->Left != NULL
                    && X->Left->Kind == IrExIdent && X->Left->LocalSlot >= 0
                    && IrCallDepth > 0 && X->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    int ls = X->Left->LocalSlot;
                    if (Fr->LocalInitMap[ls] != NULL && Fr->LocalArraySize[ls] > 0)
                    {
                        int k;
                        for (k = 0; k < Fr->LocalArraySize[ls]; k++)
                            Fr->LocalInitMap[ls][k] = 1;
                        Fr->LocalInit[ls] = 1;
                    }
                }
                if (!IrAddrOf(Parser, X, &Addr, &Eb, &Es))
                    ProgramFail(Parser, "invalid address-of");
                return (long long)(intptr_t)Addr;
            }
            /* *ptr — dereference (ParamArray / Body IR) */
            if (E->Op == TokenAsterisk)
            {
                /* *(T*)p / *((T*)(U*)p) — outermost cast gives pointee size;
                 * walk nested TokenCast to the pointer value. */
                if (X != NULL && X->Kind == IrExUnary && X->Op == TokenCast)
                {
                    IrExpr *Cur = X;
                    int Es = 0, Eb = 0;
                    void *Addr;
                    /* outermost non-void pointee wins */
                    while (Cur != NULL && Cur->Kind == IrExUnary && Cur->Op == TokenCast)
                    {
                        if (Cur->ConstVal > 0 && Cur->I32 != TypeVoid)
                        {
                            if (Es <= 0)
                            {
                                Es = (int)Cur->ConstVal;
                                Eb = Cur->I32;
                            }
                        }
                        Cur = Cur->Left;
                    }
                    Addr = (void *)(intptr_t)IrEvalExpr(Parser, Cur != NULL ? Cur : X->Left);
                    if (Addr == NULL)
                        ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                    if (Es <= 0) Es = (int)sizeof(int);
                    if (Eb > 0 && Eb != TypeVoid)
                        return IrLoadPtrElem(Eb, Addr);
                    if (Es <= 1)
                        return (long long)(unsigned char)(*(unsigned char *)Addr);
                    if (Es == 2)
                        return (long long)*(short *)Addr;
                    if (Es == 8)
                        return *(long long *)Addr;
                    return (long long)*(int *)Addr;
                }
                /* Fast path: IR local / param pointer — no VarGet */
                if (X != NULL && X->Kind == IrExIdent && IrCallDepth > 0)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (X->LocalSlot >= 0 && X->LocalSlot < IR_CALL_MAX_LOCALS
                        && Fr->LocalIsPtr[X->LocalSlot])
                    {
                        void *Addr = (void *)(intptr_t)Fr->LocalVal[X->LocalSlot];
                        int Es = Fr->LocalElemSize[X->LocalSlot];
                        if (Addr == NULL)
                            ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                        if (Es <= 1)
                            return (long long)(unsigned char)(*(unsigned char *)Addr);
                        if (Es == 2)
                            return (long long)*(short *)Addr;
                        if (Es == 8)
                            return *(long long *)Addr;
                        return (long long)*(int *)Addr;
                    }
                    if (X->ParamSlot >= 0 && X->ParamSlot < Fr->NumParams
                        && (Fr->ParamIsPtr[X->ParamSlot]
                            || (Fr->ParamTyp[X->ParamSlot] != NULL
                                && Fr->ParamTyp[X->ParamSlot]->Base == TypePointer)))
                    {
                        void *Addr = (void *)(intptr_t)Fr->ParamVal[X->ParamSlot];
                        struct ValueType *PT = Fr->ParamTyp[X->ParamSlot];
                        int Es = (int)sizeof(int); /* default int* */
                        if (Addr == NULL)
                            ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                        if (PT != NULL && PT->FromType != NULL && PT->FromType->Sizeof > 0)
                            Es = PT->FromType->Sizeof;
                        else if (PT != NULL && PT->FromType != NULL && PT->FromType->Base == TypeChar)
                            Es = 1;
                        if (Es <= 1)
                            return (long long)(unsigned char)(*(unsigned char *)Addr);
                        if (Es == 2)
                            return (long long)*(short *)Addr;
                        if (Es == 8)
                            return *(long long *)Addr;
                        return (long long)*(int *)Addr;
                    }
                }
                {
                    void *Addr;
                    int ElemBase, ElemSize;
                    /* **matrix and **(matrix + row): the inner dereference is
                     * an array row, which decays to a pointer to its first
                     * scalar. Do not interpret that scalar as a pointer word. */
                    if (X != NULL && X->Kind == IrExUnary && X->Op == TokenAsterisk
                        && X->Left != NULL)
                    {
                        IrExpr *Root = X->Left;
                        while (Root != NULL && Root->Kind == IrExBinOp
                               && (Root->Op == TokenPlus || Root->Op == TokenMinus))
                        {
                            if (Root->Left != NULL && Root->Left->Kind == IrExIdent)
                                Root = Root->Left;
                            else if (Root->Op == TokenPlus && Root->Right != NULL
                                     && Root->Right->Kind == IrExIdent)
                                Root = Root->Right;
                            else break;
                        }
                        if (Root != NULL && Root->Kind == IrExIdent)
                        {
                            int IsMulti = 0, LeafBase = TypeInt;
                            if (Root->LocalSlot >= 0 && IrCallDepth > 0
                                && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                            {
                                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                                int ls = Root->LocalSlot;
                                IsMulti = Fr->LocalNDims[ls] >= 2;
                                if (Fr->LocalFromType[ls] != NULL)
                                    LeafBase = Fr->LocalFromType[ls]->Base;
                            }
                            else if (Root->LocalSlot < 0 && Root->ParamSlot < 0
                                     && Root->Ident != NULL)
                            {
                                struct Value *GV = IrGlobalValSoft(Parser, Root);
                                struct ValueType *T = GV != NULL ? GV->Typ : NULL;
                                if (T != NULL && T->Base == TypeArray && T->FromType != NULL
                                    && T->FromType->Base == TypeArray)
                                {
                                    IsMulti = 1;
                                    while (T != NULL && T->Base == TypeArray && T->FromType != NULL)
                                        T = T->FromType;
                                    if (T != NULL) LeafBase = T->Base;
                                }
                            }
                            if (IsMulti)
                            {
                                Addr = (void *)(intptr_t)IrEvalExpr(Parser, X->Left);
                                if (Addr == NULL)
                                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                                return IrLoadPtrElem(LeafBase, Addr);
                            }
                        }
                    }
                    if (X != NULL && X->Kind == IrExBinOp
                        && IrAddrOf(Parser, E, &Addr, &ElemBase, &ElemSize))
                    {
                        if (Addr == NULL)
                            ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                        return IrLoadPtrElem(ElemBase, Addr);
                    }
                    if (!IrResolvePtr(Parser, X, &Addr, &ElemBase, &ElemSize))
                        ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                    if (Addr == NULL)
                        ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                    return IrLoadPtrElem(ElemBase, Addr);
                }
            }
            /* ++ / -- prefix (ConstVal==0) or postfix (ConstVal==1).
             * Pointers must use element size (char* +1 byte), not integer +1.
             * ExpressionAssignInt ignores TypePointer (no-op) -> infinite
             * while (*p) p++ loops (split_csv hang). */
            if ((E->Op == TokenIncrement || E->Op == TokenDecrement)
                && X != NULL && IrAssignTargetIsConst(Parser, X))
            {
                if (E->SourceLine) Parser->Line = E->SourceLine;
                ProgramFail(Parser, "assignment of read-only variable");
            }
            /* F3: string-literal store known at compile ("lit"[i]=, *"lit"=). */
            if (E->Left != NULL && IrAssignTargetIsStringLiteral(E->Left))
            {
                if (E->SourceLine) Parser->Line = E->SourceLine;
                if (E->CharacterPos > 0)
                    Parser->CharacterPos = E->CharacterPos;
                ProgramFail(Parser, "assignment of read-only location (string literal)");
            }
            if (X != NULL && X->Kind == IrExIdent && X->Ident != NULL
                && !(X->LocalSlot >= 0 && IrCallDepth > 0)
                && !(X->ParamSlot >= 0 && IrCallDepth > 0))
            {
                Var = IrGlobalValSoft(Parser, X); /* soft: already gated */
                if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypePointer)
                {
                    void *Orig = Var->Val->Pointer;
                    int Size = TypeSize(Var->Typ->FromType, 0, TRUE);
                    if (Size < 1) Size = 1;
                    if (Orig == NULL)
                        ProgramFail(Parser, "invalid use of a NULL pointer");
                    Var->Val->Pointer = (void *)((char *)Orig
                        + ((E->Op == TokenIncrement) ? Size : -Size));
                    return E->ConstVal ? (long long)(intptr_t)Orig
                                       : (long long)(intptr_t)Var->Val->Pointer;
                }
            }
            /* Fast path: ++/-- on IR local/param pointer */
            if (X != NULL && X->Kind == IrExIdent && IrCallDepth > 0
                && (E->Op == TokenIncrement || E->Op == TokenDecrement))
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (X->LocalSlot >= 0 && X->LocalSlot < IR_CALL_MAX_LOCALS
                    && Fr->LocalIsPtr[X->LocalSlot])
                {
                    int Step = IrFrameGetPointeeSize(Fr, X->LocalSlot);
                    void *Orig = (void *)(intptr_t)Fr->LocalVal[X->LocalSlot];
                    void *Neu;
                    if (Step < 1 && Fr->LocalFromType[X->LocalSlot] != NULL
                        && Fr->LocalFromType[X->LocalSlot]->Sizeof > 0
                        && Fr->LocalFromType[X->LocalSlot]->Base != TypePointer)
                        Step = Fr->LocalFromType[X->LocalSlot]->Sizeof;
                    if (Step < 1 && Fr->LocalTyp[X->LocalSlot] != NULL
                        && Fr->LocalTyp[X->LocalSlot]->Base == TypePointer
                        && Fr->LocalTyp[X->LocalSlot]->FromType != NULL
                        && Fr->LocalTyp[X->LocalSlot]->FromType->Sizeof > 0)
                        Step = Fr->LocalTyp[X->LocalSlot]->FromType->Sizeof;
                    if (Step < 1) Step = (int)sizeof(int);
                    if (Orig == NULL)
                        ProgramFail(Parser, "invalid use of a NULL pointer");
                    Neu = (void *)((char *)Orig + ((E->Op == TokenIncrement) ? Step : -Step));
                    Fr->LocalVal[X->LocalSlot] = (long long)(intptr_t)Neu;
                    Fr->LocalInit[X->LocalSlot] = 1;
                    return E->ConstVal ? (long long)(intptr_t)Orig : (long long)(intptr_t)Neu;
                }
                if (X->ParamSlot >= 0 && X->ParamSlot < Fr->NumParams
                    && Fr->ParamIsPtr[X->ParamSlot])
                {
                    void *Orig = (void *)(intptr_t)Fr->ParamVal[X->ParamSlot];
                    void *Neu;
                    int Step = 1;
                    struct ValueType *PT = Fr->ParamTyp[X->ParamSlot];
                    if (PT != NULL && PT->FromType != NULL && PT->FromType->Sizeof > 0)
                        Step = PT->FromType->Sizeof;
                    if (Orig == NULL)
                        ProgramFail(Parser, "invalid use of a NULL pointer");
                    Neu = (void *)((char *)Orig + ((E->Op == TokenIncrement) ? Step : -Step));
                    Fr->ParamVal[X->ParamSlot] = (long long)(intptr_t)Neu;
                    return E->ConstVal ? (long long)(intptr_t)Orig : (long long)(intptr_t)Neu;
                }
            }
            V = IrEvalExpr(Parser, X);
            /* Pointer param/local: step by element size, not 1 */
            {
                int Step = 1;
                if (X != NULL && X->Kind == IrExIdent && X->ParamSlot >= 0
                    && IrCallDepth > 0
                    && X->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams
                    && IrCallStack[IrCallDepth - 1]->ParamIsPtr[X->ParamSlot])
                {
                    void *A;
                    int Eb, Es = 1;
                    if (IrResolvePtr(Parser, X, &A, &Eb, &Es) && Es > 0)
                        Step = Es;
                }
                else if (X != NULL && X->Kind == IrExIdent && X->LocalSlot >= 0
                         && IrCallDepth > 0 && X->LocalSlot < IR_CALL_MAX_LOCALS
                         && IrCallStack[IrCallDepth - 1]->LocalIsPtr[X->LocalSlot])
                {
                    Step = IrCallStack[IrCallDepth - 1]->LocalElemSize[X->LocalSlot];
                    if (Step < 1) Step = 1;
                }
                else if (X != NULL && X->Kind == IrExIdent && X->Ident != NULL
                         && X->LocalSlot < 0 && X->ParamSlot < 0
                         && (IrGlobalValSoft(Parser, X) != NULL)
                         )
                {
                    struct Value *PV = NULL;
                    PV = IrGlobalValSoft(Parser, X); /* soft: already gated */
                    if (PV != NULL && PV->Typ != NULL && PV->Typ->Base == TypePointer)
                    {
                        int Sz = TypeSize(PV->Typ->FromType, 0, TRUE);
                        if (Sz > 0) Step = Sz;
                    }
                }
                NV = (E->Op == TokenIncrement) ? V + Step : V - Step;
            }
            /* (*p)++ / (*p)-- : store through pointer (incl. *(T*)p) */
            if (X != NULL && X->Kind == IrExUnary && X->Op == TokenAsterisk)
            {
                void *Addr = NULL;
                int ElemBase = TypeInt, ElemSize = (int)sizeof(int);
                IrExpr *Target = X->Left;
                if (Target != NULL && Target->Kind == IrExUnary && Target->Op == TokenCast)
                {
                    IrExpr *Cur = Target;
                    ElemBase = 0;
                    ElemSize = 0;
                    while (Cur != NULL && Cur->Kind == IrExUnary && Cur->Op == TokenCast)
                    {
                        if (Cur->ConstVal > 0 && Cur->I32 != TypeVoid)
                        {
                            if (ElemSize <= 0)
                            {
                                ElemSize = (int)Cur->ConstVal;
                                ElemBase = Cur->I32;
                            }
                        }
                        Cur = Cur->Left;
                    }
                    Addr = (void *)(intptr_t)IrEvalExpr(Parser, Cur != NULL ? Cur : Target->Left);
                }
                else if (!IrResolvePtr(Parser, Target, &Addr, &ElemBase, &ElemSize))
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                if (Addr == NULL)
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                if (ElemSize <= 0) { ElemSize = (int)sizeof(int); ElemBase = TypeInt; }
                /* V was loaded as integer value; store NV back */
                IrStorePtrElem(Parser, ElemBase, Addr, NV);
                return E->ConstVal ? V : NV;
            }
            /* a[i]++ / a[i]-- : store through resolved address */
            if (X != NULL && X->Kind == IrExIndex)
            {
                void *Addr = NULL;
                int ElemBase = TypeInt, ElemSize = (int)sizeof(int);
                long long Idx;
                char *P;
                if (!IrResolvePtr(Parser, X->Left, &Addr, &ElemBase, &ElemSize))
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                Idx = IrEvalExpr(Parser, X->Right);
                P = (char *)Addr + (ptrdiff_t)Idx * ElemSize;
                IrStorePtrElem(Parser, ElemBase, P, NV);
                return E->ConstVal ? V : NV;
            }
            /* s.bf++ / s.m++ : member (incl. bitfield) store */
            if (X != NULL && X->Kind == IrExMember)
            {
                void *Addr = NULL;
                int Eb = TypeInt;
                if (!IrResolveMember(Parser, X, &Addr, &Eb))
                    ProgramFail(Parser, "invalid member assignment");
                if (X->BitWidth > 0)
                    IrBitInsert(Addr, X->BitOffset, X->BitWidth, NV);
                else
                    IrStorePtrElem(Parser, Eb, Addr, NV);
                return E->ConstVal ? V : NV;
            }
            if (X != NULL && X->Kind == IrExIdent)
            {
                if (X->LocalSlot >= 0 && IrCallDepth > 0 &&
                    X->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Frn = IrCallStack[IrCallDepth - 1];
                    NV = IrNarrowToLocalType(Frn, X->LocalSlot, NV);
                    Frn->LocalVal[X->LocalSlot] = NV;
                    Frn->LocalInit[X->LocalSlot] = 1;
                }
                else if (X->ParamSlot >= 0 && IrCallDepth > 0 &&
                         X->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                {
                    IrCallStack[IrCallDepth - 1]->ParamVal[X->ParamSlot] = NV;
                    /* keep VariableDefine binding in sync for VarGet */
                    if (X->Ident != NULL && (IrGlobalValSoft(Parser, X) != NULL)
                        )
                    {
                        struct Value *PV = NULL;
                        PV = IrGlobalValSoft(Parser, X); /* soft: already gated */
                        if (PV != NULL && PV->Typ != NULL && PV->Typ->Base == TypePointer)
                            PV->Val->Pointer = (void *)(intptr_t)NV;
                    }
                }
                else
                {
                    Var = IrGlobalValSoft(Parser, X); /* soft: already gated */
                    if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypePointer)
                    {
                        void *Orig = Var->Val->Pointer;
                        int Size = TypeSize(Var->Typ->FromType, 0, TRUE);
                        if (Size < 1) Size = 1;
                        if (Orig == NULL)
                            ProgramFail(Parser, "invalid use of a NULL pointer");
                        Var->Val->Pointer = (void *)((char *)Orig
                            + ((E->Op == TokenIncrement) ? Size : -Size));
                        return E->ConstVal ? (long long)(intptr_t)Orig
                                           : (long long)(intptr_t)Var->Val->Pointer;
                    }
                    ExpressionAssignInt(Parser, Var, (PTR_TYPE_INT_SIGNED)NV, FALSE);
                    /* Prefix ++/-- yields the stored, converted value for
                     * narrow and unsigned integer types. Avoid a generic value
                     * re-read for signed full-width integers: it adds a hot-path
                     * lookup to every loop increment without changing their
                     * defined result. */
                    if (!E->ConstVal && Var->Typ != NULL
                        && (Var->Typ->Base == TypeChar
                            || Var->Typ->Base == TypeUnsignedChar
                            || Var->Typ->Base == TypeShort
                            || Var->Typ->Base == TypeUnsignedShort
                            || Var->Typ->Base == TypeUnsignedInt
                            || Var->Typ->Base == TypeUnsignedLong
                            || Var->Typ->Base == TypeUnsignedLongLong))
                        NV = IrValueAsInt64(Var);
                }
            }
            return E->ConstVal ? V : NV;
        }
        IR_EVAL_CASE(IrExAssign)
        {
            long long V;
            if (E->Left != NULL && IrAssignTargetIsConst(Parser, E->Left))
                IrFailReadOnlyAssign(Parser, E->SourceLine, E->CharacterPos);
            /* fp = func / fp = &func; local, global, or struct-member fptr store.
             * Store the Function Value* (not a C code address). */
            {
                IrExpr *Rfn = E->Right;
                if (Rfn != NULL && Rfn->Kind == IrExUnary && Rfn->Op == TokenAmpersand
                    && Rfn->Left != NULL && Rfn->Left->Kind == IrExIdent)
                    Rfn = Rfn->Left;
                if (Rfn != NULL && Rfn->Kind == IrExIdent
                    && Rfn->Ident != NULL
                    && (IrGlobalValSoft(Parser, Rfn) != NULL))
            {
                struct Value *FV = NULL;
                FV = IrGlobalValSoft(Parser, Rfn); /* soft: already gated */
                if (FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction)
                {
                    if (E->Left != NULL && E->Left->Kind == IrExIdent
                        && E->Left->LocalSlot >= 0 && IrCallDepth > 0
                        && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        IrCallFrame *F = IrCallStack[IrCallDepth - 1];
                        F->LocalVal[E->Left->LocalSlot] = (long long)(intptr_t)FV;
                        F->LocalIsFptr[E->Left->LocalSlot] = 1;
                        F->LocalIsPtr[E->Left->LocalSlot] = 1;
                        F->LocalInit[E->Left->LocalSlot] = 1;
                        if (E->Left->LocalSlot >= F->NumLocals)
                            IrFrameSetLocalExtent(F, E->Left->LocalSlot + 1);
                        if (E->Left->Ident != NULL
                            && (IrGlobalValSoft(Parser, E->Left) != NULL))
                        {
                            struct Value *LV = NULL;
                            LV = IrGlobalValSoft(Parser, E->Left); /* soft: already gated */
                            if (LV != NULL && LV->Val != NULL)
                                LV->Val->Pointer = (void *)FV;
                        }
                        return (long long)(intptr_t)FV;
                    }
                    if (E->Left != NULL && E->Left->Kind == IrExIdent
                        && E->Left->Ident != NULL
                        && E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0
                        && (IrGlobalValSoft(Parser, E->Left) != NULL))
                    {
                        struct Value *LV = NULL;
                        LV = IrGlobalValSoft(Parser, E->Left); /* soft: already gated */
                        if (LV != NULL && LV->Typ != NULL
                            && LV->Typ->Base == TypePointer
                            && LV->Typ->FromType != NULL
                            && LV->Typ->FromType->Base == TypeFunction)
                        {
                            LV->Val->Pointer = (void *)FV;
                            return (long long)(intptr_t)FV;
                        }
                    }
                    /* s.fp = func / p->fp = func */
                    if (E->Left != NULL && E->Left->Kind == IrExMember)
                    {
                        void *Addr = NULL;
                        int Eb = TypeInt;
                        if (!IrResolveMember(Parser, E->Left, &Addr, &Eb) || Addr == NULL)
                            ProgramFail(Parser, "invalid member function-pointer assignment");
                        *(struct Value **)Addr = FV;
                        /* Mark member bytes so later c->f0 loads are not false-uninit */
                        {
                            IrExpr *Root = E->Left;
                            while (Root->Left != NULL
                                   && (Root->Left->Kind == IrExMember
                                       || Root->Left->Kind == IrExIndex
                                       || Root->Left->Kind == IrExIdent))
                                Root = Root->Left;
                            /* IR local: mark LocalInitMap by address (B.2 frame truth) */
                            if (Root->Kind == IrExIdent && Root->LocalSlot >= 0
                                && IrCallDepth > 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                            {
                                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                                IrFrameMarkBytes(Fr, Root->LocalSlot, Addr, (int)sizeof(void *));
                            }
                            if (Root->Kind == IrExIdent)
                                IrShadowMarkAddr(Parser, Root->Ident, Addr, (int)sizeof(void *));
                        }
                        return (long long)(intptr_t)FV;
                    }
                    /* ops[i] = &func */
                    if (E->Left != NULL && E->Left->Kind == IrExIndex)
                    {
                        void *Base = NULL;
                        int Eb = TypeInt, Es = (int)sizeof(void *);
                        long long Idx;
                        char *Slot;
                        if (IrResolvePtr(Parser, E->Left->Left, &Base, &Eb, &Es) && Base != NULL)
                        {
                            Idx = IrEvalExpr(Parser, E->Left->Right);
                            Slot = (char *)Base + (ptrdiff_t)Idx * (ptrdiff_t)sizeof(void *);
                            *(struct Value **)Slot = FV;
                            IrEvalMarkOwnedPointerWrite(Parser, Slot, (int)sizeof(void *));
                            return (long long)(intptr_t)FV;
                        }
                    }
                }
            }
            } /* end fp=&func peel */
            V = IrEvalExpr(Parser, E->Right);
            /* Aggregate store through a member / dereference / global
             * designator: V is the source object's address; copy the exact
             * object size into the designated storage (evaluated once). */
            if (E->Left != NULL && E->Left->Kind != IrExIndex
                && E->Left->CanonicalType != NULL
                && IrExprDesignatesAggregateObject(E->Left))
            {
                void *Dst = NULL;
                int sz = E->Left->CanonicalType->Sizeof;
                if (!IrAggregateObjectAddr(Parser, E->Left, &Dst))
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                if (V == 0)
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                if ((void *)(intptr_t)V != Dst)
                    memmove(Dst, (void *)(intptr_t)V, (size_t)sz);
                IrEvalMarkOwnedPointerWrite(Parser, Dst, sz);
                return (long long)(intptr_t)Dst;
            }
            /* struct-by-value: t = s  (both local owned blobs) */
            if (E->Left != NULL && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot >= 0 && IrCallDepth > 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
                && E->Right != NULL && E->Right->Kind == IrExIdent
                && E->Right->LocalSlot >= 0
                && E->Right->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *F = IrCallStack[IrCallDepth - 1];
                int LS = E->Left->LocalSlot, RS = E->Right->LocalSlot;
                if (F->LocalOwned[LS] && F->LocalOwned[RS]
                    && F->LocalFromType[LS] != NULL
                    && F->LocalFromType[RS] != NULL
                    && (F->LocalFromType[LS]->Base == TypeStruct
                        || F->LocalFromType[LS]->Base == TypeUnion)
                    && F->LocalFromType[LS] == F->LocalFromType[RS])
                {
                    int sz = F->LocalFromType[LS]->Sizeof;
                    void *Dst = (void *)(intptr_t)F->LocalVal[LS];
                    void *Src = (void *)(intptr_t)F->LocalVal[RS];
                    if (Dst != NULL && Src != NULL && sz > 0)
                        memcpy(Dst, Src, (size_t)sz);
                    F->LocalInit[LS] = 1;
                    /* copy init map (byte map) so t=s transfers member init */
                    if (F->LocalInitMap[LS] != NULL && F->LocalInitMap[RS] != NULL)
                    {
                        int cn = IrFrameInitMapLen(F, LS);
                        int rn = IrFrameInitMapLen(F, RS);
                        if (cn > 0 && rn > 0)
                        {
                            if (cn > rn) cn = rn;
                            memcpy(F->LocalInitMap[LS], F->LocalInitMap[RS], (size_t)cn);
                        }
                    }
                    else if (F->LocalInitMap[LS] != NULL && sz > 0
                             && IrFrameInitMapLen(F, LS) >= sz)
                        memset(F->LocalInitMap[LS], 1, (size_t)sz);
                    /* mark Variable shadow fully init if present */
                    if (E->Left->Ident != NULL
                        && (IrGlobalValSoft(Parser, E->Left) != NULL))
                    {
                        struct Value *LV = NULL;
                        LV = IrShadowGet(Parser, E->Left->Ident); /* shadow bridge */
                        if (LV != NULL)
                        {
                            IrValueMarkAll(LV);
                            if (LV->ElemInit != NULL && LV->ElemInitCount > 0)
                                memset(LV->ElemInit, 1, (size_t)LV->ElemInitCount);
                        }
                    }
                    return V;
                }
            }
            /* local_struct = global/index/member struct value */
            if (E->Left != NULL && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot >= 0 && IrCallDepth > 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *F = IrCallStack[IrCallDepth - 1];
                int LS = E->Left->LocalSlot;
                if (F->LocalOwned[LS] && F->LocalFromType[LS] != NULL
                    && (F->LocalFromType[LS]->Base == TypeStruct
                        || F->LocalFromType[LS]->Base == TypeUnion))
                {
                    int sz = F->LocalFromType[LS]->Sizeof;
                    void *Dst = (void *)(intptr_t)F->LocalVal[LS];
                    void *Src = NULL;
                    /* RHS already evaluated to V; for struct values V is often
                     * the address of the source payload (Ident/Index/Member). */
                    if (E->Right != NULL && E->Right->Kind == IrExIdent
                        && E->Right->LocalSlot >= 0
                        && E->Right->LocalSlot < IR_CALL_MAX_LOCALS
                        && F->LocalOwned[E->Right->LocalSlot])
                        Src = (void *)(intptr_t)F->LocalVal[E->Right->LocalSlot];
                    else if (V != 0)
                        Src = (void *)(intptr_t)V;
                    if (Dst != NULL && Src != NULL && sz > 0
                        && Src != Dst)
                    {
                        memcpy(Dst, Src, (size_t)sz);
                        F->LocalInit[LS] = 1;
                        if (F->LocalInitMap[LS] != NULL && sz > 0
                            && IrFrameInitMapLen(F, LS) >= sz)
                            memset(F->LocalInitMap[LS], 1, (size_t)sz);
                        if (E->Left->Ident != NULL
                            && (IrGlobalValSoft(Parser, E->Left) != NULL))
                        {
                            struct Value *LV = NULL;
                            LV = IrShadowGet(Parser, E->Left->Ident); /* shadow bridge */
                            if (LV != NULL)
                            {
                                IrValueMarkAll(LV);
                                if (LV->ElemInit != NULL && LV->ElemInitCount > 0)
                                    memset(LV->ElemInit, 1, (size_t)LV->ElemInitCount);
                            }
                        }
                        return V;
                    }
                }
            }
            /* *p = v  or  *(T*)p = v  or  p[i] = v */
            if (E->Left != NULL && E->Left->Kind == IrExUnary
                && E->Left->Op == TokenAsterisk)
            {
                void *Addr;
                int ElemBase = TypeInt, ElemSize = (int)sizeof(int);
                IrExpr *Target = E->Left->Left;
                /* *(T*)p = v — cast chain supplies pointee type and address */
                if (Target != NULL && Target->Kind == IrExUnary && Target->Op == TokenCast)
                {
                    IrExpr *Cur = Target;
                    ElemBase = 0;
                    ElemSize = 0;
                    while (Cur != NULL && Cur->Kind == IrExUnary && Cur->Op == TokenCast)
                    {
                        if (Cur->ConstVal > 0 && Cur->I32 != TypeVoid)
                        {
                            if (ElemSize <= 0)
                            {
                                ElemSize = (int)Cur->ConstVal;
                                ElemBase = Cur->I32;
                            }
                        }
                        Cur = Cur->Left;
                    }
                    Addr = (void *)(intptr_t)IrEvalExpr(Parser, Cur != NULL ? Cur : Target->Left);
                    if (Addr == NULL)
                        ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                    if (ElemSize <= 0) { ElemSize = (int)sizeof(int); ElemBase = TypeInt; }
                    IrStorePtrElem(Parser, ElemBase, Addr, V);
                    return V;
                }
                if (!IrResolvePtr(Parser, Target, &Addr, &ElemBase, &ElemSize))
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                /* Prefer param/local typed size over IrResolvePtr defaults */
                if (Target != NULL && Target->Kind == IrExIdent
                    && IrCallDepth > 0)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    IrExpr *X = Target;
                    if (X->ParamSlot >= 0 && X->ParamSlot < Fr->NumParams
                        && Fr->ParamIsPtr[X->ParamSlot])
                    {
                        struct ValueType *PT = Fr->ParamTyp[X->ParamSlot];
                        if (PT && PT->FromType && PT->FromType->Sizeof > 0)
                        {
                            ElemSize = PT->FromType->Sizeof;
                            ElemBase = PT->FromType->Base;
                        }
                        else if (ElemSize <= 1)
                        {
                            /* unknown pointee: assume int* (common in tests) */
                            ElemSize = (int)sizeof(int);
                            ElemBase = TypeInt;
                        }
                        Addr = (void *)(intptr_t)Fr->ParamVal[X->ParamSlot];
                    }
                }
                IrStorePtrElem(Parser, ElemBase, Addr, V);
                IrEvalMarkOwnedPointerWrite(Parser, Addr, ElemSize > 0 ? ElemSize : 4);
                /* *p = v where p aliases &s.m / &v[i].m: mark matching owned blob */
                if (Addr != NULL && IrCallDepth > 0)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    int li, sz = ElemSize > 0 ? ElemSize : 4;
                    for (li = 0; li < Fr->NumLocals && li < IR_CALL_MAX_LOCALS; li++)
                    {
                        if (!Fr->LocalOwned[li] || Fr->LocalVal[li] == 0)
                            continue;
                        {
                            char *base = (char *)(intptr_t)Fr->LocalVal[li];
                            int nbytes = 0;
                            if (Fr->LocalFromType[li] != NULL)
                                nbytes = Fr->LocalFromType[li]->Sizeof
                                    * (Fr->LocalArraySize[li] > 0 ? Fr->LocalArraySize[li] : 1);
                            else if (Fr->LocalArraySize[li] > 0)
                                nbytes = Fr->LocalArraySize[li] * (Fr->LocalElemSize[li] > 0 ? Fr->LocalElemSize[li] : 1);
                            if (nbytes <= 0) continue;
                            if ((char *)Addr >= base && (char *)Addr < base + nbytes)
                            {
                                /* frame first (byte map for struct) */
                                IrFrameMarkBytes(Fr, li, Addr, sz);
                                IrShadowMarkAddr(Parser, Fr->LocalName[li], Addr, sz);
                                break;
                            }
                        }
                    }
                }
                return V;
            }
            if (E->Left != NULL && E->Left->Kind == IrExIndex)
            {
                void *Base;
                int ElemBase, ElemSize;
                long long Idx;
                char *P;
                struct Value *AV = NULL;
                IrExpr *RootIx = E->Left;
                if (!IrResolvePtr(Parser, E->Left->Left, &Base, &ElemBase, &ElemSize))
                    ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                Idx = IrEvalExpr(Parser, E->Left->Right);
                /* Root array ident (supports multi-dim a[i][j]) */
                while (RootIx != NULL && RootIx->Kind == IrExIndex)
                    RootIx = RootIx->Left;
                if (RootIx != NULL && RootIx->Kind == IrExIdent
                    && RootIx->Ident != NULL
                    && (IrGlobalValSoft(Parser, RootIx) != NULL))
                {
                    AV = IrGlobalValSoft(Parser, RootIx); /* soft: already gated */
                    if (IrSafeChecksOn() && AV != NULL && AV->Typ != NULL && AV->Typ->Base == TypeArray
                        && AV->Typ->ArraySize > 0
                        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
                        && (Idx < 0 || Idx >= AV->Typ->ArraySize))
                        ProgramFail(Parser, DiagnosticMsgArrayIndexOutOfBoundsSized,
                            (int)Idx, AV->Typ->ArraySize);
                }
                if (RootIx != NULL && RootIx->Kind == IrExIdent
                    && RootIx->LocalSlot >= 0 && IrCallDepth > 0
                    && RootIx->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    int asz = Fr->LocalArraySize[RootIx->LocalSlot];
                    if (IrSafeChecksOn() && asz > 0 && (Idx < 0 || Idx >= asz)
                        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
                    {
                        if (RootIx->Ident != NULL)
                            ParserSetCaretOnIdent(Parser, RootIx->Ident);
                        else
                            ParserSetCaretOnAssignTarget(Parser);
                        ProgramFail(Parser, DiagnosticMsgArrayIndexOutOfBoundsSized,
                            (int)Idx, asz);
                    }
                    unsigned char *imap = Fr->LocalInitMap[RootIx->LocalSlot];
                    if (imap != NULL && Idx >= 0)
                    {
                        /* Flat element index for multi-dim: walk all index
                         * levels so m[i][j] marks the correct cell (not only 1D). */
                        int flat = 0;
                        int nd = (int)Fr->LocalNDims[RootIx->LocalSlot];
                        if (nd >= 2)
                        {
                            IrExpr *Chain[IR_MAX_ARRAY_DIMS];
                            int nc = 0;
                            IrExpr *T = E->Left;
                            /* Chain[0]=innermost index expr, Chain[nc-1]=outermost */
                            while (T != NULL && T->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
                            {
                                Chain[nc++] = T;
                                T = T->Left;
                            }
                            if (nc == nd)
                            {
                                int d;
                                long long acc = IrEvalExpr(Parser, Chain[nc - 1]->Right);
                                for (d = 1; d < nd; d++)
                                {
                                    int dim = Fr->LocalDims[RootIx->LocalSlot][d];
                                    long long ix = IrEvalExpr(Parser, Chain[nc - 1 - d]->Right);
                                    if (dim > 0)
                                        acc = acc * dim + ix;
                                }
                                flat = (int)acc;
                                if (flat >= 0 && flat < Fr->LocalArraySize[RootIx->LocalSlot])
                                    imap[(size_t)flat] = 1;
                            }
                        }
                        else if (E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
                            imap[(size_t)Idx] = 1;
                    }
                }
                P = (char *)Base + (ptrdiff_t)Idx * ElemSize;
#ifndef NO_FP
                /* C assignment conversions for scalar FP array elements. */
                if (E->Right != NULL && ElemBase != TypePointer
                    && ElemBase != TypeStruct && ElemBase != TypeUnion)
                {
                    int RF = IrExprIsFP(Parser, E->Right);
                    if (ElemBase == TypeFP && !RF)
                        V = IrFPBits((double)V);
                    else if (ElemBase != TypeFP && RF)
                        V = (long long)IrBitsFP(V);
                }
#endif
                /* Whole-struct / large element: V is pointer to source payload.
                 * Pointer-array elements (struct Edge *adj[N]) store a word. */
                if (ElemBase != TypePointer
                    && (ElemSize > (int)sizeof(long long)
                        || ElemBase == TypeStruct || ElemBase == TypeUnion
                        || (AV != NULL && AV->Typ != NULL && AV->Typ->Base == TypeArray
                            && AV->Typ->FromType != NULL
                            && (AV->Typ->FromType->Base == TypeStruct
                                || AV->Typ->FromType->Base == TypeUnion))))
                {
                    int sz = ElemSize;
                    if (sz <= 0 && AV != NULL && AV->Typ != NULL && AV->Typ->FromType != NULL)
                        sz = AV->Typ->FromType->Sizeof;
                    if (sz > 0 && V != 0)
                        memcpy(P, (void *)(intptr_t)V, (size_t)sz);
                    else if (sz > 0)
                        memset(P, 0, (size_t)sz);
                }
                else
                    IrStorePtrElem(Parser, ElemBase, P, V);
                IrEvalMarkOwnedPointerWrite(Parser, P, ElemSize > 0 ? ElemSize : 4);
                /* Dual-write shadow ElemInit only when present (token interop) */
                if (AV != NULL && AV->ElemInit != NULL && ElemSize > 0)
                    IrValueMarkAddr(AV, P, ElemSize);
                /* IR local array/struct: mark frame map for written element */
                if (E->Left != NULL && E->Left->Kind == IrExIndex
                    && IrCallDepth > 0 && ElemSize > 0)
                {
                    IrExpr *RootIx = E->Left;
                    while (RootIx != NULL && RootIx->Kind == IrExIndex && RootIx->Left != NULL)
                        RootIx = RootIx->Left;
                    if (RootIx != NULL && RootIx->Kind == IrExIdent
                        && RootIx->LocalSlot >= 0
                        && RootIx->LocalSlot < IR_CALL_MAX_LOCALS)
                        IrFrameMarkBytes(IrCallStack[IrCallDepth - 1],
                                         RootIx->LocalSlot, P, ElemSize);
                }
                /* Member-array store: mark frame byte map + parent Variable */
                if (E->Left->Left != NULL && E->Left->Left->Kind == IrExMember)
                {
                    IrExpr *Root = E->Left->Left;
                    while (Root != NULL && Root->Kind != IrExIdent
                           && Root->Left != NULL
                           && (Root->Kind == IrExIndex || Root->Kind == IrExMember))
                        Root = Root->Left;
                    if (Root != NULL && Root->Kind == IrExIdent
                        && Root->LocalSlot >= 0 && IrCallDepth > 0
                        && Root->LocalSlot < IR_CALL_MAX_LOCALS && ElemSize > 0)
                        IrFrameMarkBytes(IrCallStack[IrCallDepth - 1],
                                         Root->LocalSlot, P, ElemSize);
                    if (Root != NULL && Root->Kind == IrExIdent)
                        IrShadowMarkAddr(Parser, Root->Ident, P, ElemSize);
                }
                return V;
            }
            if (E->Left != NULL && E->Left->Kind == IrExMember)
            {
                void *Addr;
                int Eb;
                if (!IrResolveMember(Parser, E->Left, &Addr, &Eb))
                    ProgramFail(Parser, "invalid member assignment");
                if (E->Left->BitWidth > 0)
                    IrBitInsert(Addr, E->Left->BitOffset, E->Left->BitWidth, V);
#ifndef NO_FP
                else if (Eb == TypeFP || E->Left->I32 == TypeFP)
                {
                    union { long long l; double d; } u;
                    u.l = V;
                    *(double *)Addr = u.d;
                }
#endif
                else if (Eb == TypePointer || Eb == TypeArray
                         || E->Left->I32 == TypePointer
                         || (E->Right != NULL && IrExprYieldsPointer(Parser, E->Right)))
                    *(void **)Addr = (void *)(intptr_t)V;
                else
                    IrStorePtrElem(Parser, Eb, Addr, V);
                /* Mark parent aggregate: frame byte map first, then shadow. */
                {
                    IrExpr *Root = E->Left;
                    int sz = 4;
                    if (E->Left->BitWidth > 0)
                        sz = 4;
                    else if (Eb == TypeChar || Eb == TypeUnsignedChar) sz = 1;
                    else if (Eb == TypeShort || Eb == TypeUnsignedShort) sz = 2;
                    else if (Eb == TypePointer || Eb == TypeArray)
                        sz = (int)sizeof(void *);
                    else if (Eb == TypeLong || Eb == TypeUnsignedLong) sz = (int)sizeof(long);
                    else if (Eb == TypeLongLong || Eb == TypeUnsignedLongLong) sz = 8;
                    while (Root->Left != NULL
                           && (Root->Left->Kind == IrExMember
                               || Root->Left->Kind == IrExIndex
                               || Root->Left->Kind == IrExIdent))
                        Root = Root->Left;
                    if (Root->Kind == IrExIdent && Root->LocalSlot >= 0
                        && IrCallDepth > 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                        IrFrameMarkBytes(IrCallStack[IrCallDepth - 1],
                                         Root->LocalSlot, Addr, sz);
                    if (Root->Kind == IrExIdent)
                        IrShadowMarkAddrEx(Parser, Root->Ident, Addr, sz, 1);
                }
                return V;
            }
            if (E->Left != NULL && E->Left->LocalSlot >= 0 && IrCallDepth > 0)
            {
                IrCallFrame *F = IrCallStack[IrCallDepth - 1];
                if (E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    /* copy fptr from another local */
                    if (E->Right != NULL && E->Right->Kind == IrExIdent
                        && E->Right->LocalSlot >= 0
                        && E->Right->LocalSlot < IR_CALL_MAX_LOCALS
                        && F->LocalIsFptr[E->Right->LocalSlot])
                    {
                        F->LocalVal[E->Left->LocalSlot] = F->LocalVal[E->Right->LocalSlot];
                        F->LocalIsFptr[E->Left->LocalSlot] = 1;
                        F->LocalIsPtr[E->Left->LocalSlot] = 0;
                    }
                    else
                    {
                        /* Skip store only for owned array/struct blobs (fixed base).
                         * Pointer locals (IsPtr, !Owned) must accept the store even
                         * when LocalArraySize was left non-zero (EXEC=both residual
                         * on struct N *p after struct value Decl pollution). */
                        int skip_store = F->LocalOwned[E->Left->LocalSlot]
                            && F->LocalArraySize[E->Left->LocalSlot] > 0;
                        if (!skip_store)
                        {
                            /* scalar int/FP local or pointer value (not array base) */
                            if (!F->LocalIsPtr[E->Left->LocalSlot])
                            {
#ifndef NO_FP
                                /* int → float local: store IEEE bits */
                                if (F->LocalFromType[E->Left->LocalSlot] != NULL
                                    && F->LocalFromType[E->Left->LocalSlot]->Base == TypeFP
                                    && !(E->Right != NULL && IrExprIsFP(Parser, E->Right)))
                                    V = IrFPBits((double)V);
                                /* float → int local: truncate */
                                else if (E->Right != NULL && IrExprIsFP(Parser, E->Right)
                                    && (F->LocalFromType[E->Left->LocalSlot] == NULL
                                        || F->LocalFromType[E->Left->LocalSlot]->Base != TypeFP))
                                    V = (long long)IrBitsFP(V);
                                else
#endif
                                V = IrNarrowToLocalType(F, E->Left->LocalSlot, V);
                            }
                            F->LocalVal[E->Left->LocalSlot] = V;
                            /* Pointer store: drop any stale ArraySize so later
                             * p->m does not treat the slot as an owned blob. */
                            if (F->LocalIsPtr[E->Left->LocalSlot]
                                && !F->LocalOwned[E->Left->LocalSlot])
                            {
                                F->LocalArraySize[E->Left->LocalSlot] = 0;
                                F->LocalNDims[E->Left->LocalSlot] = 0;
                            }
                        }
                        /* Sync Variable table ONLY for pointer/fptr shadows that
                         * were VariableDefine'd for mixed IR/token paths.
                         * Never sync scalar locals: outer scopes often have the
                         * same name (for i nested with callee for i) and writing
                         * them causes infinite outer loops. */
                        if (E->Left->Ident != NULL
                            && (F->LocalIsPtr[E->Left->LocalSlot]
                                || F->LocalIsFptr[E->Left->LocalSlot])
                            && (IrGlobalValSoft(Parser, E->Left) != NULL))
                        {
                            struct Value *LV = NULL;
                            LV = IrGlobalValSoft(Parser, E->Left); /* soft: already gated */
                            if (LV != NULL && LV->Val != NULL
                                && LV->Typ != NULL && LV->Typ->Base == TypePointer)
                                LV->Val->Pointer = (void *)(intptr_t)V;
                        }
                    }
                    F->LocalInit[E->Left->LocalSlot] = 1;
                    return V;
                }
            }
            /* update IR call frame param by compile-time slot (preferred) */
            if (E->Left != NULL && E->Left->ParamSlot >= 0 && IrCallDepth > 0)
            {
                IrCallFrame *F = IrCallStack[IrCallDepth - 1];
                if (E->Left->ParamSlot < F->NumParams)
                {
                    F->ParamVal[E->Left->ParamSlot] = V;
                    return V;
                }
            }
            if (IrCallDepth > 0)
            {
                int i;
                IrCallFrame *F = IrCallStack[IrCallDepth - 1];
                for (i = 0; i < F->NumParams; i++)
                    if (F->ParamName[i] == E->Left->Ident ||
                        (F->ParamName[i] && E->Left->Ident &&
                         strcmp(F->ParamName[i], E->Left->Ident) == 0))
                    {
                        F->ParamVal[i] = V;
                        return V;
                    }
            }
            /* Must not use ExpressionAssignInt on pointers — it is a no-op
             * for TypePointer (default: break). That left p = p->next as a
             * silent no-op in IR loop bodies → hang / wrong list walks.
             * LocalSlot/ParamSlot already handled above — this is global only. */
            if (E->Left == NULL || E->Left->Ident == NULL)
                ProgramFail(Parser, "invalid assignment");
            Var = IrGlobalValSoft(Parser, E->Left); /* soft: already gated */
            if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypePointer)
            {
                Var->Val->Pointer = (void *)(intptr_t)V;
                IrValueMarkAll(Var);
            }
#ifndef NO_FP
            else if (Var != NULL && Var->Typ != NULL && Var->Typ->Base == TypeFP)
            {
                Var->Val->FP = IrBitsFP(V);
                IrValueMarkAll(Var);
            }
#endif
            else
                ExpressionAssignInt(Parser, Var, (PTR_TYPE_INT_SIGNED)V, FALSE);
            return V;
        }
        IR_EVAL_CASE(IrExCall)
        {
            struct Value *FuncValue = NULL;
            /* Member/expr callee: Ident is NULL, Left holds p->fp / (*fp).
             * Must not lookup NULL Ident — that SEGV in strlen. */
            if (E->Ident == NULL && E->Left != NULL)
            {
                long long ArgsL[16];
                int ai;
                struct Value *FV = NULL;
                void *Addr = NULL;
                int Eb = TypeInt, Es = 1;
                if (E->ArgCount > 16)
                    ProgramFail(Parser, "too many arguments");
                for (ai = 0; ai < E->ArgCount; ai++)
                    ArgsL[ai] = IrEvalExpr(Parser, E->Args[ai]);
                if (E->Left->Kind == IrExMember)
                {
                    if (!IrResolveMember(Parser, E->Left, &Addr, &Eb))
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    FV = *(struct Value **)Addr;
                    if (FV != NULL && FV->Typ != NULL
                        && FV->Typ->Base == TypePointer
                        && FV->Typ->FromType != NULL
                        && FV->Typ->FromType->Base == TypeFunction)
                        FV = (struct Value *)FV->Val->Pointer;
                }
                else if (E->Left->Kind == IrExIndex)
                {
                    void *Base = NULL;
                    int Es2 = (int)sizeof(void *);
                    long long Idx;
                    char *Slot;
                    if (!IrResolvePtr(Parser, E->Left->Left, &Base, &Eb, &Es2) || Base == NULL)
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    Idx = IrEvalExpr(Parser, E->Left->Right);
                    Slot = (char *)Base + (ptrdiff_t)Idx * (ptrdiff_t)sizeof(void *);
                    FV = *(struct Value **)Slot;
                }
                else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                         && E->Left->Left != NULL && E->Left->Left->Kind == IrExIndex)
                {
                    void *Base = NULL;
                    int Es2 = (int)sizeof(void *);
                    long long Idx;
                    char *Slot;
                    if (!IrResolvePtr(Parser, E->Left->Left->Left, &Base, &Eb, &Es2) || Base == NULL)
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    Idx = IrEvalExpr(Parser, E->Left->Left->Right);
                    Slot = (char *)Base + (ptrdiff_t)Idx * (ptrdiff_t)sizeof(void *);
                    FV = *(struct Value **)Slot;
                }
                else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                         && E->Left->Left != NULL && E->Left->Left->Kind == IrExMember)
                {
                    /* (*h.op)(args) — load fptr from member then call */
                    if (!IrResolveMember(Parser, E->Left->Left, &Addr, &Eb))
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    FV = *(struct Value **)Addr;
                    if (FV != NULL && FV->Typ != NULL
                        && FV->Typ->Base == TypePointer
                        && FV->Typ->FromType != NULL
                        && FV->Typ->FromType->Base == TypeFunction)
                        FV = (struct Value *)FV->Val->Pointer;
                }
                else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                         && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
                {
                    IrExpr *X = E->Left->Left;
                    if (X->LocalSlot >= 0 && IrCallDepth > 0
                        && X->LocalSlot < IR_CALL_MAX_LOCALS
                        && IrCallStack[IrCallDepth - 1]->LocalIsFptr[X->LocalSlot])
                        FV = (struct Value *)(intptr_t)IrCallStack[IrCallDepth - 1]->LocalVal[X->LocalSlot];
                    else if (X->Ident != NULL && (IrGlobalValSoft(Parser, X) != NULL)
                             && (X->LocalSlot < 0 && X->ParamSlot < 0))
                    {
                        /* global fptr: storage holds Function Value* */
                        struct Value *GV = NULL;
                        GV = IrGlobalValSoft(Parser, X); /* soft: already gated */
                        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypePointer
                            && GV->Typ->FromType != NULL
                            && GV->Typ->FromType->Base == TypeFunction
                            && GV->Val != NULL)
                            FV = (struct Value *)GV->Val->Pointer;
                    }
                    else if (IrResolvePtr(Parser, X, &Addr, &Eb, &Es) && Addr != NULL)
                        FV = *(struct Value **)Addr;
                }
                else if (E->Left->Kind == IrExIdent)
                {
                    IrExpr *X = E->Left;
                    if (X->LocalSlot >= 0 && IrCallDepth > 0
                        && X->LocalSlot < IR_CALL_MAX_LOCALS
                        && IrCallStack[IrCallDepth - 1]->LocalIsFptr[X->LocalSlot])
                        FV = (struct Value *)(intptr_t)IrCallStack[IrCallDepth - 1]->LocalVal[X->LocalSlot];
                    else if (X->ParamSlot >= 0 && IrCallDepth > 0
                             && X->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                    {
                        /* formal fptr param: ParamVal holds Function Value* */
                        FV = (struct Value *)(intptr_t)IrCallStack[IrCallDepth - 1]->ParamVal[X->ParamSlot];
                    }
                    else if (X->Ident != NULL && (IrGlobalValSoft(Parser, X) != NULL))
                    {
                        struct Value *LV = NULL;
                        LV = IrGlobalValSoft(Parser, X); /* soft: already gated */
                        if (LV != NULL && LV->Typ != NULL && LV->Typ->Base == TypePointer
                            && LV->Typ->FromType != NULL
                            && LV->Typ->FromType->Base == TypeFunction
                            && LV->Val != NULL)
                            FV = (struct Value *)LV->Val->Pointer;
                    }
                }
                else if (E->Left->Kind == IrExIndex)
                {
                    /* tab[i](...) — element is fptr */
                    void *Base = NULL;
                    int Eb = TypeInt, Es = sizeof(void *);
                    long long Idx;
                    char *Slot;
                    if (!IrResolvePtr(Parser, E->Left->Left, &Base, &Eb, &Es))
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    Idx = IrEvalExpr(Parser, E->Left->Right);
                    if (Es < (int)sizeof(void *))
                        Es = (int)sizeof(void *);
                    Slot = (char *)Base + (ptrdiff_t)Idx * Es;
                    FV = *(struct Value **)Slot;
                }
                if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction)
                    ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                return IrCallUserFuncCached(Parser, FV, ArgsL, IrBuildCallArgTypes(Parser, E, CallArgTypes, IR_CALL_MAX_PARAMS + 32), E->ArgCount);
            }
            /* Hard lookup: an unresolved callee (call to an undeclared function)
             * must fail with "'name' is undefined", not SEGV on the NULL Value*
             * returned by the soft lookup. */
            FuncValue = IrGlobalValSoft(Parser, E);
            if (FuncValue == NULL && E->Ident != NULL)
                FuncValue = E->CachedVal = IrCalleeLookupOrFail(Parser, E->Ident,
                    E->SourceLine, E->CharacterPos);
            if (FuncValue == NULL)
                ProgramFail(Parser, DiagnosticMsgFunctionUndefined);
            if (FuncValue->Typ->Base == TypeFunction &&
                FuncValue->Val->FuncDef.Intrinsic != NULL)
            {
                /* Intrinsic (printf/sprintf grey-list): build Value* args */
                struct Value *ReturnValue;
                struct Value **ParamArray;
                int Count;
                long long Result = 0;
                enum RunMode SavedMode = Parser->Mode;
                if (E->ArgCount > 16)
                    ProgramFail(Parser, "too many arguments");
                HeapPushStackFrame(Parser->pc);
                ReturnValue = IrPackReturnValue(Parser, FuncValue->Val->FuncDef.ReturnType); /* B.3 */
                ParamArray = HeapAllocStack(Parser->pc,
                    sizeof(struct Value *) * (E->ArgCount + 1));
                if (ParamArray == NULL)
                    ProgramFail(Parser, DiagnosticMsgOutOfMemory);
                for (Count = 0; Count < E->ArgCount; Count++)
                {
                    IrExpr *A = E->Args[Count];
                    /* Always allocate on the picoc stack so StdioBasePrintf's
                     * contiguous Value walk (TypeStackSizeValue) works. */
                    if (A->Kind == IrExConst && A->Op == TokenStringConstant)
                    {
                        ParamArray[Count] = IrPackPtrArg(Parser, A->ConstVal, A);
                    }
                    else if (A->Kind == IrExUnary && A->Op == TokenAmpersand)
                    {
                        /* &x before IrExprIsFP — &float is pointer, not FP bits */
                        long long V = IrEvalExpr(Parser, A);
                        ParamArray[Count] = IrPackPtrArg(Parser, V, A);
                    }
#ifndef NO_FP
                    else if (A != NULL && IrExprIsFP(Parser, A))
                    {
                        long long bits = IrEvalExpr(Parser, A);
                        ParamArray[Count] = IrPackFPArg(Parser, bits);
                    }
#endif
                    /* Ternary/ampersand/etc. that yield pointer bits (not just bare ident) */
                    else if (A != NULL && IrExprYieldsPointer(Parser, A)
                             && !(A->Kind == IrExIdent
                                  && (A->LocalSlot >= 0 || A->ParamSlot >= 0)))
                    {
                        long long V = IrEvalExpr(Parser, A);
                        ParamArray[Count] = IrPackPtrArg(Parser, V, A);
                    }
                    else if (A->Kind == IrExIdent &&
                             (A->LocalSlot >= 0 || A->ParamSlot >= 0))
                    {
                        long long V = IrEvalExpr(Parser, A);
                        struct ValueType *PTyp = NULL;
                        int as_ptr = 0;
                        if (Count < FuncValue->Val->FuncDef.NumParams)
                            PTyp = FuncValue->Val->FuncDef.ParamType
                                ? FuncValue->Val->FuncDef.ParamType[Count] : NULL;
                        /* Formal pointer (free/memcpy/...) always packs Pointer field —
                         * Claude free_int_ptr SEGV when LocalIsPtr miss → Integer field. */
                        if (PTyp != NULL && (PTyp->Base == TypePointer || PTyp->Base == TypeArray))
                            as_ptr = 1;
                        if (A->LocalSlot >= 0 && IrCallDepth > 0
                            && A->LocalSlot < IR_CALL_MAX_LOCALS
                            && IrCallStack[IrCallDepth - 1]->LocalIsPtr[A->LocalSlot])
                            as_ptr = 1;
                        else if (A->ParamSlot >= 0 && IrCallDepth > 0
                                 && A->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams
                                 && IrCallStack[IrCallDepth - 1]->ParamIsPtr[A->ParamSlot])
                            as_ptr = 1;
                        if (as_ptr)
                            ParamArray[Count] = IrPackPtrArg(Parser, V, A);
                        else
                            ParamArray[Count] = IrPackIntArg(Parser, V, A);
                    }
                    else if (A->Kind == IrExIdent
                             && A->LocalSlot < 0 && A->ParamSlot < 0)
                    {
                        struct Value *Src;
                        Src = IrGlobalVal(Parser, A); /* global only via CachedVal */
                        if (Src->Typ->Base == TypeFunction)
                        {
                            /* Function name as argument (qsort/bsearch comparator,
                             * pthread_create start routine): pass the function
                             * Value* so the intrinsic can call it back. */
                            ParamArray[Count] = IrPackPtrArg(Parser,
                                (long long)(intptr_t)Src, A);
                        }
                        else if (Src->Typ->Base == TypeArray)
                        {
                            /* array decays to pointer (sprintf buffer) */
                            ParamArray[Count] = IrPackPtrArg(Parser,
                                (long long)(intptr_t)Src->Val, A);
                        }
                        else if (Src->Typ->Base == TypePointer)
                        {
                            ParamArray[Count] = IrPackPtrArg(Parser,
                                (long long)(intptr_t)Src->Val->Pointer, A);
                        }
#ifndef NO_FP
                        else if (Src->Typ->Base == TypeFP)
                        {
                            union { double d; long long l; } u;
                            u.d = Src->Val->FP;
                            ParamArray[Count] = IrPackFPArg(Parser, u.l);
                        }
#endif
                        else
                        {
                            ParamArray[Count] = IrPackIntArg(Parser,
                                IrValueAsInt64(Src), A);
                        }
                    }
                    else if (A->Kind == IrExMember)
                    {
                        void *MAddr = NULL;
                        int Eb = (A->CanonicalFlags & IR_CF_VALID)
                            ? A->CanonicalBase
                            : (A->I32 >= TypeVoid && A->I32 <= TypeFunction)
                                ? A->I32 : TypeInt;
                        int Es = 1;
                        int isArr = (Eb == TypeArray);
                        int isPtr = !isArr && (Eb == TypePointer
                            || (A->TypeFlags & IR_TF_PTR) != 0);
                        long long MemberValueBits;
                        /* Static result type first; evaluating the member once is
                         * enough. Resolving it here and then evaluating it below
                         * duplicated calls/va_arg in the base expression. */
                        MemberValueBits = IrEvalExpr(Parser, A);
                        MAddr = (void *)(intptr_t)MemberValueBits;
                        if (isArr)
                        {
                            int markSz = 0;
                            ParamArray[Count] = IrPackPtrArg(Parser,
                                MemberValueBits, A);
                            /* Escape via decay: mark LocalInitMap + shadow bridge
                             * (member array may span many initmap slots; uninit
                             * checks still consult shadow ElemInit for scalars). */
                            if (A->Left != NULL && A->Left->Kind == IrExIdent
                                && A->Left->Ident != NULL
                                && (IrGlobalValSoft(Parser, A->Left) != NULL))
                            {
                                struct Value *SV = NULL;
                                struct Value *MV = NULL;
                                struct ValueType *ST = NULL;
                                SV = IrShadowGet(Parser, A->Left->Ident); /* shadow bridge */
                                if (SV != NULL && SV->Typ != NULL)
                                {
                                    ST = SV->Typ;
                                    if (A->Op == TokenArrow && ST->Base == TypePointer)
                                        ST = ST->FromType;
                                    if (ST != NULL && ST->Members != NULL
                                        && TableGet(ST->Members, A->Ident, &MV, NULL, NULL, NULL)
                                        && MV != NULL && MV->Typ != NULL
                                        && MV->Typ->Base == TypeArray)
                                        markSz = MV->Typ->Sizeof > 0 ? MV->Typ->Sizeof
                                            : (MV->Typ->ArraySize * (Es > 0 ? Es : 1));
                                }
                                if (SV != NULL && SV->ElemInit != NULL && markSz > 0)
                                    IrValueMarkAddr(SV, MAddr, markSz);
                            }
                            if (A->Left != NULL && A->Left->Kind == IrExIdent
                                && A->Left->LocalSlot >= 0 && IrCallDepth > 0
                                && A->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                            {
                                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                                int ls = A->Left->LocalSlot;
                                Fr->LocalInit[ls] = 1;
                                if (Fr->LocalInitMap[ls] != NULL && Fr->LocalVal[ls] != 0
                                    && MAddr != NULL)
                                {
                                    char *base = (char *)(intptr_t)Fr->LocalVal[ls];
                                    int esz = Fr->LocalElemSize[ls] > 0 ? Fr->LocalElemSize[ls] : 1;
                                    int asz = Fr->LocalArraySize[ls];
                                    int off = (int)((char *)MAddr - base);
                                    int nbytes = markSz > 0 ? markSz : (Es > 0 ? Es : 1);
                                    if (off >= 0 && asz > 0 && esz > 0)
                                    {
                                        int ei0 = off / esz;
                                        int ei1 = (off + nbytes + esz - 1) / esz;
                                        int ei;
                                        if (ei0 < 0) ei0 = 0;
                                        if (ei1 > asz) ei1 = asz;
                                        for (ei = ei0; ei < ei1; ei++)
                                            Fr->LocalInitMap[ls][ei] = 1;
                                    }
                                }
                            }
                        }
                        else
                        {
                            ParamArray[Count] = isPtr
                                ? IrPackPtrArg(Parser, MemberValueBits, A)
                                : IrPackIntArg(Parser, MemberValueBits, A);
                        }
                    }
                    else if (A->Kind == IrExIndex ||
                             (A->Kind == IrExUnary && A->Op == TokenAsterisk))
                    {
                        /* The result type determines packing; probing the base via
                         * IrResolvePtr before evaluating A runs side effects twice. */
                        long long V = IrEvalExpr(Parser, A);
                        int IsPtr = A->I32 == TypePointer
                            || (A->TypeFlags & IR_TF_PTR) != 0
                            || (A->CanonicalFlags & IR_CF_PTR) != 0;
#ifndef NO_FP
                        if (A->I32 == TypeFP || (A->TypeFlags & IR_TF_FP))
                            ParamArray[Count] = IrPackFPArg(Parser, V);
                        else
#endif
                        if (IsPtr)
                            ParamArray[Count] = IrPackPtrArg(Parser, V, A);
                        else
                            ParamArray[Count] = IrPackIntArg(Parser, V, A);
                    }
                    else
                    {
                        long long V = IrEvalExpr(Parser, A);
                        /* Formal param is pointer (e.g. memset(void*,...)): integer
                         * bits are an address — pack as Pointer so 0x10 is not read
                         * as NULL from the Integer field (407_lib_crash_cross). */
                        {
                            struct ValueType *PTyp = NULL;
                            if (Count < FuncValue->Val->FuncDef.NumParams)
                                PTyp = FuncValue->Val->FuncDef.ParamType[Count];
                            if (PTyp != NULL && (PTyp->Base == TypePointer
                                || PTyp->Base == TypeArray))
                                ParamArray[Count] = IrPackPtrArg(Parser, V, A);
                            else
                                ParamArray[Count] = IrPackIntArg(Parser, V, A);
                        }
                    }
                }
                if (E->SourceLine)
                    Parser->Line = E->SourceLine;
                if (E->CharacterPos)
                    Parser->CharacterPos = E->CharacterPos;
                IrHostCoerceParams(Parser, FuncValue, ParamArray, E->ArgCount);
                PicocCallIntrinsic(Parser, FuncValue->Val->FuncDef.Intrinsic,
                    ReturnValue, ParamArray, E->ArgCount,
                    E->Ident ? E->Ident : "?");
                /* Library/intrinsic is opaque: any pointer arg may have been written. */
                IrMarkAllPtrArgsEscaped(Parser, E);
                if (ReturnValue->Typ != &Parser->pc->VoidType &&
                    ReturnValue->Typ->Base != TypeVoid)
                    Result = IR_RET_BITS(ReturnValue);
                HeapPopStackFrame(Parser->pc);
                Parser->Mode = SavedMode;
                return Result;
            }
            /* Up to 8 arguments in the evaluator's frame array; wider calls
             * (sum10(...)) get exact-size storage on demand, so the recursive
             * evaluator's frame does not grow. Was: silent "return 0". */
            ArgV = (E->ArgCount <= 8) ? Args
                : (long long *)alloca(sizeof(long long) * (size_t)E->ArgCount);
            IrEvalPreserveArrayInitOnCallArg++;
            for (i = 0; i < E->ArgCount; i++)
                ArgV[i] = IrEvalExpr(Parser, E->Args[i]);
            IrEvalPreserveArrayInitOnCallArg--;
            /* Callee is expression: p->fp(...), (*fp)(...), local fptr(...) */
            if (E->Left != NULL && E->Ident == NULL)
            {
                struct Value *FV = NULL;
                void *Addr = NULL;
                int Eb = TypeInt, Es = 1;
                if (E->Left->Kind == IrExMember)
                {
                    if (!IrResolveMember(Parser, E->Left, &Addr, &Eb))
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    /* fptr member: slot holds Value* (TypeFunction) or pointer-to-function */
                    FV = *(struct Value **)Addr;
                    if (FV != NULL && FV->Typ != NULL
                        && FV->Typ->Base == TypePointer
                        && FV->Typ->FromType != NULL
                        && FV->Typ->FromType->Base == TypeFunction)
                    {
                        FV = (struct Value *)FV->Val->Pointer;
                    }
                }
                else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                         && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
                {
                    /* (*fp)(...) — fp holds function Value* */
                    IrExpr *X = E->Left->Left;
                    if (X->LocalSlot >= 0 && IrCallDepth > 0
                        && X->LocalSlot < IR_CALL_MAX_LOCALS
                        && IrCallStack[IrCallDepth - 1]->LocalIsFptr[X->LocalSlot])
                        FV = (struct Value *)(intptr_t)IrCallStack[IrCallDepth - 1]->LocalVal[X->LocalSlot];
                    else if (IrResolvePtr(Parser, X, &Addr, &Eb, &Es) && Addr != NULL)
                        FV = *(struct Value **)Addr;
                }
                else if (E->Left->Kind == IrExIdent)
                {
                    IrExpr *X = E->Left;
                    if (X->LocalSlot >= 0 && IrCallDepth > 0
                        && X->LocalSlot < IR_CALL_MAX_LOCALS
                        && IrCallStack[IrCallDepth - 1]->LocalIsFptr[X->LocalSlot])
                        FV = (struct Value *)(intptr_t)IrCallStack[IrCallDepth - 1]->LocalVal[X->LocalSlot];
                }
                if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction)
                    ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                return IrCallUserFuncCached(Parser, FV, ArgV, IrBuildCallArgTypes(Parser, E, CallArgTypes, IR_CALL_MAX_PARAMS + 32), E->ArgCount);
            }
            /* Resolve callee. Named functions: cache. Fptrs: always re-resolve.
             * B.3: prefer CachedFunc before TableGet/VarGet on hot path. */
            {
                struct Value *FV = NULL;
                int IsFptr = 0;
                if (IrCallDepth > 0)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    int li;
                    if (E->LocalSlot >= 0 && E->LocalSlot < IR_CALL_MAX_LOCALS
                        && Fr->LocalIsFptr[E->LocalSlot])
                    {
                        FV = (struct Value *)(intptr_t)Fr->LocalVal[E->LocalSlot];
                        IsFptr = 1;
                    }
                    else if (E->Ident != NULL)
                    {
                        for (li = 0; li < IR_CALL_MAX_LOCALS; li++)
                            if (Fr->LocalIsFptr[li]
                                && Fr->LocalName[li] != NULL
                                && strcmp(Fr->LocalName[li], E->Ident) == 0)
                            {
                                FV = (struct Value *)(intptr_t)Fr->LocalVal[li];
                                IsFptr = 1;
                                break;
                            }
                    }
                }
                /* Named non-fptr: hit cache first (no Get) */
                if (FV == NULL && !IsFptr && E->CachedFunc != NULL
                    && E->CachedFunc->Typ != NULL
                    && E->CachedFunc->Typ->Base == TypeFunction
                    && (E->CachedFunc->Val->FuncDef.BodyIr != NULL
                        || E->CachedFunc->Val->FuncDef.Body.Pos != NULL))
                {
                    FV = E->CachedFunc;
                    return IrCallUserFuncCached(Parser, FV, ArgV, IrBuildCallArgTypes(Parser, E, CallArgTypes, IR_CALL_MAX_PARAMS + 32), E->ArgCount);
                }
                if (FV == NULL && E->Ident != NULL)
                {
                    if (!TableGet(&Parser->pc->GlobalTable, E->Ident, &FV, NULL, NULL, NULL)
                        || FV == NULL)
                    {
                        if ((IrGlobalValSoft(Parser, E) != NULL))
                            FV = IrGlobalVal(Parser, E); /* global/func via CachedVal */
                    }
                }
                if (FV != NULL && FV->Typ != NULL)
                {
                    if (FV->Typ->Base == TypePointer && FV->Typ->FromType != NULL
                        && FV->Typ->FromType->Base == TypeFunction)
                    {
                        IsFptr = 1;
                        FV = (struct Value *)FV->Val->Pointer;
                        if (FV == NULL)
                            ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    }
                }
                if (IsFptr)
                {
                    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction)
                        ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    return IrCallUserFuncCached(Parser, FV, ArgV, IrBuildCallArgTypes(Parser, E, CallArgTypes, IR_CALL_MAX_PARAMS + 32), E->ArgCount);
                }
                /* Cache only complete definitions (BodyIr or token body).
                 * Never cache a forward prototype - it may be freed/replaced. */
                if (FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction
                    && (FV->Val->FuncDef.BodyIr != NULL
                        || FV->Val->FuncDef.Body.Pos != NULL))
                {
                    E->CachedFunc = FV;
                    if (E->Ident != NULL)
                    {
                        IrFuncMeta *CM = IrFuncMetaGet(FV);
                        if (CM != NULL && CM->Name == NULL)
                            CM->Name = E->Ident;
                    }
                    return IrCallUserFuncCached(Parser, FV, ArgV, IrBuildCallArgTypes(Parser, E, CallArgTypes, IR_CALL_MAX_PARAMS + 32), E->ArgCount);
                }
                /* Named resolve: live path (handles late definition) */
                if (E->Ident != NULL)
                    return IrCallUserFunc(Parser, E->Ident, ArgV, IrBuildCallArgTypes(Parser, E, CallArgTypes, IR_CALL_MAX_PARAMS + 32), E->ArgCount);
                ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
            }
        }
        IR_EVAL_DEFAULT
            return 0;
#if !BABYC_IR_EVAL_CGOTO
    }
#endif
#undef IR_EVAL_CASE
#undef IR_EVAL_DEFAULT
}


/* ---- Fuse eval (from ir_exec; called by IrEvalExpr, not ir_fast) ---- */
static int IrFuseReadIdentInt(IrExpr *E, long long *Out)
{
    IrCallFrame *Fr;
    if (E == NULL || E->Kind != IrExIdent || IrCallDepth <= 0)
        return 0;
    if (E->TypeFlags & IR_TF_FP)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    if (E->LocalSlot >= 0 && E->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        if (Fr->LocalIsPtr[E->LocalSlot] || Fr->LocalIsFptr[E->LocalSlot]
            || Fr->LocalArraySize[E->LocalSlot] > 0)
            return 0;
        if (!Fr->LocalInit[E->LocalSlot]
            && !(E->TypeFlags & IR_TF_DEFINITELY_INIT)
            && IrSafeChecksOn())
            return 0;
        *Out = Fr->LocalVal[E->LocalSlot];
        return 1;
    }
    if (E->ParamSlot >= 0 && E->ParamSlot < Fr->NumParams)
    {
        if (Fr->ParamIsPtr[E->ParamSlot])
            return 0;
        *Out = Fr->ParamVal[E->ParamSlot];
        return 1;
    }
    return 0;
}

static int IrFuseIndexVal(IrExpr *IdxE, long long *Out)
{
    long long A, B;
    if (IdxE == NULL)
        return 0;
    if (IrFuseReadIdentInt(IdxE, Out))
        return 1;
    if (IdxE->Kind == IrExConst)
    {
        *Out = IdxE->ConstVal;
        return 1;
    }
    if (IdxE->Kind != IrExBinOp)
        return 0;
    if (IdxE->Op != TokenPlus && IdxE->Op != TokenMinus)
        return 0;
    if (IdxE->TypeFlags & (IR_TF_FP | IR_TF_PTR))
        return 0;
    if (!IrFuseIndexVal(IdxE->Left, &A))
        return 0;
    if (!IrFuseIndexVal(IdxE->Right, &B))
        return 0;
    /* BabyC defines integer arithmetic as wrapping. Keep this fast fused
     * path free of host signed-overflow UB, just like IrApplyBinOpFlags. */
    *Out = (IdxE->Op == TokenPlus)
        ? (long long)((unsigned long long)A + (unsigned long long)B)
        : (long long)((unsigned long long)A - (unsigned long long)B);
    return 1;
}

static int IrFuseReadIndexInt(struct ParseState *Parser, IrExpr *Ix, long long *Out)
{
    IrCallFrame *Fr;
    IrExpr *Base, *IdxE;
    long long Idx;
    int slot, es;
    unsigned char *imap;
    char *P;
    (void)Parser;
    if (Ix == NULL || Ix->Kind != IrExIndex || IrCallDepth <= 0)
        return 0;
    if (Ix->TypeFlags & IR_TF_FP)
        return 0;
    Base = Ix->Left;
    IdxE = Ix->Right;
    if (Base == NULL || Base->Kind != IrExIdent)
        return 0;
    if (Base->LocalSlot < 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    slot = Base->LocalSlot;
    if (slot >= IR_CALL_MAX_LOCALS)
        return 0;
    es = Fr->LocalElemSize[slot];
    if (es != (int)sizeof(int) || Fr->LocalArraySize[slot] <= 0
        || Fr->LocalVal[slot] == 0 || Fr->LocalIsFptr[slot])
        return 0;
    {
        struct ValueType *LFT = Fr->LocalFromType[slot];
        if (LFT != NULL && (LFT->Base == TypeStruct || LFT->Base == TypeUnion
                            || LFT->Base == TypeFP || LFT->Base == TypeArray))
            return 0;
    }
    if (!IrFuseIndexVal(IdxE, &Idx))
        return 0;
    if (Idx < 0 || Idx >= Fr->LocalArraySize[slot])
        return 0;
    imap = Fr->LocalInitMap[slot];
    if (imap != NULL && !imap[(size_t)Idx])
        return 0;
    P = (char *)(intptr_t)Fr->LocalVal[slot];
    *Out = (long long)(*(int *)(P + (size_t)Idx * (size_t)es));
    return 1;
}

static int IrFuseReadDerefInt(IrExpr *Star, long long *Out)
{
    IrCallFrame *Fr;
    IrExpr *P;
    void *Addr;
    if (Star == NULL || Star->Kind != IrExUnary || Star->Op != TokenAsterisk)
        return 0;
    if (Star->TypeFlags & IR_TF_FP)
        return 0;
    P = Star->Left;
    if (P == NULL || P->Kind != IrExIdent || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    Addr = NULL;
    if (P->LocalSlot >= 0 && P->LocalSlot < IR_CALL_MAX_LOCALS
        && Fr->LocalIsPtr[P->LocalSlot] && Fr->LocalInit[P->LocalSlot])
    {
        /* Only int pointees. ElemSize 0/1/2 = char/short — do not load as int
         * (*a == *b on char* would read 4 bytes past NUL). */
        if (Fr->LocalElemSize[P->LocalSlot] != (int)sizeof(int))
            return 0;
        Addr = (void *)(intptr_t)Fr->LocalVal[P->LocalSlot];
    }
    else if (P->ParamSlot >= 0 && P->ParamSlot < Fr->NumParams
             && Fr->ParamIsPtr[P->ParamSlot])
    {
        struct ValueType *PT = Fr->ParamTyp[P->ParamSlot];
        if (PT == NULL || PT->FromType == NULL)
            return 0;
        if (PT->FromType->Sizeof != (int)sizeof(int))
            return 0;
        if (PT->FromType->Base == TypeChar || PT->FromType->Base == TypeUnsignedChar
            || PT->FromType->Base == TypeShort || PT->FromType->Base == TypeUnsignedShort
#ifndef NO_FP
            || PT->FromType->Base == TypeFP
#endif
            || PT->FromType->Base == TypeStruct || PT->FromType->Base == TypeUnion)
            return 0;
        Addr = (void *)(intptr_t)Fr->ParamVal[P->ParamSlot];
    }
    else
        return 0;
    if (Addr == NULL)
        return 0;
    *Out = (long long)(*(int *)Addr);
    return 1;
}

static int IrFuseWriteIdentInt(struct ParseState *Parser, IrExpr *E, long long Val)
{
    IrCallFrame *Fr;
    if (E == NULL || E->Kind != IrExIdent || IrCallDepth <= 0)
        return 0;
    if (E->TypeFlags & (IR_TF_FP | IR_TF_CONST))
        return 0;
    if (IrAssignTargetIsConst(Parser, E))
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    if (E->LocalSlot >= 0 && E->LocalSlot < IR_CALL_MAX_LOCALS
        && !Fr->LocalIsPtr[E->LocalSlot] && !Fr->LocalIsFptr[E->LocalSlot]
        && Fr->LocalArraySize[E->LocalSlot] == 0
        && !Fr->LocalIsConst[E->LocalSlot])
    {
        Fr->LocalVal[E->LocalSlot] = IrNarrowToLocalType(Fr, E->LocalSlot, Val);
        Fr->LocalInit[E->LocalSlot] = 1;
        return 1;
    }
    if (E->ParamSlot >= 0 && E->ParamSlot < Fr->NumParams
        && !Fr->ParamIsPtr[E->ParamSlot])
    {
        Fr->ParamVal[E->ParamSlot] = Val;
        return 1;
    }
    return 0;
}

static int IrFuseWriteIndexInt(struct ParseState *Parser, IrExpr *Ix, long long Val)
{
    IrCallFrame *Fr;
    IrExpr *Base, *IdxE;
    long long Idx;
    int slot, es;
    unsigned char *imap;
    char *P;
    (void)Parser;
    if (Ix == NULL || Ix->Kind != IrExIndex || IrCallDepth <= 0)
        return 0;
    if (Ix->TypeFlags & IR_TF_FP)
        return 0;
    Base = Ix->Left;
    IdxE = Ix->Right;
    if (Base == NULL || Base->Kind != IrExIdent || Base->LocalSlot < 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    slot = Base->LocalSlot;
    if (slot >= IR_CALL_MAX_LOCALS)
        return 0;
    es = Fr->LocalElemSize[slot];
    if (es != (int)sizeof(int) || Fr->LocalArraySize[slot] <= 0
        || Fr->LocalVal[slot] == 0 || Fr->LocalIsFptr[slot])
        return 0;
    {
        struct ValueType *LFT = Fr->LocalFromType[slot];
        if (LFT != NULL && (LFT->Base == TypeStruct || LFT->Base == TypeUnion
                            || LFT->Base == TypeFP || LFT->Base == TypeArray))
            return 0;
    }
    if (!IrFuseIndexVal(IdxE, &Idx))
        return 0;
    if (Idx < 0 || Idx >= Fr->LocalArraySize[slot])
        return 0;
    P = (char *)(intptr_t)Fr->LocalVal[slot];
    *(int *)(P + (size_t)Idx * (size_t)es) = (int)Val;
    imap = Fr->LocalInitMap[slot];
    if (imap != NULL)
        imap[(size_t)Idx] = 1;
    return 1;
}

static int IrFuseWriteDerefInt(struct ParseState *Parser, IrExpr *Star, long long Val)
{
    IrCallFrame *Fr;
    IrExpr *P;
    void *Addr;
    (void)Parser;
    if (Star == NULL || Star->Kind != IrExUnary || Star->Op != TokenAsterisk)
        return 0;
    if (Star->TypeFlags & IR_TF_FP)
        return 0;
    P = Star->Left;
    if (P == NULL || P->Kind != IrExIdent || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    Addr = NULL;
    if (P->LocalSlot >= 0 && P->LocalSlot < IR_CALL_MAX_LOCALS
        && Fr->LocalIsPtr[P->LocalSlot] && Fr->LocalInit[P->LocalSlot])
    {
        /* Claude repro free_int_ptr: only fuse when pointee is clearly int-sized
         * and the stored address is non-NULL. Wrong ElemSize/uninit Addr → SEGV. */
        if (Fr->LocalElemSize[P->LocalSlot] != (int)sizeof(int))
            return 0;
        if (Fr->LocalVal[P->LocalSlot] == 0)
            return 0;
        Addr = (void *)(intptr_t)Fr->LocalVal[P->LocalSlot];
    }
    else if (P->ParamSlot >= 0 && P->ParamSlot < Fr->NumParams
             && Fr->ParamIsPtr[P->ParamSlot])
    {
        struct ValueType *PT = Fr->ParamTyp[P->ParamSlot];
        if (PT == NULL || PT->FromType == NULL
            || PT->FromType->Sizeof != (int)sizeof(int))
            return 0;
        if (PT->FromType->Base != TypeInt && PT->FromType->Base != TypeUnsignedInt)
            return 0;
        if (Fr->ParamVal[P->ParamSlot] == 0)
            return 0;
        Addr = (void *)(intptr_t)Fr->ParamVal[P->ParamSlot];
    }
    else
        return 0;
    if (Addr == NULL)
        return 0;
    *(int *)Addr = (int)Val;
    return 1;
}

int IrTryFuseEval(struct ParseState *Parser, IrExpr *E, long long *Out)
{
    long long L, R;
    if (E == NULL || IrCallDepth <= 0)
        return 0;
    if (E->TypeFlags & IR_TF_FP)
        return 0;

    /* x++ / x-- on int local/param (not pointer). ConstVal=1 => postfix. */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExIdent)
    {
        IrExpr *X = E->Left;
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int delta = (E->Op == TokenIncrement) ? 1 : -1;
        if (X->TypeFlags & (IR_TF_FP | IR_TF_PTR | IR_TF_CONST))
            return 0;
        if (IrAssignTargetIsConst(Parser, X))
            return 0;
        if (X->LocalSlot >= 0 && X->LocalSlot < IR_CALL_MAX_LOCALS
            && !Fr->LocalIsPtr[X->LocalSlot] && !Fr->LocalIsFptr[X->LocalSlot]
            && Fr->LocalArraySize[X->LocalSlot] == 0
            && Fr->LocalInit[X->LocalSlot]
            && !Fr->LocalIsConst[X->LocalSlot])
        {
            long long oldv = Fr->LocalVal[X->LocalSlot];
            long long nv = IrNarrowToLocalType(Fr, X->LocalSlot, oldv + delta);
            Fr->LocalVal[X->LocalSlot] = nv;
            *Out = E->ConstVal ? oldv : nv;
            return 1;
        }
        if (X->ParamSlot >= 0 && X->ParamSlot < Fr->NumParams
            && !Fr->ParamIsPtr[X->ParamSlot])
        {
            long long old = Fr->ParamVal[X->ParamSlot];
            Fr->ParamVal[X->ParamSlot] = old + delta;
            *Out = E->ConstVal ? old : (old + delta);
            return 1;
        }
        return 0;
    }

    /* a[j+1] = a[j];  key = a[i];  j = i - 1;  *p = n */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Right != NULL)
    {
        long long Rv;
        if (E->Right->Kind == IrExIndex)
        {
            if (!IrFuseReadIndexInt(Parser, E->Right, &Rv))
                return 0;
        }
        else if (E->Right->Kind == IrExUnary && E->Right->Op == TokenAsterisk)
        {
            if (!IrFuseReadDerefInt(E->Right, &Rv))
                return 0;
        }
        else if (E->Right->Kind == IrExBinOp
                 && (E->Right->Op == TokenPlus || E->Right->Op == TokenMinus)
                 && !(E->Right->TypeFlags & (IR_TF_FP | IR_TF_PTR)))
        {
            if (!IrFuseIndexVal(E->Right, &Rv))
                return 0;
        }
        else if (E->Right->Kind == IrExConst)
            Rv = E->Right->ConstVal;
        else if (E->Right->Kind == IrExIdent)
        {
            if (!IrFuseReadIdentInt(E->Right, &Rv))
                return 0;
        }
        else
            return 0;

        if (E->Left->Kind == IrExIndex)
        {
            if (!IrFuseWriteIndexInt(Parser, E->Left, Rv))
                return 0;
            *Out = Rv;
            return 1;
        }
        if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk)
        {
            if (!IrFuseWriteDerefInt(Parser, E->Left, Rv))
                return 0;
            *Out = Rv;
            return 1;
        }
        if (E->Left->Kind == IrExIdent)
        {
            if (!IrFuseWriteIdentInt(Parser, E->Left, Rv))
                return 0;
            *Out = Rv;
            return 1;
        }
        return 0;
    }

    if (E->Kind != IrExBinOp)
        return 0;

    /* j >= 0 && a[j] > key  — short-circuit without two full evals */
    if (E->Op == TokenLogicalAnd && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExBinOp && E->Right->Kind == IrExBinOp)
    {
        long long A, B;
        if (IrTryFuseEval(Parser, E->Left, &A))
        {
            if (!A) { *Out = 0; return 1; }
            if (IrTryFuseEval(Parser, E->Right, &B))
            {
                *Out = B ? 1 : 0;
                return 1;
            }
        }
        return 0;
    }

    /* LHS: ident | a[j] | *p ; RHS: ident | const */
    if (E->Left == NULL || E->Right == NULL)
        return 0;
    if (E->Left->Kind == IrExIndex)
    {
        if (!IrFuseReadIndexInt(Parser, E->Left, &L))
            return 0;
    }
    else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk)
    {
        if (!IrFuseReadDerefInt(E->Left, &L))
            return 0;
    }
    else if (E->Left->Kind == IrExIdent)
    {
        if (!IrFuseReadIdentInt(E->Left, &L))
            return 0;
    }
    else
        return 0;

    if (E->Right->Kind == IrExConst)
        R = E->Right->ConstVal;
    else if (E->Right->Kind == IrExIdent)
    {
        if (!IrFuseReadIdentInt(E->Right, &R))
            return 0;
    }
    else if (E->Right->Kind == IrExIndex)
    {
        if (!IrFuseReadIndexInt(Parser, E->Right, &R))
            return 0;
    }
    else if (E->Right->Kind == IrExUnary && E->Right->Op == TokenAsterisk)
    {
        if (!IrFuseReadDerefInt(E->Right, &R))
            return 0;
    }
    else
        return 0;

    /* The fused integer relop must preserve the same canonical usual
     * arithmetic conversions as the general evaluator. In particular,
     * int-versus-unsigned-int compares unsigned after promotion. */
    if ((E->CanonicalOpFlags & IR_CF_UNSIGNED) != 0)
    {
        unsigned long long UL = (unsigned long long)L;
        unsigned long long UR = (unsigned long long)R;
        switch (E->Op)
        {
            case TokenLessThan: *Out = UL < UR; return 1;
            case TokenGreaterThan: *Out = UL > UR; return 1;
            case TokenLessEqual: *Out = UL <= UR; return 1;
            case TokenGreaterEqual: *Out = UL >= UR; return 1;
            case TokenEqual: *Out = UL == UR; return 1;
            case TokenNotEqual: *Out = UL != UR; return 1;
            default: break;
        }
    }
    return IrRelopLL(E->Op, L, R, Out);
}

