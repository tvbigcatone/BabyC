/* ir_stmt.c — Body-IR statement executor (D1 step 6 extract from ir_exec.c) */
#include "picoc.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ir_runtime.h"
#include "ir_private.h"
#include "ir_bridge.h"

/* Portable IR stmt dispatch (mirror ir_eval).
 * - GCC/Clang: computed goto (labels-as-values)
 * - MSVC / others: dense switch
 * Disable: -DBABYC_NO_DISPATCH_TABLE
 */
#if !defined(BABYC_NO_DISPATCH_TABLE) && (defined(__GNUC__) || defined(__clang__))
# define BABYC_IR_STMT_CGOTO 1
#else
# define BABYC_IR_STMT_CGOTO 0
#endif


/* Shared virtual + host stack budget for automatic arrays / aggregates
 * (tree DeclStmt and linear IR_K_DECL_ARRAY). Small objects (<512 bytes)
 * are not charged. Re-executing the declaration of the same slot (block
 * inside a loop) reuses the object's storage, so the previous charge of the
 * slot is released first: otherwise every iteration would accumulate and
 * a loop-local `int big[1000]` raised a false stack overflow. On overflow
 * the caret targets the declarator ([N]) and ProgramFail does not return. */
static void IrStackFailAtDecl(struct ParseState *Parser, int Line, int CharPos,
                              const char *Ident, int DeclCount)
{
    if (Line > 0)
        Parser->Line = Line;
    if (CharPos > 0)
        Parser->CharacterPos = CharPos;
    if (Ident != NULL)
        ParserSetCaretOnIdent(Parser, Ident);
    IrStackOverflowSetCaretEx(Parser, 512, (DeclCount > 0) ? DeclCount : -1);
    ProgramFail(Parser, "stack overflow (local variables / call frames exceed stack size)");
}

void IrChargeLocalArrayOrFail(struct ParseState *Parser, IrCallFrame *Fr, int Slot,
                              size_t nbytes, int Line, int CharPos,
                              const char *Ident, int DeclCount)
{
    size_t need, budget = 0;
    char *mem0, *bot;
    char host_probe;
    ptrdiff_t host_used;
    if (Fr == NULL || Slot < 0 || Slot >= IR_CALL_MAX_LOCALS)
        return;
    if (Fr->LocalStackCharge[Slot] > 0)
    {
        if (IrStackChargeBytes >= Fr->LocalStackCharge[Slot])
            IrStackChargeBytes -= Fr->LocalStackCharge[Slot];
        else
            IrStackChargeBytes = 0;
        Fr->LocalStackCharge[Slot] = 0;
    }
    if (nbytes < 512)
        return;
    need = (size_t)MEM_ALIGN((int)nbytes);
    mem0 = (char *)Parser->pc->HeapMemory;
    bot = (char *)Parser->pc->HeapBottom;
    if (mem0 != NULL && bot != NULL && bot > mem0)
        budget = (size_t)(bot - mem0);
    if (budget == 0)
        budget = (size_t)(2 * 1024 * 1024);
    /* leave headroom so ProgramFail can still run */
    if (budget > 65536)
        budget -= 65536;
    if (IrStackChargeBytes > budget || need > budget - IrStackChargeBytes)
        IrStackFailAtDecl(Parser, Line, CharPos, Ident, DeclCount);
    /* Host stack growth guard (Body-IR is true host recursion). */
    if (!IrHostStackBudgetInited)
    {
        const char *env = getenv("BABYC_STACK_BUDGET");
        if (env != NULL && env[0] != '\0')
        {
            unsigned long v = strtoul(env, NULL, 10);
            if (v > 65536)
                IrHostStackBudget = (size_t)v;
        }
        IrHostStackBudgetInited = 1;
    }
    /* Depth marker only: the base is compared, never dereferenced. */
#if defined(__GNUC__) && __GNUC__ >= 12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
    if (IrHostStackBase == NULL)
        IrHostStackBase = &host_probe;
#if defined(__GNUC__) && __GNUC__ >= 12
#pragma GCC diagnostic pop
#endif
    host_used = (ptrdiff_t)(IrHostStackBase - &host_probe);
    if (host_used < 0)
        host_used = -host_used;
    if ((size_t)host_used > IrHostStackBudget)
        IrStackFailAtDecl(Parser, Line, CharPos, Ident, DeclCount);
    IrStackChargeBytes += need;
    Fr->LocalStackCharge[Slot] = (unsigned)need;
}

void IrExecuteStmtTree(struct ParseState *Parser, IrStmt *S)
{
    int i;
    if (S == NULL)
        return;
    /* Nested Body-IR return sets Mode=Return; if THIS frame did not return,
     * clear the leak so outer stmts (main after action_extract) still run. */
    if (Parser->Mode == RunModeReturn && IrCallDepth > 0
        && !IrCallStack[IrCallDepth - 1]->HasReturn)
        Parser->Mode = RunModeRun;
    /* RunModeGoto: keep walking to find IrStLabel */
    if (Parser->Mode != RunModeRun && Parser->Mode != RunModeGoto)
        return;
    if (S->SourceLine && Parser->Mode == RunModeRun)
    {
        Parser->Line = S->SourceLine;
        if (S->SourceFile != NULL)
            Parser->FileName = (char *)S->SourceFile;
    }

#if BABYC_IR_STMT_CGOTO
    {
        static void *const ir_stmt_jt[] = {
            [0] = &&L_IrSt_default,
            [IrStExpr] = &&L_IrStExpr,
            [IrStBlock] = &&L_IrStBlock,
            [IrStIf] = &&L_IrStIf,
            [IrStReturn] = &&L_IrStReturn,
            [IrStBreak] = &&L_IrStBreak,
            [IrStContinue] = &&L_IrStContinue,
            [IrStWhile] = &&L_IrStWhile,
            [IrStDo] = &&L_IrStDo,
            [IrStFor] = &&L_IrStFor,
            [IrStDecl] = &&L_IrStDecl,
            [IrStSwitch] = &&L_IrStSwitch,
            [IrStCase] = &&L_IrStCase,
            [IrStDefault] = &&L_IrStDefault,
            [IrStGoto] = &&L_IrStGoto,
            [IrStLabel] = &&L_IrStLabel,
        };
        unsigned k = (unsigned)S->Kind;
        if (k < (unsigned)(sizeof(ir_stmt_jt)/sizeof(ir_stmt_jt[0])) && ir_stmt_jt[k] != 0)
            goto *ir_stmt_jt[k];
        goto L_IrSt_default;
    }
# define IR_STMT_CASE(name) L_##name:
# define IR_STMT_DEFAULT L_IrSt_default:
# define IR_STMT_BREAK return
#else
    switch (S->Kind)
    {
# define IR_STMT_CASE(name) case name:
# define IR_STMT_DEFAULT default:
# define IR_STMT_BREAK break
#endif
        IR_STMT_CASE(IrStExpr)
            if (Parser->Mode == RunModeGoto) IR_STMT_BREAK;
            IrEvalExpr(Parser, S->Expr);
            IR_STMT_BREAK;
        IR_STMT_CASE(IrStBlock)
        {
            /* Pure IR call frames use LocalSlot; skip VariableScope* overhead */
            if (IrCallDepth > 0)
            {
                /* Stop on THIS frame HasReturn. Nested return may leave
                 * Mode=Return; IrModeAllowsContinue clears the leak. */
                for (i = 0; i < S->StmtCount
                     && IrModeAllowsContinue(Parser)
                     && !IrFrameHasReturn(); i++)
                    IrExecuteStmt(Parser, S->Stmts[i]);
            }
            else
            {
                int PrevScopeID = 0, ScopeID = IrScopeBegin(Parser, &PrevScopeID);
                for (i = 0; i < S->StmtCount
                     && (Parser->Mode == RunModeRun || Parser->Mode == RunModeGoto); i++)
                    IrExecuteStmt(Parser, S->Stmts[i]);
                IrScopeEnd(Parser, ScopeID, PrevScopeID);
            }
            IR_STMT_BREAK;
        }
        IR_STMT_CASE(IrStIf)
            if (Parser->Mode == RunModeGoto)
            {
                if (S->ThenStmt) IrExecuteStmt(Parser, S->ThenStmt);
                if (Parser->Mode == RunModeGoto && S->ElseStmt)
                    IrExecuteStmt(Parser, S->ElseStmt);
                IR_STMT_BREAK;
            }
            if (IrEvalExpr(Parser, S->Expr))
                IrExecuteStmt(Parser, S->ThenStmt);
            else if (S->ElseStmt)
                IrExecuteStmt(Parser, S->ElseStmt);
            if (Parser->Mode == RunModeReturn && IrCallDepth > 0
                && !IrCallStack[IrCallDepth - 1]->HasReturn)
                Parser->Mode = RunModeRun;
            IR_STMT_BREAK;
        IR_STMT_CASE(IrStReturn)
        {
            long long V = 0;
            /* Do not consume a return while hunting for a goto label */
            if (Parser->Mode == RunModeGoto)
                IR_STMT_BREAK;
            if (S->Expr != NULL)
                V = IrEvalExpr(Parser, S->Expr);
            if (IrCallDepth > 0)
            {
                /* Struct/union return-by-value: V is address of source blob
                 * (local or temp). Copy to a stable static buffer before the
                 * callee frame is freed, then hand the buffer pointer back.
                 * Mirrors the token-path IrStructRetBuf handling. */
                {
                    struct ValueType *RT = NULL;
                    /* Walk up to find the function's ReturnType if available
                     * via TopStackFrame or by checking if V looks like a blob
                     * pointer from a local struct (conservative: always copy
                     * when the expression is an Ident of owned local struct). */
                    static char IrStructRetBufIR[512];
                    /* S->DeclFromType is the function's declared return
                     * type (ir_compile TokenReturn). */
                    if (S->Expr != NULL && S->DeclFromType != NULL
                        && IrTypeIsCopyableAggregate(S->DeclFromType)
                        && S->DeclFromType->Sizeof <= (int)sizeof(IrStructRetBufIR))
                    {
                        /* Aggregate return: V is the address of the returned
                         * object (local, member, element, deref, global or
                         * nested result); copy the value out of it. */
                        void *Src = (void *)(intptr_t)V;
                        if (Src == NULL)
                            ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
                        if (Src != (void *)IrStructRetBufIR)
                            memmove(IrStructRetBufIR, Src,
                                    (size_t)S->DeclFromType->Sizeof);
                        V = (long long)(intptr_t)IrStructRetBufIR;
                    }
                    else if (S->Expr != NULL && S->Expr->Kind == IrExIdent
                        && S->Expr->LocalSlot >= 0
                        && S->Expr->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                        int ls = S->Expr->LocalSlot;
                        if (Fr->LocalOwned[ls] && Fr->LocalFromType[ls] != NULL
                            && (Fr->LocalFromType[ls]->Base == TypeStruct
                                || Fr->LocalFromType[ls]->Base == TypeUnion))
                        {
                            int sz = Fr->LocalFromType[ls]->Sizeof;
                            void *Src = (void *)(intptr_t)Fr->LocalVal[ls];
                            if (Src != NULL && sz > 0 && sz <= (int)sizeof(IrStructRetBufIR))
                            {
                                memcpy(IrStructRetBufIR, Src, (size_t)sz);
                                V = (long long)(intptr_t)IrStructRetBufIR;
                            }
                        }
                    }
                }
                IrCallStack[IrCallDepth - 1]->ReturnVal = V;
                IrCallStack[IrCallDepth - 1]->HasReturn = 1;
            }
            /* Token ReturnValue belongs to the IR frame entered from expression.c
             * / startup (IrCallDepth==1). Nested Body-IR (depth>=2) must NOT
             * write it or set Mode=Return — that aborts the outer function
             * (e.g. main stops after action_extract returns). */
            if (IrCallDepth == 1
                && Parser->pc->TopStackFrame && Parser->pc->TopStackFrame->ReturnValue
                && Parser->pc->TopStackFrame->ReturnValue->Typ->Base != TypeVoid
                && S->Expr != NULL)
            {
                struct Value *RV = Parser->pc->TopStackFrame->ReturnValue;
                switch (RV->Typ->Base)
                {
                    case TypeInt: RV->Val->Integer = (int)V; break;
                    case TypeShort: RV->Val->ShortInteger = (short)V; break;
                    case TypeChar: RV->Val->Character = (char)V; break;
                    case TypeLong: RV->Val->LongInteger = (long)V; break;
                    case TypeLongLong: RV->Val->LongLongInteger = V; break;
                    case TypePointer:
                    case TypeArray:
                        RV->Val->Pointer = (void *)(intptr_t)V;
                        break;
                    case TypeFunction:
                        /* returning function designator as pointer bits */
                        RV->Val->Pointer = (void *)(intptr_t)V;
                        break;
                    case TypeStruct:
                    case TypeUnion:
                    {
                        /* V is pointer to stable blob (IrStructRetBufIR). */
                        int sz = TypeSizeValue(RV, FALSE);
                        void *Src = (void *)(intptr_t)V;
                        if (Src != NULL && RV->Val != NULL && sz > 0)
                            memcpy(RV->Val, Src, (size_t)sz);
                        break;
                    }
                    default: RV->Val->Integer = (int)V; break;
                }
                /* Token path uninit checks treat return Value as a use source;
                 * mark initialized so head()->m / **ret_pp() are not false fails. */
                RV->IsInitialized = TRUE;
            }
            /* Always Mode=Return so for/while/switch in THIS function stop.
             * Nested pure-IR callers restore SavedMode after the call.
             * Token ReturnValue was written only when IrCallDepth==1 above. */
            Parser->Mode = RunModeReturn;
            IR_STMT_BREAK;
        }
        IR_STMT_CASE(IrStDecl)
        {
            long long V = 0;
            int DeclCount = S->DeclArraySize;
            struct Value *Slot = NULL;
            if (Parser->Mode == RunModeGoto)
                IR_STMT_BREAK;
            if (S->Expr != NULL)
                V = IrEvalExpr(Parser, S->Expr);
            /* VLA bounds are runtime expressions. Allocate using the evaluated
             * element count; otherwise the bound can be mistaken for a pointer. */
            if (DeclCount == -2)
            {
                int elem_size = S->DeclElemSize > 0 ? S->DeclElemSize : 1;
                if (V <= 0 || V > 16 * 1024 * 1024 / elem_size)
                {
                    if (S->SourceLine > 0) Parser->Line = S->SourceLine;
                    if (S->CharacterPos > 0) Parser->CharacterPos = S->CharacterPos;
                    ProgramFail(Parser, "invalid variable length array bound");
                }
                DeclCount = (int)V;
            }

            if (S->LocalSlot >= 0 && IrCallDepth > 0 &&
                S->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                Fr->LocalName[S->LocalSlot] = S->DeclIdent;
                /* Every Decl form (incl. the scalar FP early exit below) must
                 * mark const before returning: `const double d; d = 2;`. */
                if (S->DeclIsConst)
                    Fr->LocalIsConst[S->LocalSlot] = 1;
#ifndef NO_FP
                /* scalar float/double: value bits in LocalVal */
                if (S->DeclArraySize == 0 && !S->DeclIsPtr && !S->DeclIsFptr
                    && S->DeclFromType != NULL
                    && S->DeclFromType->Base == TypeFP)
                {
                    long long bits = 0;
                    if (S->DeclInitVals != NULL && S->DeclInitCount > 0)
                        bits = S->DeclInitVals[0];
                    else if (S->Expr != NULL)
                    {
                        /* int expr → float local needs IEEE bits; FP expr already bits */
                        if (IrExprIsFP(Parser, S->Expr))
                            bits = V;
                        else
                            bits = IrFPBits((double)V);
                    }
                    Fr->LocalVal[S->LocalSlot] = bits;
                    Fr->LocalIsPtr[S->LocalSlot] = 0;
                    Fr->LocalFromType[S->LocalSlot] = S->DeclFromType;
                    Fr->LocalTyp[S->LocalSlot] = S->DeclFromType;
                    Fr->LocalElemSize[S->LocalSlot] = (S->DeclElemSize > 0 ? S->DeclElemSize : (int)sizeof(double));
                    Fr->LocalInit[S->LocalSlot] = 1;
                    if (S->LocalSlot >= Fr->NumLocals)
                        IrFrameSetLocalExtent(Fr, S->LocalSlot + 1);
                    IR_STMT_BREAK;
                }
#endif
                if (DeclCount > 0)
                {
                    /* fixed array or VLA OR struct value local (ArraySize==1, DeclFromType set).
                     * Payload is malloc'd (freed in IrFrameFreeOwned). Large locals
                     * (>=512 bytes) also charge IrStackChargeBytes so deep/huge arrays
                     * exhaust PICOC_STACK_SIZE like the token path (126/368/369).
                     * Small arrays skip the bump: charging inside Body-IR then
                     * returning via token VariableStackFramePop can SEGV later
                     * Intrinsics (printf). */
                    size_t nbytes = (size_t)DeclCount * (size_t)(S->DeclElemSize > 0 ? S->DeclElemSize : 1);
                    void *mem;
                    unsigned char *imap;
                    if (S->SourceLine > 0)
                        Parser->Line = S->SourceLine;
                    if (S->CharacterPos > 0)
                        Parser->CharacterPos = S->CharacterPos;
                    if (S->DeclIdent != NULL)
                        ParserSetCaretOnIdent(Parser, S->DeclIdent);
                    {
                        /* Prefer the closing ']' of the array declarator on this line. */
                        const char *Src = Parser->SourceText;
                        const char *LinePos, *CPos, *Best = NULL;
                        int LineCount;
                        if (Src != NULL)
                        {
                            for (LinePos = Src, LineCount = 1;
                                 *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
                                if (*LinePos == '\n') LineCount++;
                            for (CPos = LinePos; *CPos != '\0' && *CPos != '\n'; CPos++)
                            {
                                if (*CPos == ']') Best = CPos;
                                if (*CPos == ';') break;
                            }
                            if (Best != NULL)
                                Parser->CharacterPos = (int)(Best - LinePos) + 1;
                        }
                    }
                    /* Dual stack budget:
                     * 1) Virtual IrStackChargeBytes vs PICOC_STACK_SIZE arena
                     *    (target semantics + test 369 huge single local).
                     * 2) Real host C-stack growth (Body-IR is true host recursion;
                     *    Windows ~1MB default can die before virtual budget hits).
                     * Either exceeded → same ProgramFail / caret as token path. */
                    /* GUARD: shared with linear DECL_ARRAY (IrChargeLocalArrayOrFail). */
                    IrChargeLocalArrayOrFail(Parser, Fr, S->LocalSlot, nbytes,
                                             S->SourceLine, S->CharacterPos,
                                             S->DeclIdent, DeclCount);
                    mem = NULL;
                    /* Static arrays must not live on the frame arena (freed on
                     * pop while callers may still hold &buf). Always malloc. */
                    if (!(S->DeclIsStatic) && nbytes < 512)
                        mem = IrFrameArenaAlloc(Fr, nbytes);
                    if (mem == NULL)
                        mem = malloc(nbytes);
                    /* Struct/union (incl. arrays of struct): byte-level LocalInitMap
                     * (B.1.3) so member uninit does not need Value.ElemInit alone.
                     * Scalar arrays keep per-element map. */
                    {
                        int map_n = DeclCount > 0 ? DeclCount : 1;
                        int is_su = (S->DeclFromType != NULL
                            && (S->DeclFromType->Base == TypeStruct
                                || S->DeclFromType->Base == TypeUnion));
                        if (is_su && nbytes > 0 && nbytes < 65536)
                            map_n = (int)nbytes;
                        imap = NULL;
                        if (IrSafeChecksOn())
                        {
                            imap = (unsigned char *)IrFrameArenaAlloc(Fr, (size_t)map_n);
                            if (imap != NULL)
                                memset(imap, 0, (size_t)map_n);
                            else
                                imap = (unsigned char *)calloc((size_t)map_n, 1);
                        }
                        if (mem == NULL || (IrSafeChecksOn() && imap == NULL))
                            ProgramFail(Parser, "out of memory (local array)");
                        /* C: brace-init zeros the whole object; bare auto is indeterminate.
                         * Static-duration arrays (and structs-as-array) are zero-initialized
                         * even without an explicit initializer (C99 6.7.8/10). */
                        if (S->DeclInitVals != NULL || S->DeclIsStatic)
                        {
                            memset(mem, 0, nbytes);
                            if (imap != NULL)
                                memset(imap, 1, (size_t)map_n);
                        }
                        /* Static local arrays must outlive the frame: callers may
                         * return &buf[0]. Persist via IrStaticLocalGet; never put
                         * them on the frame arena and never LocalOwned-free them. */
                        if (S->DeclIsStatic && S->DeclIdent != NULL)
                        {
                            IrStaticLocalEntry *SE = IrStaticLocalGet(S->DeclIdent, S->SourceLine, 1);
                            if (SE != NULL && SE->Inited && SE->Val != 0)
                            {
                                /* Reuse previous persistent buffer; drop this alloc. */
                                if (mem != NULL)
                                    free(mem);
                                mem = (void *)(intptr_t)SE->Val;
                            }
                            else if (SE != NULL)
                            {
                                SE->Val = (long long)(intptr_t)mem;
                                SE->Inited = 1;
                            }
                            Fr->LocalIsStatic[S->LocalSlot] = 1;
                            Fr->LocalStaticLine[S->LocalSlot] = S->SourceLine;
                            IrFrameSetLocalOwned(Fr, S->LocalSlot, 0); /* process-lifetime */
                        }
                        else
                            IrFrameSetLocalOwned(Fr, S->LocalSlot, 1); /* malloc; free in IrFrameFreeOwned */
                        Fr->LocalVal[S->LocalSlot] = (long long)(intptr_t)mem;
                        /* Static struct/array: also mark Variable shadow fully init so
                         * member loads (l.x, l.n[1]) pass uninit checks. */
                        if (S->DeclIsStatic && S->DeclIdent != NULL
                            && (IrGlobalIdentSoft(Parser, S->DeclIdent) != NULL))
                        {
                            struct Value *SV = NULL;
                            SV = IrShadowGet(Parser, S->DeclIdent); /* shadow bridge */
                            if (SV != NULL)
                            {
                                SV->IsInitialized = TRUE;
                                if (SV->ElemInit != NULL && SV->ElemInitCount > 0)
                                    memset(SV->ElemInit, 1, (size_t)SV->ElemInitCount);
                            }
                        }
                        /* Scalar owned struct/union: LocalIsPtr=0 (value blob).
                         * Array including T a[1] (DeclIsPtr from compile): LocalIsPtr=1.
                         * DeclCount alone cannot distinguish S v vs S v[1] (both 1). */
                        Fr->LocalIsPtr[S->LocalSlot] =
                            (is_su && DeclCount <= 1 && !S->DeclIsPtr) ? 0 : 1;
                        Fr->LocalInitMap[S->LocalSlot] = imap;
                        /* Keep element count/stride for indexing; byte-map length
                         * is derived in IrFrameInitMapLen from Sizeof * count. */
                        {
                            int leaf_es = S->DeclElemSize;
                            if (leaf_es <= 0)
                            {
                                if (S->DeclElemIsPtr || S->DeclIsFptr)
                                    leaf_es = (int)sizeof(void *);
                                else
                                    leaf_es = IrResolveDeclElemSize(0, S->DeclFromType);
                            }
                            Fr->LocalElemSize[S->LocalSlot] = (leaf_es > 0 ? leaf_es : 1);
                        }
                        Fr->LocalArraySize[S->LocalSlot] = DeclCount > 0 ? DeclCount : (is_su ? 1 : 0);
                    }
                    {
                        int di;
                        Fr->LocalNDims[S->LocalSlot] = (unsigned char)(S->DeclNDims > 0 ? S->DeclNDims : (DeclCount > 0 ? 1 : 0));
                        for (di = 0; di < IR_MAX_ARRAY_DIMS; di++)
                            Fr->LocalDims[S->LocalSlot][di] = (di < S->DeclNDims) ? S->DeclDims[di] : 0;
                        if (S->DeclNDims <= 0 && DeclCount > 0)
                            Fr->LocalDims[S->LocalSlot][0] = DeclCount;
                    }
                    Fr->LocalInit[S->LocalSlot] = 1; /* name exists */
                    Fr->LocalFromType[S->LocalSlot] = S->DeclFromType;
                    Fr->LocalTyp[S->LocalSlot] = S->DeclFromType;
                    /* struct S t = make(...); / S t = other; — copy from expr
                     * result (pointer to return blob or source local). */
                    if (S->Expr != NULL && S->DeclFromType != NULL
                        && (S->DeclFromType->Base == TypeStruct
                            || S->DeclFromType->Base == TypeUnion)
                        && mem != NULL)
                    {
                        /* The initializer was evaluated once above (C11 6.7.9:
                         * one evaluation); re-evaluating repeated its side
                         * effects (arr[i++], va_arg). */
                        long long srcp = V;
                        void *src = (void *)(intptr_t)srcp;
                        int sz = S->DeclFromType->Sizeof > 0 ? S->DeclFromType->Sizeof
                                 : (S->DeclElemSize > 0 ? S->DeclElemSize : 0);
                        if (src != NULL && sz > 0)
                        {
                            int mn = 0;
                            if (imap != NULL)
                            {
                                mn = IrFrameInitMapLen(Fr, S->LocalSlot);
                                if (mn <= 0 && sz > 0) mn = sz;
                            }
                            /* GUARD: IrCopyStructBlob */
                            IrCopyStructBlob(mem, src, sz, imap, mn);
                        }
                        /* mark Variable shadow fully init */
                        if (S->DeclIdent != NULL
                            && (IrGlobalIdentSoft(Parser, S->DeclIdent) != NULL))
                        {
                            struct Value *SV = NULL;
                            SV = IrShadowGet(Parser, S->DeclIdent); /* shadow bridge */
                            if (SV != NULL)
                            {
                                SV->IsInitialized = TRUE;
                                if (SV->ElemInit != NULL && SV->ElemInitCount > 0)
                                    memset(SV->ElemInit, 1, (size_t)SV->ElemInitCount);
                            }
                        }
                    }
                    /* int (*tab[N])(...): elements are function pointers */
                    if (S->DeclIsFptr)
                        Fr->LocalIsFptr[S->LocalSlot] = 1;
                    /* T *a[N]: elements are data pointers (not scalar T a[N]) */
                    if (S->DeclElemIsPtr)
                        Fr->LocalElemIsPtr[S->LocalSlot] = 1;
                    /* Brace-init: T a[N] = { c0, e1, ... } — const in Vals, runtime in Exprs.
                     * DeclFromType is now set for scalar value arrays (unsigned loads);
                     * still use integer store path unless FP/struct/union. */
                    /* int dst[] = src; GUARD: IrCopyArrayIdentInit only. */
                    if (S->DeclInitCount == -1 && S->Expr != NULL
                        && S->Expr->Kind == IrExIdent && S->Expr->LocalSlot >= 0
                        && IrCallDepth > 0)
                    {
                        IrCallFrame *FrS = IrCallStack[IrCallDepth - 1];
                        int es = S->DeclElemSize > 0 ? S->DeclElemSize : 1;
                        int n = S->DeclArraySize > 0 ? S->DeclArraySize : 0;
                        (void)IrCopyArrayIdentInit(Parser, FrS, mem, n, es,
                            S->Expr, imap);
                    }
                    else if (S->DeclInitVals != NULL
                        && (S->DeclFromType == NULL
                            || (S->DeclFromType->Base != TypeFP
                                && S->DeclFromType->Base != TypeStruct
                                && S->DeclFromType->Base != TypeUnion)))
                    {
                        int ei, es = S->DeclElemSize > 0 ? S->DeclElemSize : 1;
                        char *base = (char *)mem;
                        for (ei = 0; ei < S->DeclInitCount && ei < S->DeclArraySize; ei++)
                        {
                            long long cv = S->DeclInitVals[ei];
                            if (S->DeclInitExprs != NULL && S->DeclInitExprs[ei] != NULL)
                                cv = IrEvalExpr(Parser, S->DeclInitExprs[ei]);
                            /* GUARD: IrWriteByElemSize (shared tree+lin). */
                            IrWriteByElemSize(base + (size_t)ei * (size_t)es, es, cv);
                        }
                        IrMarkInitMapRange(imap, S->DeclArraySize);
                    }
#ifndef NO_FP
                    /* float/double local / array brace-init (bits in DeclInitVals) */
                    else if (S->DeclInitVals != NULL && S->DeclFromType != NULL
                             && S->DeclFromType->Base == TypeFP)
                    {
                        int ei, es = IrResolveDeclElemSize(S->DeclElemSize, S->DeclFromType);
                        if (es <= 0) es = (int)sizeof(double); /* execution-slot compatibility */
                        char *base = (char *)mem;
                        for (ei = 0; ei < S->DeclInitCount && ei < S->DeclArraySize; ei++)
                        {
                            union { long long l; double d; } u;
                            u.l = S->DeclInitVals[ei];
                            if (es >= (int)sizeof(double))
                                *(double *)(base + (size_t)ei * (size_t)es) = u.d;
                            else
                                *(float *)(base + (size_t)ei * (size_t)es) = (float)u.d;
                        }
                        if (imap != NULL)
                        {
                            for (ei = 0; ei < S->DeclArraySize; ei++)
                                imap[ei] = 1;
                        }
                    }
#endif
                    /* Struct / struct-array brace-init — unified cold pack only.
                     * GUARD: no local collect/sort/write; see IrPackStructBraceInit. */
                    if (S->DeclInitVals != NULL && S->DeclFromType != NULL
                        && (S->DeclFromType->Base == TypeStruct
                            || S->DeclFromType->Base == TypeUnion)
                        && S->DeclFromType->Members != NULL)
                    {
                        int esz = S->DeclElemSize > 0 ? S->DeclElemSize
                            : S->DeclFromType->Sizeof;
                        int narr = S->DeclArraySize > 0 ? S->DeclArraySize : 1;
                        int i;
                        (void)IrPackStructBraceInit(Parser, mem, S->DeclFromType,
                            S->DeclInitVals, S->DeclInitNames,
                            S->DeclInitExprs, S->DeclInitCount, narr, esz);
                        if (imap != NULL)
                        {
                            for (i = 0; i < narr; i++)
                                imap[i] = 1;
                        }
                    }
                    /* Struct / struct-array local: frame owns blob + byte
                     * LocalInitMap. No new VariableDefine (B.1.3); optional
                     * shadow update only if name already in Variable table. */
                    if (S->DeclFromType != NULL
                        && (S->DeclFromType->Base == TypeStruct || S->DeclFromType->Base == TypeUnion)
                        && S->DeclIdent != NULL
                        && (IrGlobalIdentSoft(Parser, S->DeclIdent) != NULL))
                    {
                        struct Value *SlotV = NULL;
                        struct ValueType *ShadowTy = S->DeclFromType;
                        int braced = (S->DeclInitVals != NULL);
                        int fully = braced || S->DeclIsStatic || (S->Expr != NULL);
                        if (S->DeclArraySize > 1)
                        {
                            ShadowTy = TypeAdd(Parser->pc, Parser, S->DeclFromType,
                                TypeArray, S->DeclArraySize, Parser->pc->StrEmpty,
                                (int)nbytes, 0);
                        }
                        SlotV = IrShadowGet(Parser, S->DeclIdent); /* shadow bridge */
                        if (SlotV != NULL)
                        {
                            SlotV->Typ = ShadowTy;
                            SlotV->Val = (union AnyValue *)mem;
                            SlotV->IsLValue = TRUE;
                            SlotV->IsInitialized = fully ? TRUE : FALSE;
                            if (SlotV->ElemInit != NULL)
                            {
                                free(SlotV->ElemInit);
                                SlotV->ElemInit = NULL;
                            }
                            if (nbytes > 0 && nbytes < 65536)
                            {
                                SlotV->ElemInit = (unsigned char *)calloc(nbytes, 1);
                                SlotV->ElemInitCount = (int)nbytes;
                                if (fully)
                                    memset(SlotV->ElemInit, 1, (size_t)nbytes);
                            }
                        }
                    }
                }
                else if (S->DeclIsFptr)
                {
                    struct ValueType *FPtrTy;
                    struct Value *Slot = NULL;
                    Fr->LocalVal[S->LocalSlot] = V;
                    Fr->LocalIsFptr[S->LocalSlot] = 1;
                    Fr->LocalIsPtr[S->LocalSlot] = 0;
                    if (S->Expr != NULL)
                        Fr->LocalInit[S->LocalSlot] = 1;
                    /* Shadow in Variable table so VarGet/IR call both work */
                    FPtrTy = TypeAdd(Parser->pc, Parser, &Parser->pc->FunctionType,
                                    TypePointer, 0, Parser->pc->StrEmpty,
                                    sizeof(void *), 0);
                    Fr->LocalTyp[S->LocalSlot] = FPtrTy;
                    Fr->LocalFromType[S->LocalSlot] = FPtrTy;
                    /* B.1.3: pure IR fptr local — frame is truth (LocalIsFptr /
                     * LocalVal). Optional shadow only if already defined
                     * (token interop); do not VariableDefine new names. */
                    if (S->DeclIdent != NULL && (IrGlobalIdentSoft(Parser, S->DeclIdent) != NULL))
                    {
                        IrVarTableGet(Parser, S->DeclIdent, &Slot); /* global only */
                        if (Slot != NULL && Slot->Typ != NULL)
                        {
                            Slot->Typ = FPtrTy;
                            Slot->Val->Pointer = (void *)(intptr_t)V;
                            Slot->IsLValue = TRUE;
                        }
                    }
                }
                else if (S->DeclIsPtr)
                {
                    struct ValueType *PtrTy = NULL;
                    struct Value *SlotV = NULL;
                    Fr->LocalVal[S->LocalSlot] = V; /* address from init expr, else 0 */
                    Fr->LocalIsPtr[S->LocalSlot] = 1;
                    /* Pointer slot must never look like an owned array/struct blob
                     * (LocalArraySize>0 + LocalOwned skips assign stores). */
                    Fr->LocalArraySize[S->LocalSlot] = 0;
                    IrFrameSetLocalOwned(Fr, S->LocalSlot, 0);
                    Fr->LocalNDims[S->LocalSlot] = 0;
                    Fr->LocalElemSize[S->LocalSlot] = (S->DeclElemSize > 0 ? S->DeclElemSize : 1);
                    {
                        int ps = S->DeclPointeeSize;
                        if (ps <= 0 && S->DeclFromType != NULL && S->DeclFromType->Sizeof > 0
                            && S->DeclFromType->Base != TypePointer)
                            ps = S->DeclFromType->Sizeof;
                        if (ps > 0)
                            IrFrameSetPointeeSize(Fr, S->LocalSlot, ps);
                    }
                    Fr->LocalFromType[S->LocalSlot] = S->DeclFromType;
                    if (S->Expr != NULL)
                        Fr->LocalInit[S->LocalSlot] = 1;
                    /* B.1.3: type lives on frame (LocalTyp/LocalFromType).
                     * No new VariableDefine for IR ptr locals — resolve/load
                     * use LocalSlot. Keep shadow update only if name already
                     * exists (mixed token path). */
                    if (S->DeclFromType != NULL)
                        PtrTy = TypeAdd(Parser->pc, Parser, S->DeclFromType,
                                        TypePointer, 0, Parser->pc->StrEmpty,
                                        sizeof(void *), 0);
                    else
                        PtrTy = Parser->pc->VoidPtrType;
                    Fr->LocalTyp[S->LocalSlot] = PtrTy;
                    if (S->DeclIdent != NULL && (IrGlobalIdentSoft(Parser, S->DeclIdent) != NULL))
                    {
                        SlotV = IrShadowGet(Parser, S->DeclIdent); /* shadow bridge */
                        if (SlotV != NULL && SlotV->Val != NULL)
                        {
                            if (PtrTy != NULL)
                                SlotV->Typ = PtrTy;
                            SlotV->Val->Pointer = (void *)(intptr_t)V;
                            SlotV->IsLValue = TRUE;
                            if (S->Expr != NULL)
                                SlotV->IsInitialized = TRUE;
                        }
                    }
                }
                else
                {
                    Fr->LocalIsPtr[S->LocalSlot] = 0;
                    Fr->LocalElemSize[S->LocalSlot] = (S->DeclElemSize > 0 ? S->DeclElemSize : 0);
                    Fr->LocalFromType[S->LocalSlot] = S->DeclFromType;
                    Fr->LocalTyp[S->LocalSlot] = S->DeclFromType;
                    if (S->Expr != NULL)
                    {
#ifndef NO_FP
                        if (IrExprIsFP(Parser, S->Expr))
                            V = (long long)IrBitsFP(V);
                        else
#endif
                        if (S->DeclFromType != NULL && S->DeclFromType->Base == TypeInt)
                            V = (long long)(int)V;
                        else if (S->DeclFromType != NULL && S->DeclFromType->Base == TypeUnsignedInt)
                            V = (long long)(unsigned int)V;
                        Fr->LocalVal[S->LocalSlot] = IrNarrowToLocalType(Fr, S->LocalSlot, V);
                        Fr->LocalInit[S->LocalSlot] = 1;
                    }
                    else if (S->DeclInitVals != NULL && S->DeclInitCount > 0)
                    {
                        Fr->LocalVal[S->LocalSlot] = IrNarrowToLocalType(Fr, S->LocalSlot, S->DeclInitVals[0]);
                        Fr->LocalInit[S->LocalSlot] = 1;
                    }
                    else
                    {
#ifndef NO_FP
                        /* int x = float_expr → truncate */
                        if (S->Expr != NULL && IrExprIsFP(Parser, S->Expr))
                            V = (long long)IrBitsFP(V);
#endif
                        Fr->LocalVal[S->LocalSlot] = V;
                        /* if Expr present we did initialize */
                        Fr->LocalInit[S->LocalSlot] = (S->Expr != NULL) ? 1 : 0;
                    }
                    /* C static local: first visit applies init, later visits reload */
                    if (S->DeclIsStatic && S->DeclIdent != NULL
                        && S->DeclArraySize == 0 && !S->DeclIsPtr)
                    {
                        IrStaticLocalEntry *E = IrStaticLocalGet(S->DeclIdent, S->SourceLine, 1);
                        if (E != NULL)
                        {
                            if (!E->Inited)
                            {
                                E->Val = Fr->LocalVal[S->LocalSlot];
                                E->Inited = 1;
                            }
                            else
                                Fr->LocalVal[S->LocalSlot] = E->Val;
                            Fr->LocalInit[S->LocalSlot] = 1;
                            Fr->LocalIsStatic[S->LocalSlot] = 1;
                            Fr->LocalStaticLine[S->LocalSlot] = S->SourceLine;
                        }
                    }
                }
                if (S->LocalSlot >= Fr->NumLocals)
                    IrFrameSetLocalExtent(Fr, S->LocalSlot + 1);
                IR_STMT_BREAK;
            }

            /* Token-only paths: DeclSlot / Variable-table reuse.
             * Body-IR frames use LocalSlot (above) or free-slot fallback. */
            if (IrCallDepth <= 0 && S->DeclSlot != NULL)
            {
                Slot = (struct Value *)S->DeclSlot;
                Slot->OutOfScope = FALSE;

                Slot->IsLValue = TRUE;
                if (S->Expr != NULL)
                    ExpressionAssignInt(Parser, Slot, (PTR_TYPE_INT_SIGNED)V, FALSE);
                IR_STMT_BREAK;
            }

            if (IrCallDepth <= 0
                && (IrGlobalIdentSoft(Parser, S->DeclIdent) != NULL))
            {
                Slot = IrShadowGet(Parser, S->DeclIdent); /* shadow bridge */
                S->DeclSlot = Slot;

                Slot->IsLValue = TRUE;
                if (S->Expr != NULL)
                    ExpressionAssignInt(Parser, Slot, (PTR_TYPE_INT_SIGNED)V, FALSE);
                IR_STMT_BREAK;
            }

            {
                /* Non-slot scalar fallback: take free frame LocalSlot (B.3, no VariableDefine). */
                int ns;
                IrCallFrame *FrFb;
                if (IrCallDepth <= 0)
                    ProgramFail(Parser, "decl outside IR frame");
                FrFb = IrCallStack[IrCallDepth - 1];
                ns = FrFb->NumLocals;
                if (ns < 0 || ns >= IR_CALL_MAX_LOCALS)
                    ProgramFail(Parser, "too many locals for decl fallback");
                S->LocalSlot = ns;
                FrFb->LocalName[ns] = S->DeclIdent;
                FrFb->LocalVal[ns] = (S->Expr != NULL) ? V : 0;
                FrFb->LocalInit[ns] = (S->Expr != NULL) ? 1 : 0;
                FrFb->LocalIsPtr[ns] = 0;
                FrFb->LocalTyp[ns] = &Parser->pc->IntType;
                FrFb->LocalFromType[ns] = &Parser->pc->IntType;
                FrFb->LocalElemSize[ns] = 0;
                IrFrameSetLocalExtent(FrFb, ns + 1);
            }
            IR_STMT_BREAK;
        }

        IR_STMT_CASE(IrStCase)
        IR_STMT_CASE(IrStDefault)
            /* markers only — switch driver handles dispatch; fall-through no-op */
            IR_STMT_BREAK;

        IR_STMT_CASE(IrStSwitch)
        {
            long long V;
            IrStmt *Body;
            int si, start = -1, defi = -1;
            if (Parser->Mode == RunModeGoto)
            {
                /* still search labels inside switch body */
                if (S->BodyStmt)
                    IrExecuteStmt(Parser, S->BodyStmt);
                IR_STMT_BREAK;
            }
            V = IrEvalExpr(Parser, S->Expr);
            Body = S->BodyStmt;
            if (Body == NULL) IR_STMT_BREAK;
            if (Body->Kind == IrStBlock)
            {
                for (si = 0; si < Body->StmtCount; si++)
                {
                    IrStmt *T = Body->Stmts[si];
                    if (T == NULL) continue;
                    if (T->Kind == IrStCase && T->Expr != NULL
                        && IrEvalExpr(Parser, T->Expr) == V)
                    {
                        start = si;
                        break;
                    }
                    if (T->Kind == IrStDefault)
                        defi = si;
                }
                if (start < 0)
                    start = defi;
                if (start < 0)
                    IR_STMT_BREAK;
                for (si = start; si < Body->StmtCount && Parser->Mode == RunModeRun; si++)
                {
                    IrStmt *T = Body->Stmts[si];
                    if (T == NULL) continue;
                    if (T->Kind == IrStCase || T->Kind == IrStDefault)
                        continue;
                    IrExecuteStmt(Parser, T);
                    if (Parser->Mode == RunModeBreak)
                    {
                        Parser->Mode = RunModeRun;
                        break;
                    }
                }
            }
            else
                IrExecuteStmt(Parser, Body);
            IR_STMT_BREAK;
        }

        IR_STMT_CASE(IrStGoto)
            if (Parser->Mode == RunModeRun && S->DeclIdent != NULL)
            {
                IrGotoTarget = S->DeclIdent;
                Parser->Mode = RunModeGoto;
            }
            IR_STMT_BREAK;

        IR_STMT_CASE(IrStLabel)
            if (Parser->Mode == RunModeGoto && S->DeclIdent != NULL
                && IrGotoTarget != NULL
                && strcmp(S->DeclIdent, IrGotoTarget) == 0)
            {
                IrGotoTarget = NULL;
                Parser->Mode = RunModeRun;
            }
            IR_STMT_BREAK;

        IR_STMT_CASE(IrStBreak)
            if (Parser->Mode == RunModeGoto) IR_STMT_BREAK;
            Parser->Mode = RunModeBreak;
            IR_STMT_BREAK;
        IR_STMT_CASE(IrStContinue)
            if (Parser->Mode == RunModeGoto) IR_STMT_BREAK;
            Parser->Mode = RunModeContinue;
            IR_STMT_BREAK;

        IR_STMT_CASE(IrStWhile)
        {
            enum RunMode Pre = Parser->Mode;
            /* Ultra-fast char scan needs no scope machinery */
            if (IrTryFastWhileChain(Parser, S))
                IR_STMT_BREAK;
            {
            int PrevScopeID = 0, ScopeID = IrScopeBegin(Parser, &PrevScopeID);
            IrInLoopBody++;
            while (IrModeAllowsContinue(Parser) && Parser->Mode == RunModeRun)
            {
                long long C;
                if (S->FastCond.Active)
                    C = FastForEvalCond(Parser, (FastForCond *)&S->FastCond);
                else
                    C = IrEvalExpr(Parser, S->Expr);
                if (!C)
                    break;
                IrExecuteBodyFlat(Parser, S->BodyStmt);
                if (Parser->Mode == RunModeContinue)
                    Parser->Mode = Pre;
                if (Parser->Mode == RunModeBreak)
                {
                    Parser->Mode = Pre;
                    break;
                }
                if (Parser->Mode == RunModeReturn)
                    break;
            }
            IrInLoopBody--;
            IrClearStickyTree(S);
            IrScopeEnd(Parser, ScopeID, PrevScopeID);
            }
            IR_STMT_BREAK;
        }

        IR_STMT_CASE(IrStDo)
        {
            enum RunMode Pre = Parser->Mode;
            int PrevScopeID = 0, ScopeID = IrScopeBegin(Parser, &PrevScopeID);
            IrInLoopBody++;
            for (;;)
            {
                IrExecuteBodyFlat(Parser, S->BodyStmt);
                if (Parser->Mode == RunModeContinue)
                    Parser->Mode = Pre;
                if (Parser->Mode == RunModeBreak)
                {
                    Parser->Mode = Pre;
                    break;
                }
                if (Parser->Mode == RunModeReturn)
                    break;
                {
                    long long C;
                    if (S->FastCond.Active)
                        C = FastForEvalCond(Parser, (FastForCond *)&S->FastCond);
                    else
                        C = IrEvalExpr(Parser, S->Expr);
                    if (!C || Parser->Mode != RunModeRun)
                        break;
                }
            }
            IrInLoopBody--;
            IrClearStickyTree(S);
            IrScopeEnd(Parser, ScopeID, PrevScopeID);
            IR_STMT_BREAK;
        }

        IR_STMT_CASE(IrStFor)
        {
            enum RunMode Pre = Parser->Mode;
            if (IrTryFastPtrScanFor(Parser, S))
                IR_STMT_BREAK;
            {
            int PrevScopeID = 0, ScopeID = IrScopeBegin(Parser, &PrevScopeID);
            IrInLoopBody++;
            if (S->InitStmt)
                IrExecuteStmt(Parser, S->InitStmt);
            /* Point2: ForFpKind from compile (e.g. TIGHT) or first probe.
             * Single dispatch — no duplicated k==2..13 / else-if chains. */
            if (IrTryFastForAfterInit(Parser, S))
            {
                IrInLoopBody--;
                IrClearStickyTree(S);
                IrScopeEnd(Parser, ScopeID, PrevScopeID);
                IR_STMT_BREAK;
            }
            {
            /* LICM: hoist leading local=const out of body */
            IrStmt **licm_list = NULL;
            int licm_n = 0, licm_skip = 0;
            if (S->BodyStmt != NULL && S->BodyStmt->Kind == IrStBlock
                && S->BodyStmt->Stmts != NULL)
            {
                licm_list = S->BodyStmt->Stmts;
                licm_n = S->BodyStmt->StmtCount;
                licm_skip = IrLicmLeadingConstAssigns(Parser, S, licm_list, licm_n);
            }
            while (IrModeAllowsContinue(Parser) && Parser->Mode == RunModeRun)
            {
                long long C = 1;
                int bi;
                if (S->FastCond.Active)
                    C = FastForEvalCond(Parser, (FastForCond *)&S->FastCond);
                else if (S->Expr)
                    C = IrEvalExpr(Parser, S->Expr);
                if (!C)
                    break;
                if (licm_skip > 0 && licm_list != NULL)
                {
                    for (bi = licm_skip; bi < licm_n; bi++)
                        if (licm_list[bi] != NULL)
                            IrExecuteStmt(Parser, licm_list[bi]);
                }
                else
                    IrExecuteBodyFlat(Parser, S->BodyStmt);
                if (Parser->Mode == RunModeContinue)
                    Parser->Mode = Pre;
                if (Parser->Mode == RunModeBreak)
                {
                    Parser->Mode = Pre;
                    break;
                }
                if (Parser->Mode == RunModeReturn)
                    break;
                if (S->IncStmt)
                    IrExecuteStmt(Parser, S->IncStmt);
                else if (S->FastInc.Active)
                    FastForDoInc(Parser, (FastForInc *)&S->FastInc);
            }
            IrInLoopBody--;
            IrClearStickyTree(S);
            IrScopeEnd(Parser, ScopeID, PrevScopeID);
            } /* LICM scope */
            }
            IR_STMT_BREAK;
        }

        IR_STMT_DEFAULT
            IR_STMT_BREAK;
#if !BABYC_IR_STMT_CGOTO
    }
#endif
}

