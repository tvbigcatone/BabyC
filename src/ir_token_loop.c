/* ir_token_loop.c — token-path loop executor (not Body-IR fast path) */
#include "picoc.h"
#include <string.h>
#include <stdlib.h>
#include "ir_runtime.h"
#include "ir_bridge.h"
#include "ir_private.h" /* FastForEvalCond / FastForDoInc / FastForCaptureInc */

void LoopRunBody(struct ParseState *Parser, IrLoop *Loop, int CheckTrailingSemicolon)
{
    struct ParseState Sub; (void)Sub;
    void *BodyTokens = Loop->BodyTokens;
    struct ParseState *BodyTemplate = &Loop->BodyTemplate;

    /* Prefer compiled IR body — unless debug / TOKEN_FAST=0 (step must see statements) */
    if (Loop->BodyIr != NULL && IrTokenFastAllowed(Parser))
    {
        static int TightDebug = -1; /* getenv once, not per iteration */
        if (TightDebug < 0)
            TightDebug = getenv("BABYC_TIGHT_DEBUG") != NULL;
        if (TightDebug)
            fprintf(stderr, "[loopbody] BodyIr kind=%d\n", ((IrStmt*)Loop->BodyIr)->Kind);
        IrExecuteStmt(Parser, Loop->BodyIr);
        return;
    }

    if (BodyTokens != NULL)
    {
        if (*(unsigned char *)BodyTokens == (unsigned char)TokenEOF)
            return;
        ParserCopy(&Sub, Parser);
        Sub.Pos = BodyTokens;
        Sub.Line = BodyTemplate->Line;
        Sub.CharacterPos = BodyTemplate->CharacterPos;
        ParseStatement(&Sub, CheckTrailingSemicolon);
        Parser->Mode = Sub.Mode;
    }
    else
    {
        ParserCopyPos(Parser, BodyTemplate);
        if (ParseStatement(Parser, CheckTrailingSemicolon) != ParseResultOk)
            ProgramFail(Parser, "statement expected");
    }
}

/* Evaluate loop condition: FastCond if active, else re-parse from CondFrom.
 * If consumeCloseBracket, also expect ')' after the expression (while/for header). */
static PTR_TYPE_INT_SIGNED LoopEvalCond(struct ParseState *Parser, FastForCond *FastCond,
                                        struct ParseState *CondFrom, int ConsumeCloseBracket)
{
    PTR_TYPE_INT_SIGNED Condition;

    if (FastCond != NULL && FastCond->Active)
        return FastForEvalCond(Parser, FastCond);

    ParserCopyPos(Parser, CondFrom);
    Condition = ExpressionParseInt(Parser);
    if (ConsumeCloseBracket)
    {
        if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
            ProgramFail(Parser, "')' expected");
    }
    return Condition;
}

/* Copy FastForCond → IrCond (same layout/Active codes) */
void IrCondFromFast(IrCond *Dst, FastForCond *Src)
{
    Dst->Active = Src->Active;
    Dst->VarName = Src->VarName;
    Dst->Op = Src->Op;
    Dst->BoundIsVar = Src->BoundIsVar;
    Dst->Bound = Src->Bound;
    Dst->BoundVarName = Src->BoundVarName;
    Dst->VarSlot = Src->VarSlot;
    Dst->BoundSlot = Src->BoundSlot;
    Dst->CachedVar = Src->CachedVar;
    Dst->CachedBound = Src->CachedBound;
}

void IrIncFromFast(IrInc *Dst, FastForInc *Src)
{
    Dst->Active = Src->Active;
    Dst->VarName = Src->VarName;
    Dst->VarSlot = Src->VarSlot;
    Dst->CachedVar = Src->CachedVar;
}

/* If body is { stmts; i++; } matching VarName, set *Inc and set *NewBodyStart / *NewBodyEnd
 * to a span that is a single valid statement (or empty). Only optimizes when there is
 * at most one statement before the trailing inc (so LoopRunBody can ParseStatement once). */
int TryStripTrailingInc(struct ParseState *BodyStart, struct ParseState *BodyEnd,
                               const char *VarName, FastForInc *Inc,
                               struct ParseState *NewBodyStart, struct ParseState *NewBodyEnd)
{
    (void)BodyEnd;
    struct ParseState P;
    struct ParseState FirstStart;
    struct ParseState LastStart;
    enum RunMode SavedMode;
    enum LexToken Tok;
    int StmtCount = 0;

    if (VarName == NULL || Inc == NULL)
        return 0;

    Inc->Active = 0;
    ParserCopy(&P, BodyStart);
    SavedMode = P.Mode;
    P.Mode = RunModeSkip;

    Tok = LexGetToken(&P, NULL, FALSE);
    if (Tok == TokenLeftBrace)
    {
        LexGetToken(&P, NULL, TRUE); /* consume '{' */
        if (LexGetToken(&P, NULL, FALSE) == TokenRightBrace)
        {
            P.Mode = SavedMode;
            return 0;
        }
        ParserCopy(&FirstStart, &P);
        ParserCopy(&LastStart, &P);
        while (LexGetToken(&P, NULL, FALSE) != TokenRightBrace && P.Mode != RunModeGoto)
        {
            ParserCopy(&LastStart, &P);
            if (ParseStatement(&P, TRUE) != ParseResultOk)
            {
                P.Mode = SavedMode;
                return 0;
            }
            StmtCount++;
        }
        if (StmtCount < 1 || !FastForCaptureInc(&LastStart, Inc))
        {
            P.Mode = SavedMode;
            return 0;
        }
        if (strcmp(Inc->VarName, VarName) != 0)
        {
            Inc->Active = 0;
            P.Mode = SavedMode;
            return 0;
        }
        {
            struct Value *V;
            /* Integer FastInc only — pointer p=p+1 must use real pointer arithmetic */
            IrVarTableGet(&P, Inc->VarName, &V); /* compile/capture path */
            if (V->Typ->Base == TypePointer || V->Typ->Base == TypeArray)
            {
                Inc->Active = 0;
                P.Mode = SavedMode;
                return 0;
            }
        }
        /* Only safe if trailing-inc is the only extra statement (0 or 1 before it) */
        if (StmtCount > 2)
        {
            Inc->Active = 0;
            P.Mode = SavedMode;
            return 0;
        }
        if (StmtCount == 1)
        {
            /* body was only the increment */
            ParserCopy(NewBodyStart, BodyStart);
            ParserCopy(NewBodyEnd, BodyStart);
        }
        else
        {
            /* one statement before trailing inc — span is that single statement */
            ParserCopy(NewBodyStart, &FirstStart);
            ParserCopy(NewBodyEnd, &LastStart);
        }
        P.Mode = SavedMode;
        return 1;
    }

    if (FastForCaptureInc(BodyStart, Inc) && strcmp(Inc->VarName, VarName) == 0)
    {
        struct Value *V;
        IrVarTableGet(BodyStart, Inc->VarName, &V); /* compile/capture path */
        if (V->Typ->Base == TypePointer || V->Typ->Base == TypeArray)
        {
            Inc->Active = 0;
            P.Mode = SavedMode;
            return 0;
        }
        ParserCopy(NewBodyStart, BodyStart);
        ParserCopy(NewBodyEnd, BodyStart);
        return 1;
    }
    Inc->Active = 0;
    P.Mode = SavedMode;
    return 0;
}


/* Generic loop executor: parser only fills IrLoop; this runs iterations.
 * For / while: test condition then body.
 * Do: body then condition (caller runs first body before calling this, or Kind==Do). */

void IrExecuteLoop(struct ParseState *Parser, IrLoop *Loop, enum RunMode PreMode)
{
    PTR_TYPE_INT_SIGNED Condition;
    FastForCond FC;
    FastForInc FI;

    if (IrTryTightLoopVars(Parser, Loop))
        return;

    /* Adapt to existing Fast* eval helpers without duplicating compare logic */
    memset(&FC, 0, sizeof(FC));
    memset(&FI, 0, sizeof(FI));
    FC.Active = Loop->Cond.Active;
    FC.VarName = Loop->Cond.VarName;
    FC.Op = Loop->Cond.Op;
    FC.BoundIsVar = Loop->Cond.BoundIsVar;
    FC.Bound = Loop->Cond.Bound;
    FC.BoundVarName = Loop->Cond.BoundVarName;
    FC.VarSlot = Loop->Cond.VarSlot;
    FC.BoundSlot = Loop->Cond.BoundSlot;
    FC.CachedVar = Loop->Cond.CachedVar;
    FC.CachedBound = Loop->Cond.CachedBound;
    FI.Active = Loop->Inc.Active;
    FI.VarName = Loop->Inc.VarName;
    FI.VarSlot = Loop->Inc.VarSlot;
    FI.CachedVar = Loop->Inc.CachedVar;

    /* for:  body0; then { inc; cond; body }  (inc before cond on subsequent iters)
     * while/do with optional trailing FastInc: body0; then { cond; body; inc } */
    while (IrModeAllowsContinue(Parser) && Parser->Mode == RunModeRun)
    {
        if (Loop->Kind == IrLoopFor && FI.Active)
            FastForDoInc(Parser, &FI);

        Condition = LoopEvalCond(Parser, &FC, &Loop->CondFrom, Loop->CondNeedsCloseBracket);
        if (!Condition || Parser->Mode != RunModeRun)
            break;

        LoopRunBody(Parser, Loop, TRUE);
        {
            int Continued = 0;
            if (Parser->Mode == RunModeContinue)
            {
                Parser->Mode = PreMode;
                Continued = 1;
            }
            /* while/do stripped trailing ++ must not run on continue (C semantics) */
            if (Loop->Kind != IrLoopFor && FI.Active &&
                Parser->Mode == RunModeRun && !Continued)
                FastForDoInc(Parser, &FI);
        }
    }

    if (Parser->Mode == RunModeBreak)
        Parser->Mode = PreMode;
}


