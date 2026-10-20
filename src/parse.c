/* picoc parser - parses source and executes statements */

#include "picoc.h"
#include "cstdlib/printf_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "interpreter.h"
#include "ir_runtime.h"
#include "ir.h"

/* IR compile lives in ir_compile.c; stats still printed from parse. */
extern int PicocIrBodyCompiled;
extern int PicocIrBodyTokenFallback;

#include "ir_private.h"


/* IrInternLocal / IrBind / IrFree* → ir_compile.c */
void IrFreeFunctionBody(void *BodyIr);
IrStmt *IrCompileBody(struct ParseState *BodyStart, struct ParseState *BodyEnd);

/* Release loop-owned IR / token span copies (not function BodyIr). */
static void IrLoopRelease(struct ParseState *Parser, IrLoop *Loop)
{
    if (Loop == NULL)
        return;
    if (Loop->BodyIr != NULL)
    {
        IrFreeFunctionBody(Loop->BodyIr);
        Loop->BodyIr = NULL;
    }
    if (Loop->BodyTokens != NULL)
    {
        HeapFreeMem(Parser->pc, Loop->BodyTokens);
        Loop->BodyTokens = NULL;
    }
}
void IrCompileSetParamContext(char **ParamNames, struct ValueType **ParamTypes, int NumParams);
void IrCompileClearParamContext(void);
void IrCompileSetReturnType(struct ValueType *RT);
void *IrCompileFunctionBody(struct ParseState *BodyStart);
void FastForDoInc(struct ParseState *Parser, FastForInc *Inc);

/* IR compile/run stats — only when requested (BABYC_IR_STATS=1 or --ir-stats).
 * Default off so regression suite (.expect) stays clean on all platforms. */
void IrPrintFpStats(void); /* ir_exec.c */
void IrPrintGlobalBindStats(void); /* ir_exec.c */
void IrPrintLowerStats(void);
void IrPrintVmStats(void);

void PicocPrintIrStats(void)
{
    const char *Env = getenv("BABYC_IR_STATS");
    const char *Fp = getenv("BABYC_FP_STATS");
    int want_ir = (Env != NULL && Env[0] != '\0' && !(Env[0] == '0' && Env[1] == '\0'));
    int want_fp = (Fp != NULL && Fp[0] != '\0' && !(Fp[0] == '0' && Fp[1] == '\0'));
    if (want_ir)
    {
        int t = PicocIrBodyCompiled + PicocIrBodyTokenFallback;
        if (t > 0)
            fprintf(stderr, "[babyc-ir] functions: ir=%d token=%d (%.0f%% ir)\n",
                    PicocIrBodyCompiled, PicocIrBodyTokenFallback,
                    100.0 * PicocIrBodyCompiled / t);
    }
    if (want_ir || want_fp)
        IrPrintFpStats();
    IrPrintGlobalBindStats();
    IrPrintLowerStats();
    IrPrintVmStats();
}


/* BABYC_NO_IR: disable Body-IR for function bodies AND loop bodies.
 * Cached once. DebugMode also forces token path. Micro always on. */
static int BabycNoIr(void)
{
#ifdef BABYC_MICRO
    return 1;
#else
    /* PATH=token (legacy BABYC_NO_IR mapped in BabycExecConfigInit). */
    return BabycPathGet() == BABYC_PATH_TOKEN;
#endif
}

static struct CleanupParseValueNode *ParseTrackPendingValue(Picoc *pc, struct Value *Value)
{
    struct CleanupParseValueNode *Node = HeapAllocMem(pc, sizeof(*Node));
    if (Node == NULL)
        return NULL;
    Node->Value = Value;
    Node->Next = pc->CleanupParseValueList;
    pc->CleanupParseValueList = Node;
    return Node;
}

static void ParseUntrackPendingValue(Picoc *pc, struct CleanupParseValueNode *Node)
{
    struct CleanupParseValueNode **Link = &pc->CleanupParseValueList;
    while (*Link != NULL && *Link != Node)
        Link = &(*Link)->Next;
    if (*Link == Node)
    {
        *Link = Node->Next;
        HeapFreeMem(pc, Node);
    }
}

void ParseCleanup(Picoc *pc)
{
    while (pc->CleanupParseValueList != NULL)
    {
        struct CleanupParseValueNode *Node = pc->CleanupParseValueList;
        pc->CleanupParseValueList = Node->Next;
        if (Node->Value != NULL)
            VariableFree(pc, Node->Value);
        HeapFreeMem(pc, Node);
    }
    while (pc->CleanupTokenList != NULL)
    {
        struct CleanupTokenNode *Next = pc->CleanupTokenList->Next;
        
        HeapFreeMem(pc, pc->CleanupTokenList->Tokens);
        if (pc->CleanupTokenList->SourceText != NULL)
            HeapFreeMem(pc, (void *)pc->CleanupTokenList->SourceText);
            
        HeapFreeMem(pc, pc->CleanupTokenList);
        pc->CleanupTokenList = Next;
    }
}

/* IrExecuteFunction moved to ir_exec.c */

/* parse a statement, but only run it if Condition is TRUE */

/* Fase 3b: skip one non-control statement at token depth (RunModeSkip). */
static int ParseSkipStatementFast(struct ParseState *Parser, int CheckTrailingSemicolon)
{
    enum LexToken Tok;
    int Paren = 0, Brace = 0, Bracket = 0;
    (void)CheckTrailingSemicolon;
    if (Parser->DebugMode)
        return 0;
    Tok = LexGetToken(Parser, NULL, FALSE);
    if (Tok == TokenIf || Tok == TokenWhile || Tok == TokenFor || Tok == TokenDo
        || Tok == TokenSwitch || Tok == TokenElse || Tok == TokenCase
        || Tok == TokenDefault)
        return 0;
    if (Tok == TokenLeftBrace)
    {
        LexGetToken(Parser, NULL, TRUE);
        Brace = 1;
        while (Brace > 0)
        {
            Tok = LexGetToken(Parser, NULL, TRUE);
            if (Tok == TokenEOF)
                return 1;
            if (Tok == TokenLeftBrace) Brace++;
            else if (Tok == TokenRightBrace) Brace--;
        }
        return 1;
    }
    for (;;)
    {
        Tok = LexGetToken(Parser, NULL, TRUE);
        if (Tok == TokenEOF)
            return 1;
        if (Tok == TokenOpenBracket) Paren++;
        else if (Tok == TokenCloseBracket) { if (Paren > 0) Paren--; }
        else if (Tok == TokenLeftBrace) Brace++;
        else if (Tok == TokenRightBrace)
        {
            if (Brace > 0) Brace--;
            else
                return 0;
        }
        else if (Tok == TokenLeftSquareBracket) Bracket++;
        else if (Tok == TokenRightSquareBracket) { if (Bracket > 0) Bracket--; }
        else if (Tok == TokenSemicolon && Paren == 0 && Brace == 0 && Bracket == 0)
            return 1;
    }
}

enum ParseResult ParseStatementMaybeRun(struct ParseState *Parser, int Condition, int CheckTrailingSemicolon)
{
    if (Parser->Mode != RunModeSkip && !Condition)
    {
        enum RunMode OldMode = Parser->Mode;
        int Result;
        Parser->Mode = RunModeSkip;
        /* Fase 3b: token-depth skip when not debugging */
        if (!Parser->DebugMode && OldMode != RunModeCaseSearch
            && OldMode != RunModeGoto)
        {
            enum LexToken Peek = LexGetToken(Parser, NULL, FALSE);
            /* Control keywords: full parse (handles else pairing, etc.) */
            if (Peek == TokenIf || Peek == TokenWhile || Peek == TokenFor
                || Peek == TokenDo || Peek == TokenSwitch || Peek == TokenElse)
                Result = ParseStatement(Parser, CheckTrailingSemicolon);
            else if (ParseSkipStatementFast(Parser, CheckTrailingSemicolon))
                Result = ParseResultOk;
            else
                Result = ParseStatement(Parser, CheckTrailingSemicolon);
        }
        else
            Result = ParseStatement(Parser, CheckTrailingSemicolon);
        Parser->Mode = OldMode;
        return Result;
    }
    else
        return ParseStatement(Parser, CheckTrailingSemicolon);
}

/* count the number of parameters to a function or macro.
 * Must track paren depth: int f(int (*fp)(int), int x) has an inner ')'
 * that is NOT the end of the parameter list. */
int ParseCountParams(struct ParseState *Parser)
{
    int ParamCount = 0;
    int Depth = 1; /* already inside the opening '(' of the param list */
    enum LexToken Token;

    Token = LexGetToken(Parser, NULL, FALSE);
    if (Token == TokenCloseBracket || Token == TokenEOF)
    {
        LexGetToken(Parser, NULL, TRUE); /* consume ')' */
        return 0;
    }

    ParamCount = 1;
    while (Depth > 0 && (Token = LexGetToken(Parser, NULL, TRUE)) != TokenEOF)
    {
        if (Token == TokenOpenBracket)
            Depth++;
        else if (Token == TokenCloseBracket)
        {
            Depth--;
            if (Depth == 0)
                break;
        }
        else if (Token == TokenComma && Depth == 1)
            ParamCount++;
    }
    return ParamCount;
}

/* parse a function definition and store it for later */
struct Value *ParseFunctionDefinition(struct ParseState *Parser, struct ValueType *ReturnType, char *Identifier)
{
    struct ValueType *ParamType;
    char *ParamIdentifier;
    enum LexToken Token = TokenNone;
    struct ParseState ParamParser;
    struct Value *FuncValue;
    struct Value *OldFuncValue;
    struct CleanupParseValueNode *PendingFuncNode;
    int ParamCount = 0;
    Picoc *pc = Parser->pc;

    if (pc->TopStackFrame != NULL)
        ProgramFail(Parser, "nested function definitions are not allowed");
        
    LexGetToken(Parser, NULL, TRUE);  /* open bracket */
    ParserCopy(&ParamParser, Parser);
    ParamCount = ParseCountParams(Parser);
    if (ParamCount > PARAMETER_MAX)
        ProgramFail(Parser, "too many parameters (%d allowed)", PARAMETER_MAX);
    
    FuncValue = VariableAllocValueAndData(pc, Parser, sizeof(struct FuncDef) + sizeof(struct ValueType *) * ParamCount + sizeof(const char *) * ParamCount, FALSE, NULL, TRUE);
    PendingFuncNode = ParseTrackPendingValue(pc, FuncValue);
    if (PendingFuncNode == NULL)
    {
        VariableFree(pc, FuncValue);
        ProgramFail(Parser, DiagnosticMsgOutOfMemory);
    }
    FuncValue->Typ = &pc->FunctionType;
    FuncValue->IsLValue = TRUE; /* allow &func and stable function-pointer targets */
    FuncValue->Val->FuncDef.ReturnType = ReturnType;
    FuncValue->Val->FuncDef.NumParams = ParamCount;
    FuncValue->Val->FuncDef.VarArgs = FALSE;
    FuncValue->Val->FuncDef.ParamType = (struct ValueType **)((char *)FuncValue->Val + sizeof(struct FuncDef));
    FuncValue->Val->FuncDef.ParamName = (char **)((char *)FuncValue->Val->FuncDef.ParamType + sizeof(struct ValueType *) * ParamCount);
   
    for (ParamCount = 0; ParamCount < FuncValue->Val->FuncDef.NumParams; ParamCount++)
    { 
        /* harvest the parameters into the function definition */
        if (ParamCount == FuncValue->Val->FuncDef.NumParams-1 && LexGetToken(&ParamParser, NULL, FALSE) == TokenEllipsis)
        { 
            /* ellipsis at end */
            FuncValue->Val->FuncDef.NumParams--;
            FuncValue->Val->FuncDef.VarArgs = TRUE;
            break;
        }
        else
        { 
            /* add a parameter */
            TypeParse(&ParamParser, &ParamType, &ParamIdentifier, NULL);
            if (ParamType->Base == TypeVoid)
            {
                /* this isn't a real parameter at all - delete it */
                ParamCount--;
                FuncValue->Val->FuncDef.NumParams--;
            }
            else
            {
                /* C array-to-pointer decay on function parameters:
                 *   char *argv[]  -> char **
                 *   int a[10]     -> int *
                 * Without this, main(int, char *[]) cannot accept __argv (char**).
                 */
                if (ParamType->Base == TypeArray)
                    ParamType = TypeGetMatching(pc, Parser, ParamType->FromType,
                                               TypePointer, 0, pc->StrEmpty, TRUE);
                FuncValue->Val->FuncDef.ParamType[ParamCount] = ParamType;
                FuncValue->Val->FuncDef.ParamName[ParamCount] = ParamIdentifier;
            }
        }
        
        Token = LexGetToken(&ParamParser, NULL, TRUE);
        if (Token != TokenComma && ParamCount < FuncValue->Val->FuncDef.NumParams-1)
            ProgramFail(&ParamParser, "comma expected");
    }
    
    if (FuncValue->Val->FuncDef.NumParams != 0 && Token != TokenCloseBracket && Token != TokenComma && Token != TokenEllipsis)
        ProgramFail(&ParamParser, "bad parameter");
    
    if (strcmp(Identifier, "main") == 0)
    {
        /* make sure it's int main() */
        if ( FuncValue->Val->FuncDef.ReturnType != &pc->IntType &&
             FuncValue->Val->FuncDef.ReturnType != &pc->VoidType )
            ProgramFail(Parser, "main() should return an int or void");

        if (FuncValue->Val->FuncDef.NumParams != 0 &&
             (FuncValue->Val->FuncDef.NumParams != 2 || FuncValue->Val->FuncDef.ParamType[0] != &pc->IntType) )
            ProgramFail(Parser, "bad parameters to main()");
    }
    
    /* look for a function body */
    Token = LexGetToken(Parser, NULL, FALSE);
    if (Token == TokenSemicolon)
        LexGetToken(Parser, NULL, TRUE);    /* it's a prototype, absorb the trailing semicolon */
    else
    {
        /* it's a full function definition with a body */
        if (Token != TokenLeftBrace)
            ProgramFail(Parser, "bad function definition for '%s'", Identifier);

        /* Collect the function body by matching braces.
         * This is more robust than parsing statements in RunModeSkip:
         * complex expressions (pointer dereference + assignment, multi-level
         * pointers, etc.) can desynchronise the token stream in Skip mode,
         * causing the next function definition to be mis-parsed as a
         * declaration that requires a trailing semicolon. */
        {
            int BraceCount = 1;
            struct ParseState BodyStart;
            enum LexToken Tok;

            ParserCopy(&BodyStart, Parser);          /* position at the '{' */
            LexGetToken(Parser, NULL, TRUE);         /* consume the opening '{' */

            while (BraceCount > 0)
            {
                Tok = LexGetToken(Parser, NULL, TRUE);
                if (Tok == TokenEOF)
                {
                    /* Point caret at the opening '{' of the unclosed body */
                    ParserCopy(Parser, &BodyStart);
                    ProgramFail(Parser, "function body of '%s' not closed", Identifier);
                }
                if (Tok == TokenLeftBrace)
                    BraceCount++;
                else if (Tok == TokenRightBrace)
                    BraceCount--;
            }

            /* BodyStart .. current position (after the matching '}') is the body */
            FuncValue->Val->FuncDef.Body = BodyStart;
            FuncValue->Val->FuncDef.Body.Pos = LexCopyTokens(&BodyStart, Parser);
            FuncValue->Val->FuncDef.BodyIr = NULL;

            /* Structural fix for forward prototypes:
             * 1) Remove any prior prototype from the table.
             * 2) Publish this definition (BodyIr still NULL) under the name.
             * 3) Compile Body-IR so recursive calls resolve to *this* Value*.
             * 4) BodyIr is filled on the same FuncValue already in the table.
             * Never VariableFree a prototype after something may have cached it. */
            if (TableGet(&pc->GlobalTable, Identifier, &OldFuncValue, NULL, NULL, NULL))
            {
                if (OldFuncValue->Val->FuncDef.Body.Pos == NULL
                    && OldFuncValue->Val->FuncDef.BodyIr == NULL)
                {
                    VariableFree(pc, TableDelete(pc, &pc->GlobalTable, Identifier));
                }
                else
                    ProgramFail(Parser, "'%s' is already defined", Identifier);
            }
            if (!TableSet(pc, &pc->GlobalTable, Identifier, FuncValue,
                    (char *)Parser->FileName, Parser->Line, Parser->CharacterPos))
                ProgramFail(Parser, "'%s' is already defined", Identifier);
            ParseUntrackPendingValue(pc, PendingFuncNode);
            /* Durable name for debug/backtrace — never re-scan GlobalTable later */
            {
                IrFuncMeta *Meta = IrFuncMetaGet(FuncValue);
                if (Meta != NULL && Identifier != NULL)
                    Meta->Name = Identifier;
            }

            {
                struct ParseState Tmp;
                ParserCopy(&Tmp, &FuncValue->Val->FuncDef.Body);
                Tmp.Pos = FuncValue->Val->FuncDef.Body.Pos;
                IrCompileSetParamContext(FuncValue->Val->FuncDef.ParamName,
                                         FuncValue->Val->FuncDef.ParamType,
                                         FuncValue->Val->FuncDef.NumParams);
                IrCompileSetReturnType(FuncValue->Val->FuncDef.ReturnType);
                if (BabycNoIr() || Parser->DebugMode)
                    FuncValue->Val->FuncDef.BodyIr = NULL; /* BABYC_NO_IR / debug: token only */
                else
                    FuncValue->Val->FuncDef.BodyIr = IrCompileFunctionBody(&Tmp);
                IrCompileClearParamContext();
                /* Compile-time Meta seed: TplReady + IsLeaf once, not on first call */
                if (FuncValue->Val->FuncDef.BodyIr != NULL)
                {
                    IrFuncMeta *Meta2 = IrFuncMetaGet(FuncValue);
                    if (Meta2 != NULL)
                    {
                        if (Identifier != NULL && Meta2->Name == NULL)
                            Meta2->Name = Identifier;
                        if (!Meta2->TplReady)
                            IrMetaSeedFromBody(Meta2, FuncValue->Val->FuncDef.BodyIr);
                        if (Meta2->IsLeaf < 0)
                        {
                            IrExpr *LE = NULL;
                            IrStmt *LS = NULL;
                            if (IrIsLeafReturn((IrStmt *)FuncValue->Val->FuncDef.BodyIr, &LE, &LS))
                            {
                                Meta2->IsLeaf = 1;
                                Meta2->LeafExpr = LE;
                                Meta2->LeafStmt = LS;
                            }
                            else
                                Meta2->IsLeaf = 0;
                        }
                    }
                }
                /* Struct/union return-by-value: supported when layout-ok
                 * (incl. bitfields). Runtime copies blob via IrStructRetBufIR. */
                BabycAnalyzeNote(Identifier,
                    FuncValue->Val->FuncDef.Body.FileName,
                    FuncValue->Val->FuncDef.Body.Line,
                    FuncValue->Val->FuncDef.BodyIr != NULL);
            }
            return FuncValue;
        }

        /* Prototype only: replace prior prototype if present */
        if (TableGet(&pc->GlobalTable, Identifier, &OldFuncValue, NULL, NULL, NULL))
        {
            if (OldFuncValue->Val->FuncDef.Body.Pos == NULL
                && OldFuncValue->Val->FuncDef.BodyIr == NULL)
            {
                VariableFree(pc, TableDelete(pc, &pc->GlobalTable, Identifier));
            }
            else
                ProgramFail(Parser, "'%s' is already defined", Identifier);
        }
    }

    if (!TableSet(pc, &pc->GlobalTable, Identifier, FuncValue, (char *)Parser->FileName, Parser->Line, Parser->CharacterPos))
        ProgramFail(Parser, "'%s' is already defined", Identifier);
    ParseUntrackPendingValue(pc, PendingFuncNode);
    /* Prototype / library path: same durable Meta->Name binding */
    {
        IrFuncMeta *Meta = IrFuncMetaGet(FuncValue);
        if (Meta != NULL && Identifier != NULL)
            Meta->Name = Identifier;
    }
        
    return FuncValue;
}


/* Initialise a struct from { expr, expr, ... } in member offset order.
 * Brace must already have been consumed by the caller. */
int ParseArrayInitialiser(struct ParseState *Parser, struct Value *NewVariable, int DoAssignment);

static int ParseStructInitialiser(struct ParseState *Parser, struct Value *StructValue, int DoAssignment)
{
    enum LexToken Token;
    struct Value *CValue;
    struct Value *MemberValue = NULL; (void)MemberValue;
    struct Table *Members;
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;
    int MemberCount = 0;
    int i, j;
    /* collect members then sort by offset (hash order is not declaration order) */
    struct Value *MemberList[128];
    int Offsets[128];
    char *MemberNames[128];

    if (StructValue->Typ->Base != TypeStruct && StructValue->Typ->Base != TypeUnion)
        AssignFail(Parser, "%t from array initializer", StructValue->Typ, NULL, 0, 0, NULL, 0);

    Members = StructValue->Typ->Members;
    if (Members == NULL)
        ProgramFail(Parser, "struct has no members");

    for (Count = 0; Count < Members->Size; Count++)
    {
        for (Entry = Members->HashTable[Count]; Entry != NULL; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            if (MemberCount >= 128)
                ProgramFail(Parser, "too many struct members for initializer");
            MemberList[MemberCount] = Entry->p.v.Val;
            Offsets[MemberCount] = Entry->p.v.Val->Val->Integer;
            MemberNames[MemberCount] = Entry->p.v.Key;
            MemberCount++;
        }
    }

    /* Sort members for initializer order:
     * - struct: by byte offset, then bit offset, then DeclIndex
     * - union: by DeclIndex only (C: first declared member gets the init)
     */
    for (i = 1; i < MemberCount; i++)
    {
        struct Value *mv = MemberList[i];
        int off = Offsets[i];
        char *nm = MemberNames[i];
        int bit = mv->BitOffset;
        int di = mv->DeclIndex;
        for (j = i; j > 0; j--)
        {
            int better;
            if (StructValue->Typ->Base == TypeUnion)
                better = (MemberList[j - 1]->DeclIndex > di);
            else
                better = (Offsets[j - 1] > off
                    || (Offsets[j - 1] == off && MemberList[j - 1]->BitOffset > bit)
                    || (Offsets[j - 1] == off && MemberList[j - 1]->BitOffset == bit
                        && MemberList[j - 1]->DeclIndex > di));
            if (!better)
                break;
            MemberList[j] = MemberList[j - 1];
            Offsets[j] = Offsets[j - 1];
            MemberNames[j] = MemberNames[j - 1];
        }
        MemberList[j] = mv;
        Offsets[j] = off;
        MemberNames[j] = nm;
    }

    /* C: a brace initializer initializes the whole object; omitted members are 0. */
    if (Parser->Mode == RunModeRun && DoAssignment && StructValue->Val != NULL)
    {
        int Z = TypeSizeValue(StructValue, FALSE);
        if (Z > 0)
            memset(StructValue->Val, 0, Z);
        StructValue->IsInitialized = TRUE;
    }

    i = 0;
    Token = LexGetToken(Parser, NULL, FALSE);
    /* Allow more than MemberCount tokens when using designators (re-init same
     * or later members). Loop until '}' rather than strict sequential count. */
    while (Token != TokenRightBrace)
    {
        struct Value *MemData = NULL;
        CValue = NULL;
        int mi = i; /* sequential target member index */

        /* C99 designated initializer: .member = expr  or  .member { ... } */
        if (Token == TokenDot)
        {
            struct Value *NameLex = NULL;
            int found = -1;
            int k;
            LexGetToken(Parser, NULL, TRUE); /* consume '.' */
            if (LexGetToken(Parser, &NameLex, TRUE) != TokenIdentifier
                || NameLex == NULL || NameLex->Val == NULL
                || NameLex->Val->Identifier == NULL)
                ProgramFail(Parser, "member name expected after '.'");
            for (k = 0; k < MemberCount; k++)
            {
                if (MemberNames[k] != NULL
                    && strcmp(MemberNames[k], NameLex->Val->Identifier) == 0)
                {
                    found = k;
                    break;
                }
            }
            if (found < 0)
                ProgramFail(Parser, "unknown member '%s' in designated initializer",
                    NameLex->Val->Identifier);
            mi = found;
            Token = LexGetToken(Parser, NULL, FALSE);
            if (Token == TokenAssign)
            {
                LexGetToken(Parser, NULL, TRUE); /* consume '=' */
                Token = LexGetToken(Parser, NULL, FALSE);
            }
            /* After designator: next is value expression or nested brace */
        }

        if (mi < 0 || mi >= MemberCount)
            ProgramFail(Parser, "too many initializers");

        /* Nested brace: member is array/struct/union → recurse ({{1,2,3}}) */
        if (Token == TokenLeftBrace
            && MemberList[mi]->Typ != NULL
            && (MemberList[mi]->Typ->Base == TypeArray
                || MemberList[mi]->Typ->Base == TypeStruct
                || MemberList[mi]->Typ->Base == TypeUnion))
        {
            LexGetToken(Parser, NULL, TRUE); /* consume '{' */
            if (Parser->Mode == RunModeRun && DoAssignment)
            {
                MemData = VariableAllocValueFromExistingData(Parser, MemberList[mi]->Typ,
                    (union AnyValue *)((char *)StructValue->Val + Offsets[mi]), TRUE, StructValue);
                if (MemberList[mi]->Typ->Base == TypeArray)
                    ParseArrayInitialiser(Parser, MemData, DoAssignment);
                else
                    ParseStructInitialiser(Parser, MemData, DoAssignment);
                VariableStackPop(Parser, MemData);
            }
            else
            {
                /* skip nested initializer — must use MEMBER type, not parent.
                 * Passing the parent made MemberCount wrong (e.g. S1 has 2
                 * members while nested S0 init has 3 values) → '}' expected. */
                struct Value SkipMem;
                memset(&SkipMem, 0, sizeof(SkipMem));
                SkipMem.Typ = MemberList[mi]->Typ;
                if (MemberList[mi]->Typ->Base == TypeArray)
                    ParseArrayInitialiser(Parser, &SkipMem, FALSE);
                else
                    ParseStructInitialiser(Parser, &SkipMem, FALSE);
            }
        }
        else
        {
            if (!ExpressionParseEx(Parser, &CValue, 0))
                ProgramFail(Parser, "expression expected");

            if (Parser->Mode == RunModeRun && DoAssignment)
            {
                MemData = VariableAllocValueFromExistingData(Parser, MemberList[mi]->Typ,
                    (union AnyValue *)((char *)StructValue->Val + Offsets[mi]), TRUE, StructValue);
                if (MemberList[mi]->BitWidth > 0)
                {
                    MemData->BitOffset = MemberList[mi]->BitOffset;
                    MemData->BitWidth = MemberList[mi]->BitWidth;
                }
                ExpressionAssign(Parser, MemData, CValue, FALSE, NULL, 0, FALSE);
                VariableStackPop(Parser, MemData);
                VariableStackPop(Parser, CValue);
            }
            else if (Parser->Mode == RunModeRun && CValue != NULL)
            {
                VariableStackPop(Parser, CValue);
            }
        }

        /* Next sequential position is after the member we just initialized */
        i = mi + 1;
        Token = LexGetToken(Parser, NULL, FALSE);
        if (Token == TokenComma)
        {
            LexGetToken(Parser, NULL, TRUE);
            Token = LexGetToken(Parser, NULL, FALSE);
        }
        else if (Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
    }

    if (Token != TokenRightBrace)
        ProgramFail(Parser, "'}' expected");
    LexGetToken(Parser, NULL, TRUE); /* consume } */

    return i;
}

/* parse an array initialiser and assign to a variable */
int ParseArrayInitialiser(struct ParseState *Parser, struct Value *NewVariable, int DoAssignment)
{
    int ArrayIndex = 0;
    enum LexToken Token;
    struct Value *CValue;
    
    /* count the number of elements in the array */
    if (DoAssignment && Parser->Mode == RunModeRun)
    {
        struct ParseState CountParser;
        int NumElements;
        
        ParserCopy(&CountParser, Parser);
        NumElements = ParseArrayInitialiser(&CountParser, NewVariable, FALSE);

        if (NewVariable->Typ->Base != TypeArray)
            AssignFail(Parser, "%t from array initializer", NewVariable->Typ, NULL, 0, 0, NULL, 0);

        if (NewVariable->Typ->ArraySize == 0)
        {
            NewVariable->Typ = TypeGetMatching(Parser->pc, Parser, NewVariable->Typ->FromType, NewVariable->Typ->Base, NumElements, NewVariable->Typ->Identifier, TRUE);
            VariableRealloc(Parser, NewVariable, TypeSizeValue(NewVariable, FALSE));
        }
        {
            int Z = TypeSizeValue(NewVariable, FALSE);
            if (Z > 0 && NewVariable->Val != NULL)
                memset(NewVariable->Val, 0, Z);
            NewVariable->IsInitialized = TRUE;
        }
        #ifdef DEBUG_ARRAY_INITIALIZER
        PRINT_SOURCE_POS;
        printf("array size: %d \n", NewVariable->Typ->ArraySize);
        #endif
    }
    
    /* parse the array initialiser */
    Token = LexGetToken(Parser, NULL, FALSE);
    while (Token != TokenRightBrace)
    {
        if (LexGetToken(Parser, NULL, FALSE) == TokenLeftBrace)
        {
            /* nested { } : sub-array OR struct element */
            struct Value *SubElement;
            struct ValueType *ElemType = NewVariable->Typ->FromType;
            int ElemSize = 0;
            struct Value DummyElement;

            LexGetToken(Parser, NULL, TRUE); /* consume '{' */

            if (Parser->Mode == RunModeRun && DoAssignment)
            {
                if (NewVariable->Typ->ArraySize > 0 && ArrayIndex >= NewVariable->Typ->ArraySize)
                    ProgramFail(Parser, "too many array elements");
                ElemSize = TypeSize(ElemType, ElemType->ArraySize, TRUE);
                SubElement = VariableAllocValueFromExistingData(Parser, ElemType,
                    (union AnyValue *)((char *)NewVariable->Val + ElemSize * ArrayIndex), TRUE, NewVariable);
            }
            else
            {
                /* counting / skip: only need the element type for member list */
                memset(&DummyElement, 0, sizeof(DummyElement));
                DummyElement.Typ = ElemType;
                SubElement = &DummyElement;
            }

            if (ElemType->Base == TypeStruct || ElemType->Base == TypeUnion)
                ParseStructInitialiser(Parser, SubElement, DoAssignment);
            else
                ParseArrayInitialiser(Parser, SubElement, DoAssignment);

            /* SubElement was allocated on the interpreter stack (not DummyElement) */
            if (Parser->Mode == RunModeRun && DoAssignment)
                VariableStackPop(Parser, SubElement);
        }
        else
        {
            struct Value *ArrayElement = NULL;
        
            if (Parser->Mode == RunModeRun && DoAssignment)
            {
                struct ValueType * ElementType = NewVariable->Typ;
                int TotalSize = 1;
                int ElementSize = 0;
                
                /* int x[3][3] = {1,2,3,4} => handle it just like int x[9] = {1,2,3,4} */
                while (ElementType->Base == TypeArray)
                {
                    TotalSize *= ElementType->ArraySize;
                    ElementType = ElementType->FromType;
                    
                    /* char x[10][10] = {"abc", "def"} => assign "abc" to x[0], "def" to x[1] etc */
                    if (LexGetToken(Parser, NULL, FALSE) == TokenStringConstant && ElementType->FromType->Base == TypeChar)
                        break;
                }
                ElementSize = TypeSize(ElementType, ElementType->ArraySize, TRUE);
                #ifdef DEBUG_ARRAY_INITIALIZER
                PRINT_SOURCE_POS;
                printf("[%d/%d] element size: %d (x%d) \n", ArrayIndex, TotalSize, ElementSize, ElementType->ArraySize);
                #endif
                if (ArrayIndex >= TotalSize)
                    ProgramFail(Parser, "too many array elements");
                ArrayElement = VariableAllocValueFromExistingData(Parser, ElementType, (union AnyValue *)((char *)NewVariable->Val + ElementSize * ArrayIndex), TRUE, NewVariable);
            }

            /* assignment-expression (commas separate elements, not comma-op) */
            if (!ExpressionParseEx(Parser, &CValue, 0))
                ProgramFail(Parser, "expression expected");

            if (Parser->Mode == RunModeRun && DoAssignment)
            {
                ExpressionAssign(Parser, ArrayElement, CValue, FALSE, NULL, 0, FALSE);
                /* CValue is on top (allocated after ArrayElement) */
                VariableStackPop(Parser, CValue);
                VariableStackPop(Parser, ArrayElement);
            }
            else if (Parser->Mode == RunModeRun && CValue != NULL)
            {
                /* counting pass (DoAssignment=FALSE, still RunModeRun): free expression value.
                 * RunModeSkip: ExpressionParse already discarded the stack — CValue is unset. */
                VariableStackPop(Parser, CValue);
            }
        }
        
        ArrayIndex++;

        Token = LexGetToken(Parser, NULL, FALSE);
        if (Token == TokenComma)
        {
            LexGetToken(Parser, NULL, TRUE);
            Token = LexGetToken(Parser, NULL, FALSE);
        }   
        else if (Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
    }
    
    if (Token == TokenRightBrace)
        LexGetToken(Parser, NULL, TRUE);
    else
        ProgramFail(Parser, "'}' expected");
    
    return ArrayIndex;
}

/* assign an initial value to a variable */
void ParseDeclarationAssignment(struct ParseState *Parser, struct Value *NewVariable, int DoAssignment)
{
    struct Value *CValue;

    if (NewVariable == NULL || NewVariable->Typ == NULL)
        ProgramFail(Parser, "invalid declaration assignment");

    if (LexGetToken(Parser, NULL, FALSE) == TokenLeftBrace)
    {
        LexGetToken(Parser, NULL, TRUE);
        if (NewVariable->Typ->Base == TypeStruct || NewVariable->Typ->Base == TypeUnion)
            ParseStructInitialiser(Parser, NewVariable, DoAssignment);
        else
            ParseArrayInitialiser(Parser, NewVariable, DoAssignment);
    }
    else
    {
        /* assignment-expression only (no top-level comma — multi-decl uses ',') */
        if (!ExpressionParseEx(Parser, &CValue, 0))
            ProgramFail(Parser, "expression expected");
            
        if (Parser->Mode == RunModeRun)
        {
            if (DoAssignment)
            {
                ExpressionAssign(Parser, NewVariable, CValue, FALSE, NULL, 0, FALSE);
                NewVariable->IsInitialized = TRUE;
            }
            if (CValue != NULL)
                VariableStackPop(Parser, CValue);
        }
        /* RunModeSkip: ExpressionParse already discarded the expression stack;
         * CValue is not set — do not VariableStackPop it. */
    }
}

/* declare a variable or function */
int ParseDeclaration(struct ParseState *Parser, enum LexToken Token)
{
    char *Identifier;
    struct ValueType *BasicType;
    struct ValueType *Typ;
    struct Value *NewVariable = NULL;
    int IsStatic = FALSE;
    int FirstVisit = FALSE;
    Picoc *pc = Parser->pc;

    TypeParseFront(Parser, &BasicType, &IsStatic);
    do
    {
        TypeParseIdentPart(Parser, BasicType, &Typ, &Identifier);
        if ((Token != TokenVoidType && Token != TokenStructType && Token != TokenUnionType && Token != TokenEnumType) && Identifier == pc->StrEmpty)
            ProgramFail(Parser, "identifier expected");
            
        if (Identifier != pc->StrEmpty)
        {
            /* handle function definitions */
            if (LexGetToken(Parser, NULL, FALSE) == TokenOpenBracket)
            {
                ParseFunctionDefinition(Parser, Typ, Identifier);
                return FALSE;
            }
            else
            {
                if (Typ == &pc->VoidType && Identifier != pc->StrEmpty)
                    ProgramFail(Parser, "can't define a void variable");
                    
                if (Parser->Mode == RunModeRun || Parser->Mode == RunModeGoto)
                    NewVariable = VariableDefineButIgnoreIdentical(Parser, Identifier, Typ, IsStatic, &FirstVisit);
                
                if (LexGetToken(Parser, NULL, FALSE) == TokenAssign)
                {
                    /* we're assigning an initial value */
                    LexGetToken(Parser, NULL, TRUE);
                    if (NewVariable != NULL)
                    {
                        ParseDeclarationAssignment(Parser, NewVariable, !IsStatic || FirstVisit);
                    }
                    else
                    {
                        /* RunModeSkip (and similar): NewVariable was not defined, but we
                         * must still parse/consume the initialiser. Use a stack dummy with
                         * a valid Typ — same pattern as DummyElement in ParseArrayInitialiser.
                         * DoAssignment=FALSE so nothing is written. */
                        struct Value SkipDummy;
                        memset(&SkipDummy, 0, sizeof(SkipDummy));
                        SkipDummy.Typ = Typ;
                        ParseDeclarationAssignment(Parser, &SkipDummy, FALSE);
                    }
                }
                /* Qualifiers apply after the initializer so `const int x = 1` works. */
                if (NewVariable != NULL)
                {
                    if (Parser->DeclIsConst)
                        NewVariable->IsConst = TRUE;
                    if (Parser->DeclIsVolatile)
                        NewVariable->IsVolatile = TRUE;
                }
            }
        }
        
        Token = LexGetToken(Parser, NULL, FALSE);
        if (Token == TokenComma)
            LexGetToken(Parser, NULL, TRUE);
            
    } while (Token == TokenComma);
    
    return TRUE;
}

/* parse a #define macro definition and store it for later */
void ParseMacroDefinition(struct ParseState *Parser)
{
    struct Value *MacroName;
    char *MacroNameStr;
    struct Value *ParamName;
    struct Value *MacroValue;

    if (LexGetToken(Parser, &MacroName, TRUE) != TokenIdentifier)
        ProgramFail(Parser, "identifier expected");
    
    MacroNameStr = MacroName->Val->Identifier;
    
    if (LexRawPeekToken(Parser) == TokenOpenMacroBracket)
    {
        /* it's a parameterised macro, read the parameters */
        enum LexToken Token = LexGetToken(Parser, NULL, TRUE);
        struct ParseState ParamParser;
        int NumParams;
        int ParamCount = 0;
        
        ParserCopy(&ParamParser, Parser);
        NumParams = ParseCountParams(&ParamParser);
        MacroValue = VariableAllocValueAndData(Parser->pc, Parser, sizeof(struct MacroDef) + sizeof(const char *) * NumParams, FALSE, NULL, TRUE);
        MacroValue->Val->MacroDef.NumParams = NumParams;
        MacroValue->Val->MacroDef.ParamName = (char **)((char *)MacroValue->Val + sizeof(struct MacroDef));

        Token = LexGetToken(Parser, &ParamName, TRUE);
        
        while (Token == TokenIdentifier)
        {
            /* store a parameter name */
            MacroValue->Val->MacroDef.ParamName[ParamCount++] = ParamName->Val->Identifier;
            
            /* get the trailing comma */
            Token = LexGetToken(Parser, NULL, TRUE);
            if (Token == TokenComma)
                Token = LexGetToken(Parser, &ParamName, TRUE);
                
            else if (Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        
        if (Token != TokenCloseBracket)
            ProgramFail(Parser, "close bracket expected");
    }
    else
    {
        /* allocate a simple unparameterised macro */
        MacroValue = VariableAllocValueAndData(Parser->pc, Parser, sizeof(struct MacroDef), FALSE, NULL, TRUE);
        MacroValue->Val->MacroDef.NumParams = 0;
    }
    
    /* copy the body of the macro to execute later */
    ParserCopy(&MacroValue->Val->MacroDef.Body, Parser);
    MacroValue->Typ = &Parser->pc->MacroType;
    LexToEndOfLine(Parser);
    MacroValue->Val->MacroDef.Body.Pos = LexCopyTokens(&MacroValue->Val->MacroDef.Body, Parser);
    
    if (!TableSet(Parser->pc, &Parser->pc->GlobalTable, MacroNameStr, MacroValue, (char *)Parser->FileName, Parser->Line, Parser->CharacterPos))
        ProgramFail(Parser, "'%s' is already defined", MacroNameStr);
}

/* copy the entire parser state */
#ifdef BABYC_LEX_STATS
static unsigned long long ParserCopyStat;
unsigned long long ParserCopyStatGet(void) { return ParserCopyStat; }
#else
unsigned long long ParserCopyStatGet(void) { return 0; }
#endif

void ParserCopy(struct ParseState *To, struct ParseState *From)
{
#ifdef BABYC_LEX_STATS
    ParserCopyStat++;
#endif
    memcpy((void *)To, (void *)From, sizeof(*To));
}

/* copy where we're at in the parsing */
void ParserCopyPos(struct ParseState *To, struct ParseState *From)
{
    To->Pos = From->Pos;
    To->Line = From->Line;
    To->HashIfLevel = From->HashIfLevel;
    To->HashIfEvaluateToLevel = From->HashIfEvaluateToLevel;
    To->CharacterPos = From->CharacterPos;
    To->TokenIndex = From->TokenIndex;
}

/* Copy token span for loop re-execution. Ends with TokenEOF (NOT TokenEndOfFunction)
 * so re-running a for-body inside a function does not look like end-of-function.
 * Function bodies keep LexCopyTokens → TokenEndOfFunction. */
static void *ParseCopyTokenSpan(struct ParseState *Start, struct ParseState *End)
{
    int MemSize;
    unsigned char *NewTokens;
    if (End->Pos <= Start->Pos)
        return NULL;
    MemSize = (int)((char *)End->Pos - (char *)Start->Pos);
    NewTokens = HeapAllocMem(Start->pc, MemSize + 2);
    if (NewTokens == NULL)
        ProgramFail(Start, DiagnosticMsgOutOfMemory);
    memcpy(NewTokens, (void *)Start->Pos, MemSize);
    NewTokens[MemSize] = (unsigned char)TokenEOF;
    NewTokens[MemSize + 1] = 0;
    return NewTokens;
}

/* ========== IR build / execute ========== */

static struct ParseState *IrAllocParserPos = NULL;

/* Forward decls for IR eval/call */
/* IrCallUserFunc / Cached: defined in ir_exec.c, declared in ir_runtime.h */

/* Same as IrCallUserFunc but FuncValue already resolved (no table lookup). */



/* Loop / mono / eval: implementations in ir_exec.c */
/* Loop executor in ir_exec.c */
void IrExecuteLoop(struct ParseState *Parser, IrLoop *Loop, enum RunMode PreMode);
void IrCondFromFast(IrCond *Dst, FastForCond *Src);
void IrIncFromFast(IrInc *Dst, FastForInc *Src);
void LoopRunBody(struct ParseState *Parser, IrLoop *Loop, int CheckTrailingSemicolon);
int IrTryTightLoopVars(struct ParseState *Parser, IrLoop *Loop);
int TryStripTrailingInc(struct ParseState *BodyStart, struct ParseState *BodyEnd,
                        const char *VarName, FastForInc *Inc,
                        struct ParseState *NewBodyStart, struct ParseState *NewBodyEnd);

void FastForDoInc(struct ParseState *Parser, FastForInc *Inc)
{
    struct Value *Var;
    PTR_TYPE_INT_SIGNED V;
    long long LV;

    if (IrFrameGetByName(Inc->VarName, &LV) == 1)
    {
        IrFrameSetByName(Inc->VarName, LV + 1);
        return;
    }
    VariableGet(Parser->pc, Parser, Inc->VarName, &Var);
    V = ExpressionCoerceInteger(Var);
    ExpressionAssignInt(Parser, Var, V + 1, FALSE);
}

/* Irb retired (PIANO_TOKEN_PATH_V2 P1) */
int BabycGotoMatchCount;
void ParseFor(struct ParseState *Parser)
{
    PTR_TYPE_INT_SIGNED Condition;
    struct ParseState PreConditional;
    struct ParseState CondCapture;
    struct ParseState PreIncrement;
    struct ParseState IncCapture;
    struct ParseState PreStatement;
    struct ParseState BodyStart;
    struct ParseState BodyEnd;
    struct ParseState After;
    FastForCond FastCond;
    FastForInc FastInc;
    /* BodyTokens via IrLoop */
    
    enum RunMode OldMode = Parser->Mode;
    
    int PrevScopeID = 0, ScopeID = VariableScopeBegin(Parser, &PrevScopeID);

    FastCond.Active = 0; FastCond.CachedVar = NULL; FastCond.CachedBound = NULL;
    FastInc.Active = 0; FastInc.CachedVar = NULL;

    if (LexGetToken(Parser, NULL, TRUE) != TokenOpenBracket)
        ProgramFail(Parser, "'(' expected");
                        
    if (ParseStatement(Parser, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");
    
    ParserCopyPos(&PreConditional, Parser);
    ParserCopy(&CondCapture, Parser);
    if (LexGetToken(Parser, NULL, FALSE) == TokenSemicolon)
        Condition = TRUE;
    else
        Condition = ExpressionParseInt(Parser);
    
    if (LexGetToken(Parser, NULL, TRUE) != TokenSemicolon)
        ProgramFail(Parser, "';' expected");
    
    ParserCopyPos(&PreIncrement, Parser);
    ParserCopy(&IncCapture, Parser);
    /* for-increment is an expression (may use comma); parse under Skip so we
     * only advance the token position without executing side effects. */
    if (LexGetToken(Parser, NULL, FALSE) != TokenCloseBracket)
    {
        struct Value *IncVal = NULL;
        enum RunMode SavedIncMode = Parser->Mode;
        if (SavedIncMode == RunModeRun)
            Parser->Mode = RunModeSkip;
        if (!ExpressionParse(Parser, &IncVal))
            ProgramFail(Parser, "expression expected");
        if (IncVal != NULL && Parser->Mode == RunModeRun)
            VariableStackPop(Parser, IncVal);
        Parser->Mode = SavedIncMode;
    }
    
    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
        ProgramFail(Parser, "')' expected");
    
    ParserCopyPos(&PreStatement, Parser);
    ParserCopy(&BodyStart, Parser);
    if (ParseStatementMaybeRun(Parser, Condition ? TRUE : FALSE, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");
    
    if (Parser->Mode == RunModeContinue && OldMode == RunModeRun)
        Parser->Mode = RunModeRun;
        
    ParserCopyPos(&After, Parser);
    ParserCopy(&BodyEnd, Parser);

    /* goto/return from first body: do not continue the for */
    if (Parser->Mode == RunModeGoto || Parser->Mode == RunModeReturn)
    {
        VariableScopeEnd(Parser, ScopeID, PrevScopeID);
        /* leave After as end of body; caller continues (goto search / return) */
        return;
    }

    /* Capture fast cond/inc, optional IR body, token fallback */
    {
        IrLoop Loop;
        memset(&Loop, 0, sizeof(Loop));
        Loop.Kind = IrLoopFor;
        ParserCopy(&Loop.BodyTemplate, &BodyStart);
        ParserCopyPos(&Loop.CondFrom, &PreConditional);
        Loop.CondNeedsCloseBracket = 0;

        if (OldMode == RunModeRun && Condition && Parser->Mode == RunModeRun)
        {
            FastForCaptureCond(&CondCapture, &FastCond);
            IrCondFromFast(&Loop.Cond, &FastCond);
            if (FastCond.Active)
            {
                FastForCaptureInc(&IncCapture, &FastInc);
                /* Comma in for-increment (a++, b+=2): FastInc only tracks one var */
                {
                    struct ParseState Scan;
                    enum LexToken Tk;
                    ParserCopy(&Scan, &IncCapture);
                    while ((Tk = LexGetToken(&Scan, NULL, TRUE)) != TokenCloseBracket
                           && Tk != TokenEOF && Tk != TokenEndOfFunction)
                    {
                        if (Tk == TokenComma)
                        {
                            FastInc.Active = 0;
                            break;
                        }
                    }
                }
                IrIncFromFast(&Loop.Inc, &FastInc);
            }
            Loop.BodyIr = IrCompileBody(&BodyStart, &BodyEnd);
            /* BABYC_NO_IR / debug: keep token body (no loop BodyIr) */
            if (BabycNoIr() || Parser->DebugMode)
                Loop.BodyIr = NULL;
            if (Loop.BodyIr == NULL && BodyEnd.Pos > BodyStart.Pos
                && !BabycNoIr() && !Parser->DebugMode)
                Loop.BodyTokens = ParseCopyTokenSpan(&BodyStart, &BodyEnd);
        }

        /* Prefer tight / BodyIr executor. */
        if (IrTryTightLoopVars(Parser, &Loop))
            Condition = 0;
        else if (Condition && OldMode == RunModeRun && Parser->Mode == RunModeRun
                 && !BabycNoIr() && !Parser->DebugMode
#ifndef BABYC_MICRO
                 && (Loop.BodyIr != NULL || Loop.BodyTokens != NULL)
#else
                 && (Loop.BodyIr != NULL)
#endif
                 && (Loop.Inc.Active || FastInc.Active))
        {
            /* Unified executor only when for-inc is Fast (a++). Complex
             * inc (a++, b+=2) must use the token re-parse fallback. */
            if (!Loop.Cond.Active && FastCond.Active)
                IrCondFromFast(&Loop.Cond, &FastCond);
            if (!Loop.Inc.Active && FastInc.Active)
                IrIncFromFast(&Loop.Inc, &FastInc);
            if (!Loop.Cond.Active)
            {
                ParserCopyPos(&Loop.CondFrom, &PreConditional);
                Loop.CondNeedsCloseBracket = 0;
            }
            IrExecuteLoop(Parser, &Loop, OldMode);
            Condition = 0;
        }
        else while (Condition && Parser->Mode == RunModeRun)
        {
            /* Fallback: non-Fast for-inc expression must be re-parsed */
            {
                struct Value *IncVal = NULL;
                ParserCopyPos(Parser, &PreIncrement);
                if (LexGetToken(Parser, NULL, FALSE) != TokenCloseBracket
                    && LexGetToken(Parser, NULL, FALSE) != TokenSemicolon)
                {
                    if (ExpressionParse(Parser, &IncVal) && IncVal != NULL
                        && Parser->Mode == RunModeRun)
                        VariableStackPop(Parser, IncVal);
                }
            }
            if (Loop.Cond.Active)
                Condition = FastForEvalCond(Parser, (FastForCond *)&Loop.Cond);
            else if (FastCond.Active)
                Condition = FastForEvalCond(Parser, &FastCond);
            else
            {
                ParserCopyPos(Parser, &PreConditional);
                if (LexGetToken(Parser, NULL, FALSE) == TokenSemicolon)
                    Condition = TRUE;
                else
                    Condition = ExpressionParseInt(Parser);
            }
            if (Condition)
            {
#ifndef BABYC_MICRO
                if (Loop.BodyIr != NULL || Loop.BodyTokens != NULL)
                    LoopRunBody(Parser, &Loop, TRUE);
                else
#endif
                {
                    ParserCopyPos(Parser, &PreStatement);
                    if (ParseStatement(Parser, TRUE) != ParseResultOk)
                        ProgramFail(Parser, "statement expected");
                }
                if (Parser->Mode == RunModeContinue)
                    Parser->Mode = RunModeRun;
            }
        }
        IrLoopRelease(Parser, &Loop);
    }
    
    if (Parser->Mode == RunModeBreak && OldMode == RunModeRun)
        Parser->Mode = RunModeRun;

    VariableScopeEnd(Parser, ScopeID, PrevScopeID);

    ParserCopyPos(Parser, &After);
}

/* while: shared LoopRunBody / LoopEvalCond (executor-oriented) */
void ParseWhile(struct ParseState *Parser)
{
    PTR_TYPE_INT_SIGNED Condition;
    struct ParseState CondCapture;
    struct ParseState BodyStart;
    struct ParseState BodyEnd;
    struct ParseState PreConditional;
    FastForCond FastCond;
    IrLoop Loop;
    enum RunMode PreMode = Parser->Mode;

    memset(&FastCond, 0, sizeof(FastCond));
    memset(&Loop, 0, sizeof(Loop));

    if (LexGetToken(Parser, NULL, TRUE) != TokenOpenBracket)
        ProgramFail(Parser, "'(' expected");

    ParserCopyPos(&PreConditional, Parser);
    ParserCopy(&CondCapture, Parser);
    Condition = ExpressionParseInt(Parser);
    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
        ProgramFail(Parser, "')' expected");

    ParserCopy(&BodyStart, Parser);
    if (ParseStatementMaybeRun(Parser, Condition ? TRUE : FALSE, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");

    if (Parser->Mode == RunModeContinue)
        Parser->Mode = PreMode;
    ParserCopy(&BodyEnd, Parser);

    Loop.Kind = IrLoopWhile;
    Loop.BodyTokens = NULL;
    ParserCopy(&Loop.BodyTemplate, &BodyStart);
    ParserCopyPos(&Loop.CondFrom, &PreConditional);
    Loop.CondNeedsCloseBracket = 1;
    Loop.Cond.Active = 0;

    if (PreMode == RunModeRun && Parser->Mode == RunModeRun && Condition)
    {
        struct ParseState SpanStart, SpanEnd;
        FastForInc FastInc;

        FastInc.Active = 0; FastInc.CachedVar = NULL;
        FastForCaptureCond(&CondCapture, &FastCond);
        IrCondFromFast(&Loop.Cond, &FastCond);
        Loop.CondNeedsCloseBracket = !FastCond.Active;
        ParserCopy(&SpanStart, &BodyStart);
        ParserCopy(&SpanEnd, &BodyEnd);
        if (FastCond.Active == 1 && FastCond.VarName != NULL)
        {
            if (TryStripTrailingInc(&BodyStart, &BodyEnd, FastCond.VarName, &FastInc, &SpanStart, &SpanEnd))
            {
                IrIncFromFast(&Loop.Inc, &FastInc);
                ParserCopy(&Loop.BodyTemplate, &SpanStart);
            }
        }
        if (FastInc.Active && SpanEnd.Pos <= SpanStart.Pos)
        {
            unsigned char *Empty = HeapAllocMem(Parser->pc, 2);
            if (Empty == NULL)
                ProgramFail(Parser, DiagnosticMsgOutOfMemory);
            Empty[0] = (unsigned char)TokenEOF;
            Empty[1] = 0;
            Loop.BodyTokens = Empty;
        }
        else if (SpanEnd.Pos > SpanStart.Pos)
            Loop.BodyTokens = ParseCopyTokenSpan(&SpanStart, &SpanEnd);
        else if (BodyEnd.Pos > BodyStart.Pos)
            Loop.BodyTokens = ParseCopyTokenSpan(&BodyStart, &BodyEnd);
        /* IR from the same span as tokens (post trailing-inc strip when active) */
        if (FastInc.Active)
            Loop.BodyIr = IrCompileBody(&SpanStart, &SpanEnd);
        else
            Loop.BodyIr = IrCompileBody(&BodyStart, &BodyEnd);
        /* BABYC_NO_IR / debug: keep token body (no loop BodyIr) */
        if (BabycNoIr() || Parser->DebugMode)
            Loop.BodyIr = NULL;
        if (Loop.BodyIr != NULL)
        {
            if (Loop.BodyTokens != NULL)
                HeapFreeMem(Parser->pc, Loop.BodyTokens);
            Loop.BodyTokens = NULL;
        }
        if (BabycNoIr() || Parser->DebugMode)
            Loop.BodyTokens = NULL;
        if (!IrTryTightLoopVars(Parser, &Loop))
        {
#ifdef BABYC_MICRO
            /* Token re-parse loop (no ir_exec). Condition already true once. */
            while (Condition && Parser->Mode == RunModeRun)
            {
                ParserCopyPos(Parser, &Loop.CondFrom);
                Condition = ExpressionParseInt(Parser);
                if (Loop.CondNeedsCloseBracket)
                {
                    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                        ProgramFail(Parser, "')' expected");
                }
                if (!Condition)
                    break;
                ParserCopyPos(Parser, &Loop.BodyTemplate);
                if (ParseStatement(Parser, TRUE) != ParseResultOk)
                    ProgramFail(Parser, "statement expected");
                if (Parser->Mode == RunModeContinue)
                    Parser->Mode = PreMode;
            }
#else
            IrExecuteLoop(Parser, &Loop, PreMode);
#endif
        }
    }

    /* break/continue on the first body (before IrExecuteLoop) must not leak */
    if (Parser->Mode == RunModeBreak || Parser->Mode == RunModeContinue)
        Parser->Mode = PreMode;

    IrLoopRelease(Parser, &Loop);
    ParserCopyPos(Parser, &BodyEnd);
}

/* do-while: body first, then condition (same executor primitives) */
void ParseDoWhile(struct ParseState *Parser)
{
    struct ParseState BodyStart;
    struct ParseState BodyEnd;
    struct ParseState CondCapture;
    struct ParseState DoAfter;
    FastForCond FastCond;
    IrLoop Loop;
    enum RunMode PreMode = Parser->Mode;

    memset(&FastCond, 0, sizeof(FastCond));
    memset(&Loop, 0, sizeof(Loop));

    ParserCopy(&BodyStart, Parser);
    if (ParseStatement(Parser, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");
    ParserCopy(&BodyEnd, Parser);

    if (Parser->Mode == RunModeContinue)
        Parser->Mode = PreMode;

    if (LexGetToken(Parser, NULL, TRUE) != TokenWhile)
        ProgramFail(Parser, "'while' expected");
    if (LexGetToken(Parser, NULL, TRUE) != TokenOpenBracket)
        ProgramFail(Parser, "'(' expected");

    ParserCopy(&CondCapture, Parser);
    /* Parse condition for syntax only — do NOT run it here (side effects belong
     * after the body). IrExecuteLoop will evaluate before each subsequent body. */
    {
        enum RunMode Saved = Parser->Mode;
        if (Parser->Mode == RunModeRun)
            Parser->Mode = RunModeSkip;
        ExpressionParseInt(Parser);
        Parser->Mode = Saved;
    }
    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
        ProgramFail(Parser, "')' expected");
    ParserCopyPos(&DoAfter, Parser);

    /* First body already executed; executor does: while (cond) body */
    Loop.Kind = IrLoopDo;
    Loop.BodyTokens = NULL;
    ParserCopy(&Loop.BodyTemplate, &BodyStart);
    ParserCopy(&Loop.CondFrom, &CondCapture);
    Loop.CondNeedsCloseBracket = 0;
    Loop.Cond.Active = 0;

    if (PreMode == RunModeRun && Parser->Mode == RunModeRun)
    {
        struct ParseState SpanStart, SpanEnd;
        FastForInc FastInc;

        FastInc.Active = 0; FastInc.CachedVar = NULL;
        FastForCaptureCond(&CondCapture, &FastCond);
        IrCondFromFast(&Loop.Cond, &FastCond);
        ParserCopy(&SpanStart, &BodyStart);
        ParserCopy(&SpanEnd, &BodyEnd);
        if (FastCond.Active == 1 && FastCond.VarName != NULL)
        {
            if (TryStripTrailingInc(&BodyStart, &BodyEnd, FastCond.VarName, &FastInc, &SpanStart, &SpanEnd))
            {
                IrIncFromFast(&Loop.Inc, &FastInc);
                ParserCopy(&Loop.BodyTemplate, &SpanStart);
            }
        }
        if (FastInc.Active && SpanEnd.Pos <= SpanStart.Pos)
        {
            unsigned char *Empty = HeapAllocMem(Parser->pc, 2);
            if (Empty == NULL)
                ProgramFail(Parser, DiagnosticMsgOutOfMemory);
            Empty[0] = (unsigned char)TokenEOF;
            Empty[1] = 0;
            Loop.BodyTokens = Empty;
        }
        else if (SpanEnd.Pos > SpanStart.Pos)
            Loop.BodyTokens = ParseCopyTokenSpan(&SpanStart, &SpanEnd);
        else if (BodyEnd.Pos > BodyStart.Pos)
            Loop.BodyTokens = ParseCopyTokenSpan(&BodyStart, &BodyEnd);
        if (FastInc.Active)
            Loop.BodyIr = IrCompileBody(&SpanStart, &SpanEnd);
        else
            Loop.BodyIr = IrCompileBody(&BodyStart, &BodyEnd);
        /* BABYC_NO_IR / debug: keep token body (no loop BodyIr) */
        if (BabycNoIr() || Parser->DebugMode)
            Loop.BodyIr = NULL;
        if (Loop.BodyIr != NULL)
        {
            if (Loop.BodyTokens != NULL)
                HeapFreeMem(Parser->pc, Loop.BodyTokens);
            Loop.BodyTokens = NULL;
        }
        if (BabycNoIr() || Parser->DebugMode)
            Loop.BodyTokens = NULL;
        if (!IrTryTightLoopVars(Parser, &Loop))
        {
#ifdef BABYC_MICRO
            /* do-while token: cond then body until false */
            {
                PTR_TYPE_INT_SIGNED Cond;
                for (;;)
                {
                    ParserCopyPos(Parser, &Loop.CondFrom);
                    Cond = ExpressionParseInt(Parser);
                    if (!Cond || Parser->Mode != RunModeRun)
                        break;
                    ParserCopyPos(Parser, &Loop.BodyTemplate);
                    if (ParseStatement(Parser, TRUE) != ParseResultOk)
                        ProgramFail(Parser, "statement expected");
                    if (Parser->Mode == RunModeContinue)
                        Parser->Mode = PreMode;
                    if (Parser->Mode == RunModeBreak)
                    {
                        Parser->Mode = PreMode;
                        break;
                    }
                }
            }
#else
            IrExecuteLoop(Parser, &Loop, PreMode);
#endif
        }
    }

    if (Parser->Mode == RunModeBreak || Parser->Mode == RunModeContinue)
        Parser->Mode = PreMode;

    IrLoopRelease(Parser, &Loop);
    ParserCopyPos(Parser, &DoAfter);
}





/* parse a block of code and return what mode it returned in */
enum RunMode ParseBlock(struct ParseState *Parser, int AbsorbOpenBrace, int Condition)
{
    int PrevScopeID = 0, ScopeID = VariableScopeBegin(Parser, &PrevScopeID);

    if (AbsorbOpenBrace && LexGetToken(Parser, NULL, TRUE) != TokenLeftBrace)
        ProgramFail(Parser, "'{' expected");

    if (Parser->Mode == RunModeSkip || !Condition)
    {
        enum RunMode OldMode = Parser->Mode;
        Parser->Mode = RunModeSkip;
        if (!Parser->DebugMode && OldMode != RunModeCaseSearch && OldMode != RunModeGoto) {
            int BraceCount = 1;
            for (;;) {
                enum LexToken Tok = LexGetToken(Parser, NULL, FALSE);
                if (Tok == TokenEOF) ProgramFail(Parser, "'}' expected");
                if (Tok == TokenLeftBrace) { LexGetToken(Parser, NULL, TRUE); BraceCount++; }
                else if (Tok == TokenRightBrace) {
                    if (BraceCount == 1) break;
                    LexGetToken(Parser, NULL, TRUE); BraceCount--;
                } else LexGetToken(Parser, NULL, TRUE);
            }
        } else {
            while (ParseStatement(Parser, TRUE) == ParseResultOk) {}
        }
        Parser->Mode = OldMode;
    }
    else
    { 
        /* just run it in its current mode */
        while (ParseStatement(Parser, TRUE) == ParseResultOk)
        {}
    }
    
    if (LexGetToken(Parser, NULL, TRUE) != TokenRightBrace)
        ProgramFail(Parser, "'}' expected");

    VariableScopeEnd(Parser, ScopeID, PrevScopeID);

    return Parser->Mode;
}

/* parse a typedef declaration */
void ParseTypedef(struct ParseState *Parser)
{
    struct ValueType *Typ;
    struct ValueType **TypPtr;
    char *TypeName;
    struct Value InitValue;
#ifndef NO_FP
    struct ParseState TypeStart;
    enum LexToken FirstTypeToken;
    ParserCopy(&TypeStart, Parser);
    FirstTypeToken = LexGetToken(&TypeStart, NULL, TRUE);
    while (FirstTypeToken == TokenConstType || FirstTypeToken == TokenVolatileType
        || FirstTypeToken == TokenStaticType || FirstTypeToken == TokenAutoType
        || FirstTypeToken == TokenRegisterType || FirstTypeToken == TokenExternType)
        FirstTypeToken = LexGetToken(&TypeStart, NULL, TRUE);
#endif

    TypeParse(Parser, &Typ, &TypeName, NULL);
#ifndef NO_FP
    /* TypeParse uses FPType as the common execution representation for both
     * float and double. Preserve float's declared object type in typedefs so
     * sizeof(FloatAlias) and later declarations using that alias retain the
     * binary32 size, without changing function-call execution semantics. */
    if (FirstTypeToken == TokenFloatType && Typ != NULL
        && Typ->Base == TypeFP)
        Typ = &Parser->pc->FloatType;
#endif
    
    if (Parser->Mode == RunModeRun)
    {
        TypPtr = &Typ;
        InitValue.Typ = &Parser->pc->TypeType;
        InitValue.Val = (union AnyValue *)TypPtr;
        VariableDefine(Parser->pc, Parser, TypeName, &InitValue, NULL, FALSE);
    }
}

/* parse a statement */
enum ParseResult ParseStatement(struct ParseState *Parser, int CheckTrailingSemicolon)
{
    struct Value *CValue;
    struct Value *LexerValue;
    struct Value *VarValue;
    PTR_TYPE_INT_SIGNED Condition;
    struct ParseState PreState;
    enum LexToken Token;
    
    /* if we're debugging, check for a breakpoint */
    if (Parser->DebugMode && Parser->Mode == RunModeRun)
        DebugCheckStatement(Parser);
    
    /* take note of where we are and then grab a token to see what statement we have */   
    ParserCopyPos(&PreState, Parser);
    Token = LexGetToken(Parser, &LexerValue, TRUE);
    
    switch (Token)
    {
        case TokenEOF:
            return ParseResultEOF;
            
        case TokenIdentifier:
            /* might be a typedef-typed variable declaration or it might be an expression */
            if (VariableDefined(Parser->pc, LexerValue->Val->Identifier))
            {
                VariableGet(Parser->pc, Parser, LexerValue->Val->Identifier, &VarValue);
                if (VarValue->Typ->Base == Type_Type)
                {
                    ParserCopyPos(Parser, &PreState);
                    ParseDeclaration(Parser, Token);
                    break;
                }
            }
            else
            {
                /* it might be a goto label */
                enum LexToken NextToken = LexGetToken(Parser, NULL, FALSE);
                if (NextToken == TokenColon)
                {
                    /* declare the identifier as a goto label */
                    LexGetToken(Parser, NULL, TRUE);
                    if (Parser->Mode == RunModeGoto && Parser->SearchGotoLabel != NULL
                        && LexerValue->Val->Identifier != NULL
                        && (LexerValue->Val->Identifier == Parser->SearchGotoLabel
                            || strcmp(LexerValue->Val->Identifier, Parser->SearchGotoLabel) == 0))
                    {
                        Parser->Mode = RunModeRun;
                        Parser->SearchGotoLabel = NULL;
                        BabycGotoMatchCount++;
                    }
        
                    CheckTrailingSemicolon = FALSE;
                    break;
                }
                else if (NextToken == TokenOpenBracket
                         && Parser->pc->TopStackFrame == NULL)
                {
                    /* C89 implicit-int function definition/prototype:
                     *   main() { ... }
                     *   foo(int x); */
                    ParseFunctionDefinition(Parser, &Parser->pc->IntType,
                        LexerValue->Val->Identifier);
                    CheckTrailingSemicolon = FALSE;
                    break;
                }
#ifdef FEATURE_AUTO_DECLARE_VARIABLES
                else /* new_identifier = something */
                {    /* try to guess type and declare the variable based on assigned value */
                    if (NextToken == TokenAssign && !VariableDefinedAndOutOfScope(Parser->pc, LexerValue->Val->Identifier))
                    {
                        if (Parser->Mode == RunModeRun)
                        {
                            struct Value *CValue;
                            char* Identifier = LexerValue->Val->Identifier;

                            LexGetToken(Parser, NULL, TRUE);
                            if (!ExpressionParse(Parser, &CValue))
                            {
                                ProgramFail(Parser, "expected: expression");
                            }
                            
                            #if 0
                            PRINT_SOURCE_POS;
                            PlatformPrintf(Parser->pc->CStdOut, "%t %s = %d;\n", CValue->Typ, Identifier, CValue->Val->Integer);
                            printf("%d\n", VariableDefined(Parser->pc, Identifier));
                            #endif
                            VariableDefine(Parser->pc, Parser, Identifier, CValue, CValue->Typ, TRUE);
                            break;
                        }
                    }
                }
#endif
            }
            /* else fallthrough to expression */
            /* FALLTHROUGH */
        case TokenAsterisk: 
        case TokenAmpersand: 
        case TokenIncrement: 
        case TokenDecrement: 
        case TokenOpenBracket: 
            ParserCopyPos(Parser, &PreState);
            ExpressionParse(Parser, &CValue);
            if (Parser->Mode == RunModeRun) {
                /* In interactive mode, print the result of the expression
                 * so the user can inspect variable values easily. */
                if (Parser->FileName == Parser->pc->StrEmpty && CValue != NULL)
                {
                    PrintValue(CValue, Parser->pc->CStdOut);
                    PlatformPrintf(Parser->pc->CStdOut, "\n");
                }
                VariableStackPop(Parser, CValue);
            }
            break;
            
        case TokenLeftBrace:
            ParseBlock(Parser, FALSE, TRUE);
            CheckTrailingSemicolon = FALSE;
            break;
            
        case TokenIf:
            if (LexGetToken(Parser, NULL, TRUE) != TokenOpenBracket)
                ProgramFail(Parser, "'(' expected");
                
            Condition = ExpressionParseInt(Parser);
            
            if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                ProgramFail(Parser, "')' expected");

            if (ParseStatementMaybeRun(Parser, Condition ? TRUE : FALSE, TRUE) != ParseResultOk)
                ProgramFail(Parser, "statement expected");
            
            if (LexGetToken(Parser, NULL, FALSE) == TokenElse)
            {
                LexGetToken(Parser, NULL, TRUE);
                if (ParseStatementMaybeRun(Parser, /*!Condition*/ Condition ? FALSE : TRUE, TRUE) != ParseResultOk)
                    ProgramFail(Parser, "statement expected");
            }
            CheckTrailingSemicolon = FALSE;
            break;
        
        case TokenWhile:
            ParseWhile(Parser);
            CheckTrailingSemicolon = FALSE;
            break;
                
        case TokenDo:
            ParseDoWhile(Parser);
            break;
                
        case TokenFor:
            ParseFor(Parser);
            CheckTrailingSemicolon = FALSE;
            break;

        case TokenSemicolon: 
            CheckTrailingSemicolon = FALSE; 
            break;

        case TokenIntType:
        case TokenShortType:
        case TokenCharType:
        case TokenLongType:
        case TokenFloatType:
        case TokenDoubleType:
        case TokenVoidType:
        case TokenStructType:
        case TokenUnionType:
        case TokenEnumType:
        case TokenSignedType:
        case TokenUnsignedType:
        case TokenStaticType:
        case TokenAutoType:
        case TokenRegisterType:
        case TokenExternType:
        case TokenConstType:
        case TokenVolatileType:
            ParserCopyPos(Parser, &PreState);
            CheckTrailingSemicolon = ParseDeclaration(Parser, Token);
            break;
        
        case TokenHashDefine:
            ParseMacroDefinition(Parser);
            CheckTrailingSemicolon = FALSE;
            break;
            
#ifndef NO_HASH_INCLUDE
        case TokenHashInclude:
            if (LexGetToken(Parser, &LexerValue, TRUE) != TokenStringConstant)
                ProgramFail(Parser, "\"filename.h\" expected");
            {
                /* Trim leading/trailing space inside <...> or "..." so
                 * #include < stdio.h > matches built-in "stdio.h". */
                const char *raw = (const char *)LexerValue->Val->Pointer;
                const char *s, *e;
                char *name;
                if (raw == NULL)
                    ProgramFail(Parser, "\"filename.h\" expected");
                s = raw;
                while (*s == ' ' || *s == '\t')
                    s++;
                e = s;
                while (*e != '\0')
                    e++;
                while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
                    e--;
                if (e == s)
                    ProgramFail(Parser, "\"filename.h\" expected");
                name = TableStrRegister2(Parser->pc, s, (int)(e - s));
                IncludeFile(Parser->pc, name, Parser);
            }
            CheckTrailingSemicolon = FALSE;
            break;
#endif

        case TokenSwitch:
            if (LexGetToken(Parser, NULL, TRUE) != TokenOpenBracket)
                ProgramFail(Parser, "'(' expected");
                
            Condition = ExpressionParseInt(Parser);
            
            if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                ProgramFail(Parser, "')' expected");
            
            if (LexGetToken(Parser, NULL, FALSE) != TokenLeftBrace)
                ProgramFail(Parser, "'{' expected");
            
            { 
                /* new block so we can store parser state */
                enum RunMode OldMode = Parser->Mode;
                int OldSearchLabel = Parser->SearchLabel;
                Parser->Mode = RunModeCaseSearch;
                Parser->SearchLabel = (int)Condition;
                
                ParseBlock(Parser, TRUE, (OldMode != RunModeSkip) && (OldMode != RunModeReturn));
                
                /* C semantics: break only leaves the switch; continue/goto/return
                 * must propagate to the enclosing for/while or caller.
                 * Old code always restored OldMode and wiped continue/goto. */
                if (Parser->Mode == RunModeBreak || Parser->Mode == RunModeCaseSearch)
                    Parser->Mode = OldMode;
                else if (Parser->Mode == RunModeRun)
                    Parser->Mode = OldMode;
                /* RunModeContinue / RunModeGoto / RunModeReturn: keep */

                Parser->SearchLabel = OldSearchLabel;
            }

            CheckTrailingSemicolon = FALSE;
            break;

        case TokenCase:
            if (Parser->Mode == RunModeCaseSearch)
            {
                Parser->Mode = RunModeRun;
                Condition = ExpressionParseInt(Parser);
                Parser->Mode = RunModeCaseSearch;
            }
            else
                Condition = ExpressionParseInt(Parser);
                
            if (LexGetToken(Parser, NULL, TRUE) != TokenColon)
                ProgramFail(Parser, "':' expected");
            
            if (Parser->Mode == RunModeCaseSearch && Condition == Parser->SearchLabel)
                Parser->Mode = RunModeRun;

            CheckTrailingSemicolon = FALSE;
            break;
            
        case TokenDefault:
            if (LexGetToken(Parser, NULL, TRUE) != TokenColon)
                ProgramFail(Parser, "':' expected");
            
            if (Parser->Mode == RunModeCaseSearch)
                Parser->Mode = RunModeRun;
                
            CheckTrailingSemicolon = FALSE;
            break;

        case TokenBreak:
            if (Parser->Mode == RunModeRun)
                Parser->Mode = RunModeBreak;
            break;
            
        case TokenContinue:
            if (Parser->Mode == RunModeRun)
                Parser->Mode = RunModeContinue;
            break;
            
        case TokenReturn:
            if (Parser->Mode == RunModeRun)
            {
                if (!Parser->pc->TopStackFrame || Parser->pc->TopStackFrame->ReturnValue->Typ->Base != TypeVoid)
                {
                    if (!ExpressionParse(Parser, &CValue))
                        ProgramFail(Parser, "value required in return");
                    
                    if (!Parser->pc->TopStackFrame) /* return from top-level program? */
                        PlatformExit(Parser->pc, (int)ExpressionCoerceInteger(CValue));
                    else
                        ExpressionAssign(Parser, Parser->pc->TopStackFrame->ReturnValue, CValue, TRUE, NULL, 0, FALSE);

                    VariableStackPop(Parser, CValue);
                }
                else
                {
                    if (ExpressionParse(Parser, &CValue))
                        ProgramFail(Parser, "value in return from a void function");                    
                }
                
                Parser->Mode = RunModeReturn;
            }
            else
                ExpressionParse(Parser, &CValue);
            break;

        case TokenTypedef:
            ParseTypedef(Parser);
            break;
            
        case TokenGoto:
            if (LexGetToken(Parser, &LexerValue, TRUE) != TokenIdentifier)
                ProgramFail(Parser, "identifier expected");
            
            if (Parser->Mode == RunModeRun)
            { 
                Parser->SearchGotoLabel = TableStrRegister(Parser->pc,
                    LexerValue->Val->Identifier);
                Parser->Mode = RunModeGoto;
            }
            break;
                
        case TokenDelete:
        {
            /* try it as a function or variable name to delete */
            if (LexGetToken(Parser, &LexerValue, TRUE) != TokenIdentifier)
                ProgramFail(Parser, "identifier expected");
                
            if (Parser->Mode == RunModeRun)
            { 
                /* delete this variable or function */
                CValue = TableDelete(Parser->pc, &Parser->pc->GlobalTable, LexerValue->Val->Identifier);

                if (CValue == NULL)
                    ProgramFail(Parser, "'%s' is not defined", LexerValue->Val->Identifier);
                
                VariableFree(Parser->pc, CValue);
            }
            break;
        }
        
        default:
            /* Unexpected token: restore Pos so callers can recover, but keep
             * Line/CharacterPos from the bad token. Otherwise a leading //
             * comment or blank lines rewind Line to the previous line and the
             * caret points at the comment (004_string_compare: "02:" on L2
             * was reported as L1:0). */
            {
                int ErrLine = Parser->Line;
                int ErrCol = Parser->CharacterPos;
                ParserCopyPos(Parser, &PreState);
                Parser->Line = ErrLine;
                Parser->CharacterPos = ErrCol;
            }
            return ParseResultError;
    }
    
    if (CheckTrailingSemicolon)
    {
        enum LexToken SemiTok = LexGetToken(Parser, NULL, FALSE);
        if (SemiTok != TokenSemicolon)
        {
            /* Be tolerant: if the next token starts a new declaration or
             * function, do not force a semicolon.  This recovers from
             * cases where a previous function body left the token stream
             * slightly out of sync (common with complex pointer expressions). */
            if (SemiTok == TokenIntType || SemiTok == TokenShortType ||
                SemiTok == TokenCharType || SemiTok == TokenLongType ||
                SemiTok == TokenFloatType || SemiTok == TokenDoubleType ||
                SemiTok == TokenVoidType || SemiTok == TokenStructType ||
                SemiTok == TokenUnionType || SemiTok == TokenEnumType ||
                SemiTok == TokenSignedType || SemiTok == TokenUnsignedType ||
                SemiTok == TokenStaticType || SemiTok == TokenAutoType ||
                SemiTok == TokenRegisterType || SemiTok == TokenExternType ||
                SemiTok == TokenConstType || SemiTok == TokenVolatileType || SemiTok == TokenIdentifier ||
                SemiTok == TokenTypedef || SemiTok == TokenEOF)
            {
                /* leave the token for the next ParseStatement – no error */
            }
            else
            {
                LexGetToken(Parser, NULL, TRUE); /* consume the unexpected token */
                ProgramFail(Parser, "';' expected");
            }
        }
        else
            LexGetToken(Parser, NULL, TRUE); /* consume the ';' */
    }
    
    return ParseResultOk;
}

/* quick scan a source file for definitions */
void PicocParse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int RunIt, int CleanupNow, int CleanupSource, int EnableDebugger)
{
    struct ParseState Parser;
    enum ParseResult Ok;
    struct CleanupTokenNode *NewCleanupNode;
    char *RegFileName = TableStrRegister(pc, FileName);

    /* Remember the last file for the debugger (set_breakpoint from interactive) */
    if (RegFileName != NULL && RegFileName != pc->StrEmpty)
        pc->DebugLastFileName = RegFileName;

    int TokenLen = 0;
    void *Tokens;

    /* Register source ownership before lexing. LexFail can longjmp before
     * LexAnalyse returns, so registering afterwards leaks both source and
     * lexer scratch buffers on lexical errors. */
    NewCleanupNode = HeapAllocMem(pc, sizeof(struct CleanupTokenNode));
    if (NewCleanupNode == NULL)
    {
        if (!CleanupNow && CleanupSource && Source != NULL)
            HeapFreeMem(pc, (void *)Source);
        ProgramFailNoParser(pc, DiagnosticMsgOutOfMemory);
    }
    NewCleanupNode->Tokens = NULL;
    NewCleanupNode->SourceText = (!CleanupNow && CleanupSource) ? Source : NULL;
    NewCleanupNode->Next = pc->CleanupTokenList;
    pc->CleanupTokenList = NewCleanupNode;

    Tokens = LexAnalyse(pc, RegFileName, Source, SourceLen, &TokenLen);
    NewCleanupNode->Tokens = Tokens;
    if (getenv("BABYC_TOKEN_DUMP") != NULL)
        LexDumpTokens(stderr, Tokens, TokenLen);

    /* do the parsing */
    LexInitParser(&Parser, pc, Source, Tokens, RegFileName, RunIt, EnableDebugger);

    do {
        Ok = ParseStatement(&Parser, TRUE);
    } while (Ok == ParseResultOk);

    if (Ok == ParseResultError)
        ProgramFail(&Parser, "parse error");

    /* CleanupNow owns no persistent token state after a successful parse.
     * Nested includes may have pushed nodes, so unlink this exact node. */
    if (CleanupNow)
    {
        struct CleanupTokenNode **Link = &pc->CleanupTokenList;
        while (*Link != NULL && *Link != NewCleanupNode)
            Link = &(*Link)->Next;
        if (*Link == NewCleanupNode)
            *Link = NewCleanupNode->Next;
        HeapFreeMem(pc, Tokens);
        HeapFreeMem(pc, NewCleanupNode);
    }
}

/* parse interactively */
void PicocParseInteractiveNoStartPrompt(Picoc *pc, int EnableDebugger)
{
    struct ParseState Parser;
    enum ParseResult Ok;
    
    LexInitParser(&Parser, pc, NULL, NULL, pc->StrEmpty, TRUE, EnableDebugger);
    PicocPlatformSetExitPoint(pc);
    LexInteractiveClear(pc, &Parser);

    do
    {
        LexInteractiveStatementPrompt(pc);
        Ok = ParseStatement(&Parser, TRUE);
        LexInteractiveCompleted(pc, &Parser);
        
    } while (Ok == ParseResultOk);
    
    if (Ok == ParseResultError)
        ProgramFail(&Parser, "parse error");
    
    PlatformPrintf(pc->CStdOut, "\n");
}

/* parse interactively, showing a startup message */
void PicocParseInteractive(Picoc *pc)
{
    PlatformPrintf(pc->CStdOut, INTERACTIVE_PROMPT_START);
    PicocParseInteractiveNoStartPrompt(pc, TRUE);
}
