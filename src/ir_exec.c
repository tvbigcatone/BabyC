/* ir_exec.c - Body IR executor (parser/executor separation)
 *
 * Owns: call stack, binop/unary, IrEvalExpr, IrExecuteStmt, IrCallUserFunc*,
 *       IrExecuteFunctionBody.
 * parse.c owns: IrCompile*, Parse*.
 *
 * Include order (B.3): ir_runtime.h first; ir_bridge.h for residual
 * Variable/Heap/Expression (globals, Intrinsic pack, dual-write).
 */
#include "picoc.h"
#include "cstdlib/printf_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ir_runtime.h"
#include "ir_bridge.h"
#include "ir_private.h"
/* Point3c frame pool/stack → ir_frame.c (D1) */
/* Point4: CachedVal hit/miss counters (BABYC_GLOBAL_BIND_STATS=1). */
long IrGlobalBindHits = 0;
long IrGlobalBindMiss = 0;
/* Structural FP counters (BABYC_FP_STATS=1 via IrPrintFpStats). */
long IrFpPtrScanHit = 0;
long IrFpPtrScanMiss = 0;
long IrFpPtrScanPredHit = 0;
long IrFpKernelWhileHit = 0;
long IrFpForHit = 0;
void IrPrintGlobalBindStats(void)
{
    const char *e = getenv("BABYC_GLOBAL_BIND_STATS");
    if (e == NULL || e[0] == '\0' || e[0] == '0')
        return;
    fprintf(stderr, "[babyc-bind] hits=%ld miss=%ld\n", IrGlobalBindHits, IrGlobalBindMiss);
}

void IrPrintFpStats(void)
{
    const char *e = getenv("BABYC_FP_STATS");
    if (e == NULL || e[0] == '\0' || e[0] == '0')
        return;
    fprintf(stderr,
        "[fp] ptrscan_hit=%ld ptrscan_pred_hit=%ld ptrscan_miss=%ld "
        "kernel_while=%ld for_hit=%ld\n",
        IrFpPtrScanHit, IrFpPtrScanPredHit, IrFpPtrScanMiss,
        IrFpKernelWhileHit, IrFpForHit);
}

/* FramePush/Pop → ir_frame.c (D1) */
long long g_fp_thin = 0;

/* Safe checks (uninit + OOB): profile + env.
 *
 * Compile-time profiles (Makefile):
 *   -DBABYC_SAFE_FORCE_OFF  → micro / footprint: always off (no env override)
 *   -DBABYC_SAFE_FORCE_ON   → desktop debug: always on (no env override)
 * Default product build: ON, overridable:
 *   BABYC_SAFE=0 or BABYC_NO_UNINIT=1 → off
 *   BABYC_SAFE=1 → on
 */
int IrSafeChecksOn(void)
{
#if defined(BABYC_SAFE_FORCE_OFF)
    return 0;
#elif defined(BABYC_SAFE_FORCE_ON)
    return 1;
#else
    static int On = -1;
    if (On < 0)
    {
        const char *s = getenv("BABYC_SAFE");
        if (s != NULL && s[0] != '\0')
            On = (s[0] != '0') ? 1 : 0;
        else if (getenv("BABYC_NO_UNINIT") != NULL)
            On = 0; /* legacy alias */
        else
            On = 1; /* product default */
    }
    return On;
#endif
}

int IrUninitChecksOn(void)
{
    return IrSafeChecksOn();
}

/* IrStackChargeBytes / IrGotoTarget → ir_frame.c (D1) */

/* B.3: Variable table lookup without VarGet (LocalTable then GlobalTable). */
/* VarCache/VarTable/Shadow → ir_shadow.c */





/* F1: global Ident → Value* via CachedVal (Point4 bind). */

/* F3: type of Ident expr without VariableGet for slotted locals/params. */
struct ValueType *IrExprType(struct ParseState *Parser, IrExpr *E)
{
    IrCallFrame *Fr;
    int ls, ps;
    struct Value *V;
    (void)ps;
    if (E == NULL)
        return NULL;
    if (E->Kind == IrExIdent && E->LocalSlot >= 0 && IrCallDepth > 0
        && E->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        Fr = IrCallStack[IrCallDepth - 1];
        ls = E->LocalSlot;
        if (Fr->LocalTyp[ls] != NULL)
            return Fr->LocalTyp[ls];
        return Fr->LocalFromType[ls];
    }
    if (E->Kind == IrExIdent && E->ParamSlot >= 0 && IrCallDepth > 0
        && E->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
    {
        Fr = IrCallStack[IrCallDepth - 1];
        return Fr->ParamTyp[E->ParamSlot];
    }
    if (E->Kind == IrExIdent && E->Ident != NULL
        && E->LocalSlot < 0 && E->ParamSlot < 0)
    {
        V = IrGlobalVal(Parser, E);
        return (V != NULL) ? V->Typ : NULL;
    }
    return NULL;
}


int IrInLoopBody = 0;

/* Static local table → ir_frame.c (D1) */
/* B.3: Intrinsic/token-call Value pack without VariableAllocValueFromType */



static int IrLocalIsConstSlot(int Slot)
{
    if (IrCallDepth <= 0 || Slot < 0 || Slot >= IR_CALL_MAX_LOCALS)
        return 0;
    return IrCallStack[IrCallDepth - 1]->LocalIsConst[Slot] != 0;
}

/* Walk assign LHS to const local root (ident or member chain). */
int IrAssignTargetIsConst(struct ParseState *Parser, IrExpr *Left)
{
    IrExpr *X = Left;
    /* Parser used by IrGlobalValSoft */
    if (X != NULL && (X->TypeFlags & IR_TF_CONST))
        return 1;
    /* *p = ... through const pointer is OK (pointee mutable unless const T*).
     * Only reject when the const object itself is the destination. */
    if (X != NULL && X->Kind == IrExUnary && X->Op == TokenAsterisk)
        return 0;
    while (X != NULL)
    {
        if (X->Kind == IrExIdent)
        {
            if (X->LocalSlot >= 0)
                return IrLocalIsConstSlot(X->LocalSlot) ? 1 : 0;
            if (X->ParamSlot >= 0)
                return 0; /* params not const-qualified in frame today */
            if (X->Ident != NULL)
            {
                struct Value *V = IrGlobalValSoft(Parser, X);
                if (V != NULL && V->IsConst)
                    return 1;
            }
            return 0;
        }
        if (X->Kind == IrExMember || X->Kind == IrExIndex)
            X = X->Left;
        else if (X->Kind == IrExUnary)
            X = X->Left;
        else
            break;
    }
    return 0;
}

/* Shared read-only store diagnostic (tree assign, linear StoreLocal /
 * member store, light StoreLocal). Caret: the first '=' on the statement
 * line (token-path convention, 138/280); otherwise CharPos when known,
 * else the caret already in Parser. Never returns. */
void IrFailReadOnlyAssign(struct ParseState *Parser, int Line, int CharPos)
{
    int found = 0;
    if (Line > 0)
        Parser->Line = Line;
    if (Parser->SourceText != NULL && Parser->Line > 0)
    {
        const char *s = Parser->SourceText;
        const char *line = s;
        int ln = 1;
        while (*s && ln < Parser->Line)
        {
            if (*s == '\n') { ln++; line = s + 1; }
            s++;
        }
        if (ln == Parser->Line)
        {
            const char *eq = line;
            while (*eq && *eq != '\n' && *eq != '=') eq++;
            if (*eq == '=')
            {
                Parser->CharacterPos = (int)(eq - line) + 1;
                found = 1;
            }
        }
    }
    if (!found && CharPos > 0)
        Parser->CharacterPos = CharPos;
    ProgramFail(Parser, "assignment of read-only variable");
}

/* Narrow integer value to local's declared type (int8_t/uint16_t/...). */



/* Point Parser caret at the array declarator with largest [N] (N>=minN).
 * Handles LF and CRLF. Used by stack-overflow diagnostics (368/575/578). */

/* Point Parser at array declarator [N]. PreferN (DeclArraySize) wins; else largest N>=MinN.
 * CRLF/LF safe. */
void IrStackOverflowSetCaretEx(struct ParseState *Parser, int MinN, int PreferN)
{
    const char *Src, *LinePos, *CPos, *BestLine = NULL, *BestBr = NULL;
    const char *PrefLine = NULL, *PrefBr = NULL;
    int BestN = 0, BestLineNo = 0, PrefLineNo = 0, curLine = 1;
    if (Parser == NULL || Parser->SourceText == NULL)
        return;
    Src = Parser->SourceText;
    LinePos = Src;
    for (CPos = Src; ; CPos++)
    {
        int at_eol = (*CPos == '\0' || *CPos == '\n'
                      || (*CPos == '\r' && CPos[1] != '\n'));
        if (at_eol)
        {
            const char *L = LinePos;
            const char *End = CPos;
            if (End > LinePos && End[-1] == '\r')
                End--;
            while (L < End)
            {
                if (*L == '[')
                {
                    const char *D = L + 1;
                    int n = 0;
                    while (D < End && *D >= '0' && *D <= '9')
                    { n = n * 10 + (*D - '0'); D++; }
                    if (D < End && *D == ']' && n > 0)
                    {
                        if (n > BestN)
                        {
                            BestN = n;
                            BestLine = LinePos;
                            BestBr = D;
                            BestLineNo = curLine;
                        }
                        if (PreferN > 0 && n == PreferN)
                        {
                            PrefLine = LinePos;
                            PrefBr = D;
                            PrefLineNo = curLine;
                        }
                    }
                }
                L++;
            }
            if (*CPos == '\0')
                break;
            if (*CPos == '\r' && CPos[1] == '\n')
                CPos++;
            curLine++;
            LinePos = CPos + 1;
        }
    }
    if (PrefLine != NULL && PrefBr != NULL)
    {
        Parser->Line = PrefLineNo;
        Parser->CharacterPos = (int)(PrefBr - PrefLine) + 1;
        return;
    }
    if (BestLine != NULL && BestBr != NULL && BestN >= MinN)
    {
        Parser->Line = BestLineNo;
        Parser->CharacterPos = (int)(BestBr - BestLine) + 1;
    }
}

static void IrStackOverflowSetCaret(struct ParseState *Parser, int MinN)
{
    IrStackOverflowSetCaretEx(Parser, MinN, -1);
}

/* Host (real process) stack growth guard for Body-IR recursion.
 * Virtual IrStackChargeBytes tracks the emulated PICOC_STACK_SIZE arena;
 * this tracks real C-stack frames, which are independent and can exhaust
 * first (esp. Windows ~1MB default). Budget default 512KB, override via
 * BABYC_STACK_BUDGET (bytes). */
char *IrHostStackBase = NULL;
size_t IrHostStackBudget = 512 * 1024;
int IrHostStackBudgetInited = 0;

/* bitfield extract/insert (LSB-first, same model as expression.c token path) */


/* Element size for IR pointer/array ident (local or param). 0 if not a ptr. */
/* Element size of a pointer/array expression (ident, index base, *p, p+n). */

int IrResolvePtr(struct ParseState *Parser, IrExpr *Base,
                        void **AddrOut, int *ElemBaseOut, int *ElemSizeOut);
int IrResolveMember(struct ParseState *Parser, IrExpr *E,
                           void **AddrOut, int *ElemBaseOut);
int IrMemberPtrElemSize(struct ParseState *Parser, IrExpr *E);
/* Unified address-of: Ident(array/ptr), Index, Member, *p, p+n / p-n. */
int IrAddrOf(struct ParseState *Parser, IrExpr *E,
                    void **AddrOut, int *ElemBaseOut, int *ElemSizeOut);


/* Arena + FreeOwned → ir_frame.c (D1) */
/* Memo → ir_fast.c (D1-5) */

/* Per-function meta cache: avoid re-walking Body IR on every call. */
/* IrFuncMeta / FCache → ir_call.c (D1 step 4) */

int IrKernelExprOk(IrExpr *E, int depth);
int IrKernelStmtOk(IrStmt *S, int depth);
int IrKernelExecStmt(struct ParseState *Parser, IrStmt *S, IrCallFrame *F, int *flow);
long long IrKernelEval(struct ParseState *Parser, IrExpr *E, IrCallFrame *F);

static int IrStmtTouchesGlobal(struct ParseState *Parser, IrStmt *S,
                               struct Value *Self, const char **LocalNames, int NLocals);

int IrBodyTouchesGlobal(struct ParseState *Parser, IrStmt *S,
                               struct Value *Self, const char **LocalNames, int NLocals)
{
    int i;
    if (S == NULL) return 0;
    if (S->Kind == IrStBlock)
    {
        for (i = 0; i < S->StmtCount; i++)
            if (IrStmtTouchesGlobal(Parser, S->Stmts[i], Self, LocalNames, NLocals))
                return 1;
        return 0;
    }
    return IrStmtTouchesGlobal(Parser, S, Self, LocalNames, NLocals);
}

static int IrStmtTouchesGlobal(struct ParseState *Parser, IrStmt *S,
                               struct Value *Self, const char **LocalNames, int NLocals)
{
    if (S == NULL) return 0;
    switch (S->Kind)
    {
        case IrStExpr:
        case IrStReturn:
            return IrExprTouchesGlobal(Parser, S->Expr, Self, LocalNames, NLocals);
        case IrStDecl:
            return IrExprTouchesGlobal(Parser, S->Expr, Self, LocalNames, NLocals);
        case IrStIf:
            return IrExprTouchesGlobal(Parser, S->Expr, Self, LocalNames, NLocals)
                || IrBodyTouchesGlobal(Parser, S->ThenStmt, Self, LocalNames, NLocals)
                || IrBodyTouchesGlobal(Parser, S->ElseStmt, Self, LocalNames, NLocals);
        case IrStWhile:
        case IrStDo:
            return IrExprTouchesGlobal(Parser, S->Expr, Self, LocalNames, NLocals)
                || IrBodyTouchesGlobal(Parser, S->BodyStmt, Self, LocalNames, NLocals);
        case IrStFor:
            return IrStmtTouchesGlobal(Parser, S->InitStmt, Self, LocalNames, NLocals)
                || IrExprTouchesGlobal(Parser, S->Expr, Self, LocalNames, NLocals)
                || IrStmtTouchesGlobal(Parser, S->IncStmt, Self, LocalNames, NLocals)
                || IrBodyTouchesGlobal(Parser, S->BodyStmt, Self, LocalNames, NLocals);
        case IrStBlock:
            return IrBodyTouchesGlobal(Parser, S, Self, LocalNames, NLocals);
        case IrStSwitch:
            return IrExprTouchesGlobal(Parser, S->Expr, Self, LocalNames, NLocals)
                || IrBodyTouchesGlobal(Parser, S->BodyStmt, Self, LocalNames, NLocals);
        default:
            return 0;
    }
}

/* Collect local names from Body IR decls (shallow + block). */
int IrCollectLocals(IrStmt *S, const char **Names, int Cap, int N)
{
    int i;
    if (S == NULL || N >= Cap) return N;
    if (S->Kind == IrStDecl && S->DeclIdent != NULL && N < Cap)
        Names[N++] = S->DeclIdent;
    if (S->Kind == IrStBlock)
    {
        for (i = 0; i < S->StmtCount && N < Cap; i++)
            N = IrCollectLocals(S->Stmts[i], Names, Cap, N);
    }
    else
    {
        if (S->ThenStmt) N = IrCollectLocals(S->ThenStmt, Names, Cap, N);
        if (S->ElseStmt) N = IrCollectLocals(S->ElseStmt, Names, Cap, N);
        if (S->BodyStmt) N = IrCollectLocals(S->BodyStmt, Names, Cap, N);
        if (S->InitStmt) N = IrCollectLocals(S->InitStmt, Names, Cap, N);
        if (S->IncStmt) N = IrCollectLocals(S->IncStmt, Names, Cap, N);
    }
    return N;
}





/* Shared pure operators (no VarGet / no BindSlot) */




PTR_TYPE_INT_SIGNED FastForEvalCond(struct ParseState *Parser, FastForCond *C);
long long IrCallUserFunc(struct ParseState *Parser, const char *Name,
                                long long *ArgVals, struct ValueType **ArgTypes, int ArgCount);
long long IrCallUserFuncCached(struct ParseState *Parser, struct Value *FuncValue,
                                      long long *ArgVals, struct ValueType **ArgTypes, int ArgCount);

/* ---- Function body / mono / eval / execute (moved from parse.c) ---- */

/* Integers to ParamVal; pointer/array to ParamVal as (intptr_t) address.
 * Also keeps VariableDefine bindings for any token-path / VarGet fallback. */

/* Unified executor entry (parser/executor separation): preferred name. */

/* Back-compat wrapper (no ParamArray) */


/* Returns 0 / 1 / 2 / 3 as above */


/* Iterative rewrite of fib-like (standard a=1,b=2 or general small steps).
 * For classic Fibonacci: O(n) two registers, no recursion. */

/* Iterative rewrite of fact-like: O(n) multiply loop */

/* (4) sum-linear: F(n)=F(n-1)+C or F(n-1)+n */

/* (5) const-mult: F(n)=C*F(n-1) */

/* (6) twin: F(n)=2*F(n-1)[+n|C] */

/* (7) tail: F(n)=F(n-1) → base */

static long long IrMonoEvalExpr(IrExpr *E, long long *P, int NP, struct Value *Self);




/* Body is only "return expr;" (optionally wrapped in a 1-stmt block).
 * Used to skip IrExecuteStmt overhead on leaf int/ptr helpers. */


int IrLookupParam(const char *Name, long long *Out)
{
    int d, i;
    for (d = IrCallDepth - 1; d >= 0; d--)
    {
        for (i = 0; i < IrCallStack[d]->NumParams; i++)
            if (IrCallStack[d]->ParamName[i] == Name ||
                (IrCallStack[d]->ParamName[i] && Name &&
                 strcmp(IrCallStack[d]->ParamName[i], Name) == 0))
            {
                *Out = IrCallStack[d]->ParamVal[i];
                return 1;
            }
        /* only current frame for params — break after top frame */
        break;
    }
    return 0;
}

/* B.1.1 — resolve Body-IR Ident: frame for slots, VarGet only for globals. */
/* B.1.1: frame/global resolve without VariableGet on slotted locals.
 * Hot load path inlines the same rules (uninit ProgramFail); this API is the
 * shared entry for tools / future backends (design §3.4.3). */
int IrFrameResolveIdent(struct ParseState *Parser, IrExpr *E,
                        long long *ValOut, void **AddrOut,
                        struct ValueType **TypOut)
{
    if (E == NULL || E->Kind != IrExIdent)
        return 0;
    if (E->LocalSlot >= 0 && IrCallDepth > 0
        && E->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int ls = E->LocalSlot;
        if (!Fr->LocalInit[ls] && !Fr->LocalOwned[ls]
            && Fr->LocalArraySize[ls] <= 0)
            return 0; /* uninit scalar — caller may ProgramFail */
        if (ValOut)
            *ValOut = Fr->LocalVal[ls];
        if (AddrOut)
        {
            if (Fr->LocalOwned[ls] || Fr->LocalIsPtr[ls]
                || Fr->LocalArraySize[ls] > 0)
                *AddrOut = (void *)(intptr_t)Fr->LocalVal[ls];
            else
                *AddrOut = (void *)&Fr->LocalVal[ls];
        }
        if (TypOut)
        {
            if (Fr->LocalTyp[ls] != NULL)
                *TypOut = Fr->LocalTyp[ls];
            else
                *TypOut = Fr->LocalFromType[ls];
        }
        return 1;
    }
    if (E->ParamSlot >= 0 && IrCallDepth > 0
        && E->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int ps = E->ParamSlot;
        if (ValOut)
            *ValOut = Fr->ParamVal[ps];
        if (AddrOut)
            *AddrOut = (void *)(intptr_t)Fr->ParamVal[ps];
        if (TypOut)
            *TypOut = Fr->ParamTyp[ps];
        return 1;
    }
    /* global only */
    if (E->Ident != NULL && (IrGlobalValSoft(Parser, E) != NULL)
        )
    {
        struct Value *V = NULL;
        V = IrGlobalVal(Parser, E); /* global only via CachedVal */
        if (V == NULL)
            return 0;
        if (ValOut)
        {
            if (V->Typ != NULL && V->Typ->Base == TypePointer)
                *ValOut = (long long)(intptr_t)V->Val->Pointer;
            else if (V->Typ != NULL && V->Typ->Base == TypeArray)
                *ValOut = (long long)(intptr_t)(void *)V->Val;
            else if (V->Typ != NULL && V->Typ->Base == TypeFunction)
                *ValOut = (long long)(intptr_t)V;
            else
                *ValOut = IrValueAsInt64(V);
        }
        if (AddrOut)
        {
            if (V->Typ != NULL && V->Typ->Base == TypePointer)
                *AddrOut = V->Val->Pointer;
            else
                *AddrOut = (void *)V->Val;
        }
        if (TypOut)
            *TypOut = V->Typ;
        return 1;
    }
    return 0;
}



/* Opaque library call: any pointer/array arg may be written — mark escaped. */
/* B.1.3: for struct/union locals LocalInitMap is a byte map of length
 * (element_count * sizeof element). LocalArraySize/LocalElemSize stay as
 * element count / stride for indexing — never overloaded as byte map dims. */



/* B.1c: dual-write to Variable shadow is optional.
 * Default ON (token interop / mixed IR+token). Set BABYC_IR_SHADOW=0 to
 * disable all dual-write Gets on pure Body-IR paths (frame is sole truth).
 */
int IrTokenFastAllowed(struct ParseState *Parser);

/* Resolve Variable shadow for dual-write / token interop. NULL if gated off
 * or name not in Variable table. */

/* Dual-write uninit bitmap to Variable shadow when present (token interop). */
/* Dual-write uninit: byte map if present; optional whole-object if no map. */


/* Dual-write when Value* already resolved (token interop only). */


/* B.3: Body-IR frames do not bind names in Variable table — skip scope IDs. */
int IrScopeBegin(struct ParseState *Parser, int *Prev)
{
    if (IrCallDepth > 0)
    {
        if (Prev) *Prev = 0;
        return 0;
    }
    return VariableScopeBegin(Parser, Prev);
}

void IrScopeEnd(struct ParseState *Parser, int ScopeID, int Prev)
{
    if (IrCallDepth > 0)
        return;
    VariableScopeEnd(Parser, ScopeID, Prev);
}


/* Nested Body-IR return sets Mode=Return; pure-IR callers restore SavedMode.
 * If Mode still leaks, clear it when THIS frame has not returned so outer
 * blocks (main after action_extract) keep running. */
int IrModeAllowsContinue(struct ParseState *Parser)
{
    if (Parser->Mode == RunModeRun || Parser->Mode == RunModeGoto)
        return 1;
    if (Parser->Mode == RunModeReturn && IrCallDepth > 0
        && !IrCallStack[IrCallDepth - 1]->HasReturn)
    {
        Parser->Mode = RunModeRun;
        return 1;
    }
    return 0;
}

int IrFrameHasReturn(void)
{
    return (IrCallDepth > 0 && IrCallStack[IrCallDepth - 1]->HasReturn);
}





/* 1 if [Addr, Addr+Size) is fully marked initialized on the frame map */
int IrFrameLeafInit(IrCallFrame *Fr, int slot, void *Addr, int Size)
{
    char *base;
    int off, i, n;
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS || Addr == NULL
        || Fr->LocalInitMap[slot] == NULL || Fr->LocalVal[slot] == 0)
        return 0;
    if (Size < 1) Size = 1;
    base = (char *)(intptr_t)Fr->LocalVal[slot];
    off = (int)((char *)Addr - base);
    if (IrFrameIsByteMap(Fr, slot))
    {
        n = IrFrameInitMapLen(Fr, slot);
        for (i = 0; i < Size; i++)
        {
            int ei = off + i;
            if (ei < 0 || ei >= n || !Fr->LocalInitMap[slot][ei])
                return 0;
        }
        return 1;
    }
    else
    {
        int es = Fr->LocalElemSize[slot] > 0 ? Fr->LocalElemSize[slot] : 1;
        int ei = es > 0 ? off / es : 0;
        n = Fr->LocalArraySize[slot];
        return (ei >= 0 && ei < n && Fr->LocalInitMap[slot][ei]) ? 1 : 0;
    }
}

static int IrEscapePathIsPure(const IrExpr *E, int Depth)
{
    int i;
    if (E == NULL) return 1;
    if (Depth > 32 || E->Kind == IrExCall || E->Kind == IrExAssign
        || E->Kind == IrExVaArg) return 0;
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)) return 0;
    if (!IrEscapePathIsPure(E->Left, Depth + 1)
        || !IrEscapePathIsPure(E->Right, Depth + 1)) return 0;
    for (i = 0; E->Args != NULL && i < E->ArgCount; ++i)
        if (!IrEscapePathIsPure(E->Args[i], Depth + 1)) return 0;
    return 1;
}

/* Mark only when the *argument* is a pointer/array (not a scalar load like b[0]). */
static void IrMarkArgEscapedToLib(struct ParseState *Parser, IrExpr *A)
{
    IrCallFrame *Fr;
    int slot;
    if (A == NULL || IrCallDepth <= 0)
        return;
    Fr = IrCallStack[IrCallDepth - 1];

    if (A->Kind == IrExIdent && A->LocalSlot >= 0 && A->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        slot = A->LocalSlot;
        /* Only if this local is a pointer/array/struct blob, not a plain int */
        if (!Fr->LocalIsPtr[slot] && Fr->LocalInitMap[slot] == NULL
            && (Fr->LocalFromType[slot] == NULL
                || (Fr->LocalFromType[slot]->Base != TypeStruct
                    && Fr->LocalFromType[slot]->Base != TypeUnion)))
            return;
        if (!(Fr->LocalInit[slot] && Fr->LocalInitMap[slot] != NULL
              && IrFrameInitMapLen(Fr, slot) > 0
              && Fr->LocalInitMap[slot][0]))
        {
            Fr->LocalInit[slot] = 1;
            if (Fr->LocalInitMap[slot] != NULL)
            {
                int n = IrFrameInitMapLen(Fr, slot);
                if (n <= 0) n = Fr->LocalArraySize[slot];
                if (n <= 0) n = 256;
                if (n > 65536) n = 65536;
                memset(Fr->LocalInitMap[slot], 1, (size_t)n);
            }
        }
        /* shadow bridge: sync Value.ElemInit for token/uninit paths */
        if (A->Ident != NULL && (IrGlobalValSoft(Parser, A) != NULL))
        {
            struct Value *V = NULL;
            V = IrShadowGet(Parser, A->Ident); /* shadow bridge */
            if (V != NULL)
            {
                V->IsInitialized = TRUE;
                if (V->ElemInit != NULL && V->ElemInitCount > 0)
                    memset(V->ElemInit, 1, (size_t)V->ElemInitCount);
            }
        }
        return;
    }
    if (A->Kind == IrExIdent && A->Ident != NULL
        && A->LocalSlot < 0 && A->ParamSlot < 0
        && (IrGlobalValSoft(Parser, A) != NULL))
    {
        struct Value *V = NULL;
        V = IrGlobalVal(Parser, A); /* global only via CachedVal */
        if (V != NULL && V->Typ != NULL
            && (V->Typ->Base == TypeArray || V->Typ->Base == TypePointer
                || V->Typ->Base == TypeStruct || V->Typ->Base == TypeUnion))
        {
            V->IsInitialized = TRUE;
            IrValueMarkAll(V);
        }
        return;
    }
    /* &x — escape the referent */
    if (A->Kind == IrExUnary && A->Op == TokenAmpersand && A->Left != NULL)
    {
        IrMarkArgEscapedToLib(Parser, A->Left);
        return;
    }
    /* s.arr / s.name as call arg (decay to pointer): mark member bytes on frame */
    if (A->Kind == IrExMember && A->Left != NULL)
    {
        IrExpr *Root = A->Left;
        void *Addr = NULL;
        int Eb = TypeInt;
        int msz = 0;
        while (Root != NULL && Root->Kind != IrExIdent && Root->Left != NULL
               && (Root->Kind == IrExMember || Root->Kind == IrExIndex))
            Root = Root->Left;
        /* Escape bookkeeping must never re-evaluate a side-effecting member
         * base after the native call. Only resolve paths rooted in a local
         * frame slot, and only when the full path is observationally pure. */
        if (Root == NULL || Root->Kind != IrExIdent || Root->LocalSlot < 0
            || Root->LocalSlot >= IR_CALL_MAX_LOCALS || !IrEscapePathIsPure(A, 0))
            return;
        if (IrResolveMember(Parser, A, &Addr, &Eb) && Addr != NULL)
        {
            if (Eb == TypeArray)
            {
                /* member array: mark whole array sizeof from type if possible */
                struct ValueType *St = NULL;
                struct Value *MV = NULL;
                if (Root != NULL && Root->Kind == IrExIdent && Root->LocalSlot >= 0
                    && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                    St = Fr->LocalFromType[Root->LocalSlot];
                if (St != NULL && St->Members != NULL && A->Ident != NULL
                    && TableGet(St->Members, A->Ident, &MV, NULL, NULL, NULL)
                    && MV != NULL && MV->Typ != NULL)
                    msz = MV->Typ->Sizeof;
                if (msz <= 0) msz = 8;
            }
            else if (Eb == TypeChar || Eb == TypeUnsignedChar) msz = 1;
            else if (Eb == TypeShort || Eb == TypeUnsignedShort) msz = 2;
            else if (Eb == TypePointer) msz = (int)sizeof(void *);
            else msz = 4;
            if (Root != NULL && Root->Kind == IrExIdent && Root->LocalSlot >= 0
                && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                IrFrameMarkBytes(Fr, Root->LocalSlot, Addr, msz);
        }
        return;
    }
    /* &s.m or array-member decay used as pointer: mark parent.
     * Scalar member rvalue (printf("%d", s.n)) must NOT mark the struct. */
    if (A->Kind == IrExUnary && A->Op == TokenAmpersand
        && A->Left != NULL && A->Left->Kind == IrExMember && A->Left->Left != NULL)
        IrMarkArgEscapedToLib(Parser, A->Left->Left);
    /* Do NOT treat IrExIndex / *p loads as whole-object escape */
}

void IrMarkAllPtrArgsEscaped(struct ParseState *Parser, IrExpr *Call)
{
    int i;
    if (Call == NULL || Call->Args == NULL)
        return;
    for (i = 0; i < Call->ArgCount; i++)
    {
        IrExpr *A = Call->Args[i];
        if (A == NULL)
            continue;
        if (A->Kind == IrExIdent
            || A->Kind == IrExMember
            || (A->Kind == IrExUnary && A->Op == TokenAmpersand))
            IrMarkArgEscapedToLib(Parser, A);
        /* scalar b[i] / *p as values: not whole-object escape */
    }
}


int IrResolveMember(struct ParseState *Parser, IrExpr *E, void **AddrOut, int *ElemBaseOut);

/* True if expression is unsigned integer (for logical >>). */


/* True if shift operand should use 64-bit width (ull/ll/ul/long). */


#ifndef NO_FP
/* True if expression yields IEEE double bits in long long. */
static int IrExprIsFP_uncached(struct ParseState *Parser, IrExpr *E);

int IrExprIsFP(struct ParseState *Parser, IrExpr *E)
{
    int r;
    if (E == NULL) return 0;
    if (E->TypeFlags & IR_TF_RESOLVED)
        return (E->TypeFlags & IR_TF_FP) ? 1 : 0;
    r = IrExprIsFP_uncached(Parser, E);
    /* Soft annotate: subsequent probes O(1). Safe — deterministic. */
    E->TypeFlags |= IR_TF_RESOLVED;
    if (r)
        E->TypeFlags |= IR_TF_FP;
    else
        E->TypeFlags |= IR_TF_INT;
    return r;
}

static int IrExprIsFP_uncached(struct ParseState *Parser, IrExpr *E)
{

    if (E == NULL) return 0;
    if (E->TypeFlags & IR_TF_RESOLVED)
        return (E->TypeFlags & IR_TF_FP) ? 1 : 0;
    if (E->Kind == IrExConst && E->Op == TokenFPConstant)
        return 1;
    if (E->Kind == IrExIdent && E->LocalSlot >= 0 && IrCallDepth > 0
        && E->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        struct ValueType *LT = Fr->LocalTyp[E->LocalSlot];
        struct ValueType *LFT = Fr->LocalFromType[E->LocalSlot];
        if (LT != NULL && LT->Base == TypeFP)
            return 1;
        if (LFT != NULL && LFT->Base == TypeFP)
            return 1;
        return 0; /* slotted: never VarGet for FP probe */
    }
    if (E->Kind == IrExIdent && E->ParamSlot >= 0 && IrCallDepth > 0
        && E->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
    {
        struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[E->ParamSlot];
        if (PT != NULL && PT->Base == TypeFP)
            return 1;
        return 0;
    }
    if (E->Kind == IrExIdent && E->Ident != NULL
        && E->LocalSlot < 0 && E->ParamSlot < 0
        && (IrGlobalValSoft(Parser, E) != NULL))
    {
        struct Value *GV = NULL;
        GV = IrGlobalVal(Parser, E); /* global only via CachedVal */
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeFP)
            return 1;
    }
    if (E->Kind == IrExIndex && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && IrCallDepth > 0
        && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        struct ValueType *LFT = IrCallStack[IrCallDepth - 1]->LocalFromType[E->Left->LocalSlot];
        if (LFT != NULL && LFT->Base == TypeFP)
            return 1;
    }
    if (E->Kind == IrExMember)
    {
        /* Type classification must be observational: resolving a member can
         * evaluate its base (g()->field, va_arg(...)->field). The evaluator
         * will resolve/load the lvalue exactly once after this probe. */
        return E->I32 == TypeFP || (E->TypeFlags & IR_TF_FP) != 0;
    }
    if (E->Kind == IrExBinOp)
        return IrExprIsFP(Parser, E->Left) || IrExprIsFP(Parser, E->Right);
    if (E->Kind == IrExUnary)
    {
        /* (int)fp is integer, not FP; (double)x is FP */
        if (E->Op == TokenCast)
            return (E->I32 == TypeFP) ? 1 : 0;
        return IrExprIsFP(Parser, E->Left);
    }
    if (E->Kind == IrExTernary)
    {
        /* FP if either arm is FP (usual arithmetic conversion) */
        if (IrExprIsFP(Parser, E->Right)) return 1;
        if (E->Args && E->ArgCount > 0 && IrExprIsFP(Parser, E->Args[0])) return 1;
        return 0;
    }
    /* Parser used by IrGlobalValSoft */
    return 0;

}

long long IrFPBits(double d)
{
    union { double d; long long l; } u;
    u.d = d;
    return u.l;
}
double IrBitsFP(long long l)
{
    union { double d; long long l; } u;
    u.l = l;
    return u.d;
}
#endif

int IrTryDenseForBody(struct ParseState *Parser, IrStmt *S);





/* True if expr yields a pointer value for Intrinsic arg packing (%s, memcpy, ...). */
int IrExprYieldsPointer(struct ParseState *Parser, IrExpr *A)
{
    if (A == NULL) return 0;
    switch (A->Kind)
    {
        case IrExConst:
            return A->Op == TokenStringConstant;
        case IrExIdent:
            if (A->LocalSlot >= 0 && IrCallDepth > 0
                && A->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (Fr->LocalIsPtr[A->LocalSlot] || Fr->LocalOwned[A->LocalSlot]
                    || Fr->LocalArraySize[A->LocalSlot] > 0)
                    return 1;
                return 0; /* slotted local: never VarGet */
            }
            if (A->ParamSlot >= 0 && IrCallDepth > 0
                && A->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
            {
                if (IrCallStack[IrCallDepth - 1]->ParamIsPtr[A->ParamSlot])
                    return 1;
                {
                    struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[A->ParamSlot];
                    if (PT != NULL && (PT->Base == TypePointer || PT->Base == TypeArray))
                        return 1;
                }
                return 0;
            }
            /* global / non-frame: best-effort via VarGet */
            if (A->Ident != NULL && (IrGlobalValSoft(Parser, A) != NULL))
            {
                struct Value *V = NULL;
                V = IrGlobalVal(Parser, A); /* global only via CachedVal */
                if (V != NULL && V->Typ != NULL
                    && (V->Typ->Base == TypePointer || V->Typ->Base == TypeArray))
                    return 1;
            }
            return 0;
        case IrExVaArg:
            return A->VaType != NULL && (A->VaType->Base == TypePointer
                                         || A->VaType->Base == TypeArray);
        case IrExIndex:
        case IrExMember:
            /* Static result type only; never resolve an expression base here. */
            return ((A->TypeFlags & IR_TF_PTR) && !(A->TypeFlags & IR_TF_FP))
                || (A->CanonicalFlags & IR_CF_PTR) || A->I32 == TypePointer;
        case IrExTernary:
            /* pointer if either branch is pointer-like (C usual conversion) */
            if (IrExprYieldsPointer(Parser, A->Right))
                return 1;
            if (A->Args != NULL && A->ArgCount >= 1 && A->Args[0] != NULL
                && IrExprYieldsPointer(Parser, A->Args[0]))
                return 1;
            return 0;
        case IrExUnary:
            /* &x yields pointer; *p yields pointee (usually scalar) — do NOT
             * treat dereference as pointer or printf %d gets CharPtr/XXX. */
            if (A->Op == TokenAmpersand)
                return 1;
            if (A->Op == TokenAsterisk)
                return ((A->TypeFlags & IR_TF_PTR) && !(A->TypeFlags & IR_TF_FP))
                    || (A->CanonicalFlags & IR_CF_PTR) || A->I32 == TypePointer;
            if ((A->Op == TokenIncrement || A->Op == TokenDecrement)
                && A->Left != NULL)
                return IrExprYieldsPointer(Parser, A->Left);
            /* (char *)p: cast stamped PTR at compile */
            if (A->Op == TokenCast)
                return (A->TypeFlags & IR_TF_PTR) && !(A->TypeFlags & IR_TF_FP);
            return 0;
        case IrExCall:
            /* char *f(): return type stamped PTR by IrAnnotateExpr (a stale
             * designator PTR is cleared there for non-pointer returns), so
             * c ? f() : g() packs as a pointer for %s. */
            return (A->TypeFlags & IR_TF_PTR) && !(A->TypeFlags & IR_TF_FP);
        case IrExBinOp:
            /* ptr + int, int + ptr, ptr - int are pointers; ptr - ptr is
             * ptrdiff_t (printf("%s", text + 2) was packed as int). */
            if (A->Op == TokenPlus)
            {
                int lp = IrExprYieldsPointer(Parser, A->Left);
                int rp = IrExprYieldsPointer(Parser, A->Right);
                return lp != rp;
            }
            if (A->Op == TokenMinus)
                return IrExprYieldsPointer(Parser, A->Left)
                    && !IrExprYieldsPointer(Parser, A->Right);
            return 0;
        default:
            return 0;
    }
}


/* Structural fast Intrinsic Value: one HeapAllocStack, minimal init.
 * Avoids VariableAllocValueFromType call chain on the pack hot path. */

/* B.3: ReturnValue for token/IR call frames — same ABI as VariableAllocValueFromType
 * (OnHeap=FALSE) but via IrPackAlloc. Size from TypeSize like VariableAlloc. */

/* Pack integer IR value into a stack Value for Intrinsic printf/sprintf.
 * Still allocates a picoc Value* (Intrinsic ABI); type comes from frame
 * LocalTyp/LocalFromType when the arg is a slotted local (B.3). */

/* Pack pointer bits into a stack Value for Intrinsic (printf %s, etc.).
 * Prefer typed pointer from frame LocalTyp when available. */

#ifndef NO_FP
/* Pack FP bits into stack Value for Intrinsic. */
struct Value *IrPackFPArg(struct ParseState *Parser, long long Bits)
{
    struct Value *PV = IrPackAlloc(Parser, &Parser->pc->FPType, (int)sizeof(double));
    PV->Val->FP = IrBitsFP(Bits);
        return PV;
}
#endif

/* G6 — fused dense patterns. Return 1 and write *Out if handled.
 * Conservative: only IR frame int locals/params, no FP, no pointer scale.
 * On any doubt return 0 so the generic switch (uninit, bounds, FP) runs. */
int IrRelopLL(enum LexToken Op, long long L, long long R, long long *Out)
{
    switch (Op)
    {
        case TokenEqual:         *Out = (L == R); return 1;
        case TokenNotEqual:      *Out = (L != R); return 1;
        case TokenLessThan:      *Out = (L <  R); return 1;
        case TokenGreaterThan:   *Out = (L >  R); return 1;
        case TokenLessEqual:     *Out = (L <= R); return 1;
        case TokenGreaterEqual:  *Out = (L >= R); return 1;
        default: return 0;
    }
}










/* Loop body: if block, run statements without per-iteration ScopeBegin
 * (parent loop holds one scope; IrStDecl recycles slots). */

/* Unpin DeclSlot values so ScopeEnd can mangle them after a loop ends. */
void IrClearStickyTree(IrStmt *S)
{
    /* Reserved: unpin DeclSlots when loop ends (no Sticky field currently). */
    (void)S;
}

void IrExecuteBodyFlat(struct ParseState *Parser, IrStmt *S)
{
    int i;
    if (S == NULL)
        return;
    IrInLoopBody++;
    if (S->Kind == IrStBlock)
    {
        /* Loop body block: no ScopeBegin — decls Sticky across iters */
        for (i = 0; i < S->StmtCount && Parser->Mode == RunModeRun; i++)
            IrExecuteStmt(Parser, S->Stmts[i]);
    }
    else
        IrExecuteStmt(Parser, S);
    IrInLoopBody--;
}


/* Native C scan: while (*p) { s += *p; p++; }  (char* local, int local accum) */



/* True if any subexpression writes LocalSlot (assign or ++/--). */
static int IrExprWritesLocalSlot(IrExpr *E, int Slot)
{
    int a;
    if (E == NULL || Slot < 0)
        return 0;
    if (E->Kind == IrExAssign && E->Left != NULL
        && E->Left->Kind == IrExIdent && E->Left->LocalSlot == Slot)
        return 1;
    /* i++ / ++i / i-- / --i write the local (LICM must not hoist i=0). */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot == Slot)
        return 1;
    if (IrExprWritesLocalSlot(E->Left, Slot))
        return 1;
    if (IrExprWritesLocalSlot(E->Right, Slot))
        return 1;
    if (E->Args != NULL)
        for (a = 0; a < E->ArgCount; a++)
            if (IrExprWritesLocalSlot(E->Args[a], Slot))
                return 1;
    return 0;
}

/* Scan a statement tree for any write to LocalSlot. */
static int IrStmtWritesLocalSlot(IrStmt *S, int Slot)
{
    int i;
    if (S == NULL || Slot < 0)
        return 0;
    if (S->Expr != NULL && IrExprWritesLocalSlot(S->Expr, Slot))
        return 1;
    if (S->BodyStmt != NULL && IrStmtWritesLocalSlot(S->BodyStmt, Slot))
        return 1;
    if (S->ThenStmt != NULL && IrStmtWritesLocalSlot(S->ThenStmt, Slot))
        return 1;
    if (S->ElseStmt != NULL && IrStmtWritesLocalSlot(S->ElseStmt, Slot))
        return 1;
    if (S->IncStmt != NULL && IrStmtWritesLocalSlot(S->IncStmt, Slot))
        return 1;
    if (S->InitStmt != NULL && IrStmtWritesLocalSlot(S->InitStmt, Slot))
        return 1;
    if (S->Stmts != NULL)
        for (i = 0; i < S->StmtCount; i++)
            if (S->Stmts[i] != NULL && IrStmtWritesLocalSlot(S->Stmts[i], Slot))
                return 1;
    return 0;
}

/* LICM (light): hoist leading body stmts of form local = const
 * when the local is not the induction variable AND is not written again
 * later in the body (otherwise e.g. sum=0; sum+=i; would break).
 * Returns count of stmts to skip at the start of each body iteration. */
int IrLicmLeadingConstAssigns(struct ParseState *Parser, IrStmt *ForS, IrStmt **BodyList, int BodyN)
{
    IrCallFrame *Fr;
    int i, skip = 0;
    int indSlot = -1;
    const char *indName = NULL;

    if (Parser == NULL || ForS == NULL || BodyList == NULL || BodyN <= 0)
        return 0;
    if (IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];

    /* induction var from FastCond or Expr i < N */
    if (ForS->FastCond.Active && ForS->FastCond.VarName != NULL)
        indName = ForS->FastCond.VarName;
    if (ForS->Expr != NULL && ForS->Expr->Kind == IrExBinOp
        && ForS->Expr->Left != NULL && ForS->Expr->Left->Kind == IrExIdent)
    {
        if (ForS->Expr->Left->LocalSlot >= 0)
            indSlot = ForS->Expr->Left->LocalSlot;
        if (indName == NULL)
            indName = ForS->Expr->Left->Ident;
    }
    if (indSlot < 0 && indName != NULL)
    {
        int li;
        for (li = 0; li < IR_CALL_MAX_LOCALS; li++)
            if (Fr->LocalName[li] != NULL && strcmp(Fr->LocalName[li], indName) == 0)
            {
                indSlot = li;
                break;
            }
    }

    for (i = 0; i < BodyN; i++)
    {
        IrStmt *St = BodyList[i];
        IrExpr *E, *L, *R;
        int slot, j, laterWrite;
        if (St == NULL || St->Kind != IrStExpr || St->Expr == NULL)
            break;
        E = St->Expr;
        if (E->Kind != IrExAssign)
            break;
        L = E->Left;
        R = E->Right;
        if (L == NULL || L->Kind != IrExIdent || L->LocalSlot < 0)
            break;
        if (R == NULL || R->Kind != IrExConst)
            break;
        slot = L->LocalSlot;
        if (slot == indSlot)
            break;
        /* Refuse hoist if any later body stmt writes the same local
         * (sum=0; sum+=p; is NOT loop-invariant). */
        laterWrite = 0;
        for (j = i + 1; j < BodyN; j++)
            if (BodyList[j] != NULL && IrStmtWritesLocalSlot(BodyList[j], slot))
            {
                laterWrite = 1;
                break;
            }
        if (laterWrite)
            break;
        /* hoist: execute once now */
        IrExecuteStmt(Parser, St);
        if (Parser->Mode != RunModeRun)
            return skip; /* aborted */
        skip++;
    }
    return skip;
}

/* for (i=0; i<N; i++) { p = base; while (*p) { s += *p; p++; } }
 * Uses AST LocalSlots only; elem size 1 (char). */


/* while (p < end) { s += *p; p = p + 1; }  int or char pointer with bound */


/* True if pointer local pointee is 1-byte (char/unsigned char). */
int IrLocalPtrElemIsChar(IrCallFrame *Fr, int pslot)
{
    struct ValueType *FT;
    if (Fr == NULL || pslot < 0 || pslot >= IR_CALL_MAX_LOCALS)
        return 0;
    if (!Fr->LocalIsPtr[pslot])
        return 0;
    if (Fr->LocalElemSize[pslot] == 1)
        return 1;
    if (IrFrameGetPointeeSize(Fr, pslot) == 1)
        return 1;
    FT = Fr->LocalFromType[pslot];
    if (FT != NULL && (FT->Base == TypeChar || FT->Base == TypeUnsignedChar))
        return 1;
    FT = Fr->LocalTyp[pslot];
    if (FT != NULL && FT->FromType != NULL
        && (FT->FromType->Base == TypeChar || FT->FromType->Base == TypeUnsignedChar))
        return 1;
    return 0;
}


/* General: while (*p) { if (*p==c1 || *p==c2 || ...) acc++; p++; }
 * Structural char-scan with equality predicates (not bench-specific). */





int IrFrameSlotByName(const char *Name);
/* --- recovered / stub fast-paths (ForFpKind zero-cost match) --- */
int IrStructMemberOffset(struct ValueType *ST, const char *Name,
                                int *OffOut, int *BaseOut, int *IsPtrOut)
{
    int es = 4, eb = TypeInt;
    /* GUARD: H11 — single member layout */
    if (!IrResolveMemberLayout(ST, Name, OffOut, &es, &eb, IsPtrOut))
        return 0;
    if (BaseOut) *BaseOut = eb;
    (void)es;
    return 1;
}

/* thin stubs replaced below before IrExecuteStmt */



/* --- thin for paths (IsThinKernel / TplReady aware) --- */
int IrExprCachedGlobal(struct ParseState *Parser, IrExpr *E,
                              struct Value **Out)
{
    struct Value *V = NULL;
    if (Out != NULL)
        *Out = NULL;
    if (E == NULL || E->Ident == NULL || Parser == NULL)
        return 0;
    V = E->CachedVal;
    if (V != NULL && !V->OutOfScope)
    {
        if (Out != NULL)
            *Out = V;
        return 1;
    }
    if (E->CachedFunc != NULL && !E->CachedFunc->OutOfScope
        && E->CachedFunc->Typ != NULL
        && E->CachedFunc->Typ->Base == TypeFunction)
    {
        V = E->CachedFunc;
        E->CachedVal = V;
        if (Out != NULL)
            *Out = V;
        return 1;
    }
    if (!IrVarTableLookup(Parser->pc, E->Ident, &V) || V == NULL)
        return 0;
    E->CachedVal = V;
    if (V->Typ != NULL && V->Typ->Base == TypeFunction
        && V->Val != NULL
        && (V->Val->FuncDef.BodyIr != NULL
            || V->Val->FuncDef.Body.Pos != NULL
            || V->Val->FuncDef.Intrinsic != NULL))
        E->CachedFunc = V;
    if (Out != NULL)
        *Out = V;
    return 1;
}

int IrFrameSlotByName(const char *Name);







int IrExprIdentIsPtr(IrExpr *X, IrCallFrame *F)
{
    if (X == NULL || X->Kind != IrExIdent || F == NULL) return 0;
    if (X->LocalSlot >= 0 && X->LocalSlot < IR_CALL_MAX_LOCALS
        && (F->LocalIsPtr[X->LocalSlot] || F->LocalIsFptr[X->LocalSlot]))
        return 1;
    if (X->ParamSlot >= 0 && X->ParamSlot < IR_CALL_MAX_PARAMS
        && F->ParamIsPtr[X->ParamSlot])
        return 1;
    return 0;
}

/* Point2 forward decls — defs live after IrTry* implementations. */
int IrDispatchForFp(struct ParseState *Parser, IrStmt *S, int k);
int IrProbeForFp(struct ParseState *Parser, IrStmt *S);


/* parse a "for" statement */
/* Fast loop cond: "i < N" or "i < n" (variable bound) + inc i++ / i=i+1 */
int FastForCaptureCond(struct ParseState *From, FastForCond *Out)
{
    struct ParseState P;
    struct Value *LexVal;
    enum LexToken Op, Tok;

    Out->Active = 0;
    Out->BoundIsVar = 0;
    Out->BoundVarName = NULL;
    Out->Bound = 0;
    Out->VarName = NULL;
    Out->VarSlot = -1;
    Out->BoundSlot = -1;
    Out->CachedVar = NULL;
    Out->CachedBound = NULL;
    ParserCopy(&P, From);
    Tok = LexGetToken(&P, &LexVal, TRUE);

    /* while(1) / do ... while(0): bare integer constant condition */
    if (Tok == TokenIntegerConstant)
    {
        enum LexToken Rest;
        Out->Bound = IrValueAsInt64(LexVal);
        Out->BoundIsVar = 0;
        Rest = LexGetToken(&P, NULL, FALSE);
        if (Rest != TokenCloseBracket && Rest != TokenSemicolon && Rest != TokenEOF)
            return 0; /* while(1+0) etc. — not a bare constant */
        Out->Active = 2; /* constant condition */
        return 1;
    }

    if (Tok != TokenIdentifier)
        return 0;
    Out->VarName = LexVal->Val->Identifier;
    Op = LexGetToken(&P, NULL, FALSE);
    /* while(x) / while(flag): bare identifier as boolean — only if end of cond */
    if (Op != TokenLessThan && Op != TokenLessEqual && Op != TokenGreaterThan
        && Op != TokenGreaterEqual && Op != TokenEqual && Op != TokenNotEqual)
    {
        if (Op != TokenCloseBracket && Op != TokenSemicolon && Op != TokenEOF)
            return 0;
        Out->Active = 3; /* truthiness of VarName */
        Out->Op = TokenNone;
        return 1;
    }
    LexGetToken(&P, NULL, TRUE); /* consume relop */
    Out->Op = Op;
    Tok = LexGetToken(&P, &LexVal, TRUE);
    if (Tok == TokenIntegerConstant)
    {
        Out->Bound = IrValueAsInt64(LexVal);
        Out->BoundIsVar = 0;
    }
    else if (Tok == TokenIdentifier)
    {
        struct Value *BoundCheck;
        /* Not a call: i < lim() is not a simple variable bound */
        if (LexGetToken(&P, NULL, FALSE) == TokenOpenBracket)
            return 0;
        /* #define N 4 -> resolve to integer bound (keep FastCond active) */
        if (TableGet(&P.pc->GlobalTable, LexVal->Val->Identifier, &BoundCheck, NULL, NULL, NULL)
            && BoundCheck->Typ->Base == TypeMacro
            && BoundCheck->Val->MacroDef.NumParams == 0
            && BoundCheck->Val->MacroDef.Body.Pos != NULL)
        {
            struct ParseState MP;
            struct Value *MV;
            enum LexToken MT;
            ParserCopy(&MP, &BoundCheck->Val->MacroDef.Body);
            MT = LexGetToken(&MP, &MV, TRUE);
            if (MT != TokenIntegerConstant)
                return 0;
            Out->Bound = IrValueAsInt64(MV);
            Out->BoundIsVar = 0;
        }
        else
        {
            Out->BoundVarName = LexVal->Val->Identifier;
            Out->BoundIsVar = 1;
        }
    }
    else
        return 0;
    /* Must be end of condition — reject i < N && ..., i < n + 1, etc. */
    {
        enum LexToken Rest = LexGetToken(&P, NULL, FALSE);
        if (Rest != TokenCloseBracket && Rest != TokenSemicolon && Rest != TokenEOF)
            return 0;
    }
    Out->Active = 1;
    return 1;
}

int FastForCaptureInc(struct ParseState *From, FastForInc *Out)
{
    struct ParseState P;
    struct Value *LexVal;
    enum LexToken Tok;
    char *Name;

    Out->Active = 0;
    Out->VarSlot = -1;
    Out->CachedVar = NULL;
    ParserCopy(&P, From);
    Tok = LexGetToken(&P, &LexVal, TRUE);
    if (Tok != TokenIdentifier)
        return 0;
    Name = LexVal->Val->Identifier;
    Tok = LexGetToken(&P, NULL, TRUE);
    if (Tok == TokenIncrement)
    {
        Out->VarName = Name;
        Out->Active = 1;
        return 1;
    }
    /* i = i + 1 */
    if (Tok == TokenAssign)
    {
        Tok = LexGetToken(&P, &LexVal, TRUE);
        if (Tok != TokenIdentifier || strcmp(LexVal->Val->Identifier, Name) != 0)
            return 0;
        Tok = LexGetToken(&P, NULL, TRUE);
        if (Tok != TokenPlus)
            return 0;
        Tok = LexGetToken(&P, &LexVal, TRUE);
        if (Tok != TokenIntegerConstant || IrValueAsInt64(LexVal) != 1)
            return 0;
        Out->VarName = Name;
        Out->Active = 1;
        return 1;
    }
    return 0;
}



int IrFrameSlotByName(const char *Name)
{
    int i;
    IrCallFrame *F;
    if (Name == NULL || IrCallDepth <= 0)
        return -1;
    F = IrCallStack[IrCallDepth - 1];
    for (i = F->NumLocals - 1; i >= 0; i--)
        if (i < IR_CALL_MAX_LOCALS && F->LocalName[i] != NULL
            && strcmp(F->LocalName[i], Name) == 0)
            return i;
    return -1;
}

/* Tight for: for (i=…; i < N; i++) s += i;  — raw C on IR locals */


/* Dense for-body: single-index dual load  s = s + (base[i] + base[j])
 * or block of pure local int ops. Generalisation of IrTryFastArrayAccum. */

/* Fast: for (i=0;i<N;i++) s += a[i]  or  s += p[i]  (local int array / int*) */





/* for (i=..; i<N; i++) a[i] = C;  — tight fill, mirrors FastArrayAccum */

/* return p->a + p->b + p->c; */

/* Match score-like body:
 *   int s; s = p->a + p->b + p->c; s = p->op(s, p->a); return s;
 * Returns field names via out params (borrowed Ident pointers). */

/* for (...) t = t + score(&global_struct); with score-like body + leaf op */

/* for (i=…; i < N; i++) s = s + leaf(C0, C1);
 * leaf is return p0 OP p1 with const args — collapse to native loop. */



/* ---- Generalized integer kernel loop ----
 * Accelerates nested for/if with pure integer/bitwise ops on locals/params
 * and 1-D array loads (local ptr or global). Covers CRC-style bit loops and
 * similar numeric kernels without full IrEvalExpr overhead.
 */



/* Resolve index base to (void*) pointer + elem size; 0 on failure */


/* flow: 0 ok, 1 break, 2 continue */




/* Point2: single dispatch for ForFpKind — no dual cascade. */



PTR_TYPE_INT_SIGNED FastForEvalCond(struct ParseState *Parser, FastForCond *C)
{
    struct Value *Var;
    struct Value *BoundVar;
    PTR_TYPE_INT_SIGNED Left, Right;
    long long LV, RV;

    /* Active==2: constant condition (while(1) / while(0)) */
    if (C->Active == 2)
        return C->Bound != 0;
    /* Active==3: while(x) — truthiness of identifier */
    if (C->Active == 3)
    {
        /* B.1.1: prefer compile-time VarSlot, then name frame, then global Get */
        if (C->VarSlot >= 0 && IrCallDepth > 0
            && C->VarSlot < IR_CALL_MAX_LOCALS
            && IrCallStack[IrCallDepth - 1]->LocalInit[C->VarSlot])
            return IrCallStack[IrCallDepth - 1]->LocalVal[C->VarSlot] != 0;
        if (IrFrameGetByName(C->VarName, &LV) == 1)
            return LV != 0;
        /* Local Value pointers live in stack frames and must not be retained
         * across token re-entry/function calls. Keep the cache only for globals. */
        if (Parser->pc->TopStackFrame != NULL)
            VariableGet(Parser->pc, Parser, (char *)C->VarName, &Var);
        else
            Var = IrGlobalCacheFill(Parser, C->VarName, &C->CachedVar);
        return IrValueAsInt64(Var) != 0;
    }

    if (C->VarSlot >= 0 && IrCallDepth > 0
        && C->VarSlot < IR_CALL_MAX_LOCALS
            && IrCallStack[IrCallDepth - 1]->LocalInit[C->VarSlot])
        Left = (PTR_TYPE_INT_SIGNED)IrCallStack[IrCallDepth - 1]->LocalVal[C->VarSlot];
    else if (IrFrameGetByName(C->VarName, &LV) == 1)
        Left = (PTR_TYPE_INT_SIGNED)LV;
    else
    {
        /* Local Value pointers must be re-resolved in the current frame. */
        if (Parser->pc->TopStackFrame != NULL)
            VariableGet(Parser->pc, Parser, (char *)C->VarName, &Var);
        else
            Var = IrGlobalCacheFill(Parser, C->VarName, &C->CachedVar);
        Left = IrValueAsInt64(Var);
    }
    if (C->BoundIsVar)
    {
        if (C->BoundSlot >= 0 && IrCallDepth > 0
            && C->BoundSlot < IR_CALL_MAX_LOCALS
            && IrCallStack[IrCallDepth - 1]->LocalInit[C->BoundSlot])
            Right = (PTR_TYPE_INT_SIGNED)IrCallStack[IrCallDepth - 1]->LocalVal[C->BoundSlot];
        else if (IrFrameGetByName(C->BoundVarName, &RV) == 1)
            Right = (PTR_TYPE_INT_SIGNED)RV;
        else
        {
            /* As above, cache only globals, never stack-local Value pointers. */
            if (Parser->pc->TopStackFrame != NULL)
                VariableGet(Parser->pc, Parser, (char *)C->BoundVarName, &BoundVar);
            else
                BoundVar = IrGlobalCacheFill(Parser, C->BoundVarName, &C->CachedBound);
            Right = IrValueAsInt64(BoundVar);
        }
    }
    else
        Right = (PTR_TYPE_INT_SIGNED)C->Bound;
    switch (C->Op)
    {
        case TokenLessThan:      return Left < Right;
        case TokenLessEqual:     return Left <= Right;
        case TokenGreaterThan:   return Left > Right;
        case TokenGreaterEqual:  return Left >= Right;
        case TokenEqual:         return Left == Right;
        case TokenNotEqual:      return Left != Right;
        default:                 return 0;
    }
}

/* --- Shared loop executor primitives (step toward interpreter/executor split) --- */

/* Run one loop body: from TokenEOF-cached tokens if present, else from source Pos. */



/* ---- Loop executor (moved from parse.c) ---- */
/* Token path roles: (1) phase-M micro footprint (2) interactive debug.
 * Fast token paths (tight loops, BodyIr-in-loop) must NOT run when debugging
 * so each statement still hits DebugCheckStatement (step / breakpoints).
 * BABYC_TOKEN_FAST=0 forces the slow/debug-faithful path for bisect. */
int IrTokenFastAllowed(struct ParseState *Parser)
{
    static int EnvFast = -1;
    const char *e;
    if (Parser != NULL && Parser->DebugMode)
        return 0;
    if (EnvFast < 0)
    {
        e = getenv("BABYC_TOKEN_FAST");
        /* default ON; only explicit "0" disables */
        EnvFast = (e != NULL && e[0] == '0' && e[1] == '\0') ? 0 : 1;
    }
    return EnvFast;
}

