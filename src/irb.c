/* irb.c — bound loop IR executor (parser/executor separation)
 * Extracted from parse.c. Compile of loops still starts in ParseFor/While;
 * this file owns IrbEval / IrbLocate / IrbExec / IrbTryLoop.
 */
#include "picoc.h"
#include "cstdlib/printf_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "interpreter.h"
#include "ir.h"
#include "ir_private.h"

/* ======================================================================
 * Irb — bound loop IR
 *
 * A loop (for / while / do) whose body is made of integer expression
 * statements, if/else, nested loops, break/continue is compiled ONCE per
 * source position (cache keyed by the body's token address) into IrExpr /
 * IrStmt trees, and executed without re-lexing.  Every identifier is
 * resolved to its struct Value* when the loop is entered (IrbBind), so the
 * hot path does no table lookups.  Integer variables and 1-D int arrays /
 * pointers are read and written in place, with the same uninitialised-value
 * rules (per-byte bitmap) and the same C int semantics as the token path.
 *
 * Anything else (calls, printf, declarations, return, switch, ...) is NOT
 * an obstacle: that single statement is delegated back to the token path
 * (IrStDelegate) while the surrounding loop machinery stays in IR.  If the
 * loop itself cannot be compiled or bound, the previous paths run unchanged.
 * ====================================================================== */
#define IRB_MAX_BIND   24
#define IRB_CACHE_SIZE 256
#define IRB_USE_R      1
#define IRB_USE_W      2
#define IRB_USE_BASE   4
#define IRB_USE_MEMBER 8
#define IRB_USE_ARROW  16  /* p->field: pointer-to-struct, int-base field (Phase B) */

#define IRB_EXEC_NORMAL   0
#define IRB_EXEC_BREAK    1
#define IRB_EXEC_CONTINUE 2
#define IRB_EXEC_EXIT     3   /* Parser->Mode was changed by a delegated statement */

typedef struct IrbCode {
    const void *Site;
    int Ok;
    IrExpr *Cond;
    IrExpr *Inc;
    IrStmt *Body;
    int NBind;
    char *BindName[IRB_MAX_BIND];
    char *BindMember[IRB_MAX_BIND];  /* field name for IRB_USE_MEMBER slots, else NULL */
    unsigned char BindUse[IRB_MAX_BIND];
    signed char BindBase[IRB_MAX_BIND];
    signed char BindElem[IRB_MAX_BIND];
    int Classified;
    struct IrbCode *Next;
} IrbCode;

static IrbCode *IrbCache[IRB_CACHE_SIZE];
static IrbCode *IrbBuilding = NULL;
static int IrbTrace = 0;

static int IrbIsIntBase(int B)
{
    return B == TypeInt || B == TypeChar || B == TypeShort
        || B == TypeLong || B == TypeLongLong;
}

static IrExpr *IrbNewExpr(IrExprKind K)
{
    IrExpr *E = (IrExpr *)calloc(1, sizeof(IrExpr));
    if (E != NULL)
    {
        E->Kind = K;
        E->BindSlot = -1;
        E->LocalSlot = -1;
        E->ParamSlot = -1;
    }
    return E;
}

static IrStmt *IrbNewStmt(IrStmtKind K, struct ParseState *P)
{
    IrStmt *S = (IrStmt *)calloc(1, sizeof(IrStmt));
    if (S != NULL)
    {
        S->Kind = K;
        S->LocalSlot = -1;
        S->SourceLine = P->Line;
        S->SourceFile = P->FileName;
    }
    return S;
}

/* ---- exact diagnostics -------------------------------------------------
 * Runtime errors detected in Irb (uninitialised read, division by zero) are
 * reported by re-running the ROOT expression that was being evaluated on the
 * token path, from the position saved at compile time.  The message and the
 * caret column are then produced by the very same code as before, whatever
 * the shape of the expression.  If the token path does not object, Irb's own
 * diagnostic is raised, positioned at the start of that expression. */
#define IRB_REPLAY_STMT 0   /* ParseStatement(Parser, TRUE)  */
#define IRB_REPLAY_COND 1   /* ExpressionParseInt            */
#define IRB_REPLAY_INC  2   /* ParseStatement(Parser, FALSE) */

static IrExpr *IrbRoot = NULL;   /* root expression currently being evaluated */

static IrExpr *IrbSetReplay(IrExpr *E, struct ParseState *At, int Kind)
{
    if (E != NULL && E->Replay == NULL)
    {
        E->Replay = (struct ParseState *)malloc(sizeof(struct ParseState));
        if (E->Replay != NULL)
        {
            ParserCopy(E->Replay, At);
            E->ReplayKind = Kind;
        }
    }
    return E;
}

static void IrbFail(struct ParseState *Parser, int Replay, const char *Fmt, const char *Name)
{
    IrExpr *R = IrbRoot;
    if (R != NULL && R->Replay != NULL)
    {
        ParserCopyPos(Parser, R->Replay);
        if (Replay)
        {
            switch (R->ReplayKind)
            {
                case IRB_REPLAY_COND: ExpressionParseInt(Parser); break;
                case IRB_REPLAY_INC:  ParseStatement(Parser, FALSE); break;
                default:              ParseStatement(Parser, TRUE); break;
            }
            ParserCopyPos(Parser, R->Replay); /* token path did not object */
        }
    }
    if (Name != NULL)
        ProgramFail(Parser, Fmt, Name);
    else
        ProgramFail(Parser, Fmt);
}

#define IRB_EVAL(P, B, E) (IrbRoot = (E), IrbEval((P), (B), (E)))

/* Field is NULL for a plain identifier or array/pointer base; non-NULL for
 * ident.field. The (Name, Field) pair is the real slot key, so two member
 * accesses on the same struct variable but different fields (s.a, s.b) get
 * their own slots instead of colliding, while repeated ident.field or bare
 * ident uses of the same name still share one slot exactly as before. */
static int IrbSlot(char *Name, int Use, char *Field)
{
    IrbCode *C = IrbBuilding;
    int i;
    for (i = 0; i < C->NBind; i++)
        if (C->BindName[i] == Name && C->BindMember[i] == Field)
        {
            C->BindUse[i] |= (unsigned char)Use;
            return i;
        }
    if (C->NBind >= IRB_MAX_BIND)
        return -1;
    C->BindName[C->NBind] = Name;
    C->BindMember[C->NBind] = Field;
    C->BindUse[C->NBind] = (unsigned char)Use;
    C->BindBase[C->NBind] = -1;
    C->BindElem[C->NBind] = -1;
    return C->NBind++;
}

static int IrbIsLvalue(IrExpr *E)
{
    return E != NULL && (E->Kind == IrExIdent || E->Kind == IrExIndex || E->Kind == IrExMember);
}

static void IrbMarkWrite(IrExpr *E)
{
    if (E != NULL && E->BindSlot >= 0)
        IrbBuilding->BindUse[E->BindSlot] |= IRB_USE_W;
}

static int IrbIsAssignOp(enum LexToken Op)
{
    return Op == TokenAssign || Op == TokenAddAssign || Op == TokenSubtractAssign
        || Op == TokenMultiplyAssign || Op == TokenDivideAssign || Op == TokenModulusAssign
        || Op == TokenShiftLeftAssign || Op == TokenShiftRightAssign
        || Op == TokenArithmeticAndAssign || Op == TokenArithmeticOrAssign
        || Op == TokenArithmeticExorAssign;
}

static enum LexToken IrbAssignToBin(enum LexToken Op)
{
    switch (Op)
    {
        case TokenAddAssign:            return TokenPlus;
        case TokenSubtractAssign:       return TokenMinus;
        case TokenMultiplyAssign:       return TokenAsterisk;
        case TokenDivideAssign:         return TokenSlash;
        case TokenModulusAssign:        return TokenModulus;
        case TokenShiftLeftAssign:      return TokenShiftLeft;
        case TokenShiftRightAssign:     return TokenShiftRight;
        case TokenArithmeticAndAssign:  return TokenAmpersand;
        case TokenArithmeticOrAssign:   return TokenArithmeticOr;
        case TokenArithmeticExorAssign: return TokenArithmeticExor;
        default:                        return TokenNone;
    }
}

static int IrbPrec(enum LexToken Op)
{
    if (IrbIsAssignOp(Op))
        return 0;
    return IrOpPrecedence(Op);
}

static IrExpr *IrbCompileExpr(struct ParseState *P, int MinPrec);

static IrExpr *IrbCompilePrimary(struct ParseState *P)
{
    struct Value *LV = NULL;
    enum LexToken Tok;
    IrExpr *E, *X;

    Tok = LexGetToken(P, &LV, TRUE);
    switch (Tok)
    {
        case TokenOpenBracket:
        {
            struct Value *V2 = NULL;
            enum LexToken T2 = LexGetToken(P, &V2, FALSE);
            if (IsTypeToken(P, T2, V2))
                return NULL; /* cast */
            X = IrbCompileExpr(P, 0);
            if (X == NULL || LexGetToken(P, NULL, TRUE) != TokenCloseBracket)
                return NULL;
            return X;
        }
        case TokenPlus:
        case TokenMinus:
        case TokenUnaryNot:
        case TokenUnaryExor:
            X = IrbCompilePrimary(P);
            if (X == NULL)
                return NULL;
            if (Tok == TokenPlus)
                return X;
            E = IrbNewExpr(IrExUnary);
            if (E == NULL)
                return NULL;
            E->Op = Tok;
            E->Left = X;
            E->ConstVal = 0;
            return E;
        case TokenIncrement:
        case TokenDecrement:
            X = IrbCompilePrimary(P);
            if (!IrbIsLvalue(X))
                return NULL;
            IrbMarkWrite(X);
            E = IrbNewExpr(IrExUnary);
            if (E == NULL)
                return NULL;
            E->Op = Tok;
            E->Left = X;
            E->ConstVal = 0; /* prefix */
            return E;
        case TokenIntegerConstant:
        case TokenCharacterConstant:
            E = IrbNewExpr(IrExConst);
            if (E == NULL)
                return NULL;
            E->ConstVal = ExpressionCoerceInteger(LV);
            E->I32 = (Tok == TokenIntegerConstant && LV->Typ->Base == TypeInt);
            return E;
        case TokenIdentifier:
        {
            char *Name = LV->Val->Identifier;
            enum LexToken Nx = LexGetToken(P, NULL, FALSE);
            int Slot;
            struct Value *MV = NULL;

            if (Nx == TokenOpenBracket)
                return NULL; /* call, or function-like macro */
            if (VariableDefined(P->pc, Name)
                && TableGet(&P->pc->GlobalTable, Name, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->Typ->Base == TypeMacro)
            {
                /* object-like macro that is a single integer literal → constant */
                struct ParseState MP;
                struct Value *MLex = NULL;
                if (MV->Val->MacroDef.NumParams != 0)
                    return NULL;
                ParserCopy(&MP, P);
                MP.Pos = MV->Val->MacroDef.Body.Pos;
                if (LexGetToken(&MP, &MLex, TRUE) != TokenIntegerConstant || MLex == NULL)
                    return NULL;
                E = IrbNewExpr(IrExConst);
                if (E == NULL)
                    return NULL;
                E->ConstVal = ExpressionCoerceInteger(MLex);
                E->I32 = (MLex->Typ->Base == TypeInt);
                return E;
            }
            if (Nx == TokenLeftSquareBracket)
            {
                IrExpr *Idx;
                Slot = IrbSlot(Name, IRB_USE_BASE, NULL);
                if (Slot < 0)
                    return NULL;
                LexGetToken(P, NULL, TRUE); /* [ */
                Idx = IrbCompileExpr(P, 0);
                if (Idx == NULL || LexGetToken(P, NULL, TRUE) != TokenRightSquareBracket)
                    return NULL;
                E = IrbNewExpr(IrExIndex);
                if (E == NULL)
                    return NULL;
                E->Right = Idx;
                E->BindSlot = Slot; /* root index holds the array binding */
                /* Phase C: a[i][j]... chain further indices */
                while (LexGetToken(P, NULL, FALSE) == TokenLeftSquareBracket)
                {
                    IrExpr *Inner = E;
                    LexGetToken(P, NULL, TRUE); /* [ */
                    Idx = IrbCompileExpr(P, 0);
                    if (Idx == NULL || LexGetToken(P, NULL, TRUE) != TokenRightSquareBracket)
                        return NULL;
                    E = IrbNewExpr(IrExIndex);
                    if (E == NULL)
                        return NULL;
                    E->Left = Inner;
                    E->Right = Idx;
                    E->BindSlot = -1; /* only root carries the bind */
                }
                /* Residual: a[i].m / a[i]->m (array of struct / ptr) */
                for (;;)
                {
                    enum LexToken NxM = LexGetToken(P, NULL, FALSE);
                    if (NxM != TokenDot && NxM != TokenArrow)
                        break;
                    {
                        int IsArr = (NxM == TokenArrow);
                        struct Value *FV2 = NULL;
                        char *Fld;
                        LexGetToken(P, NULL, TRUE);
                        if (LexGetToken(P, &FV2, TRUE) != TokenIdentifier || FV2 == NULL)
                            return NULL;
                        Fld = FV2->Val->Identifier;
                        X = IrbNewExpr(IrExMember);
                        if (X == NULL)
                            return NULL;
                        X->Left = E;
                        X->Ident = Fld;
                        X->Op = IsArr ? TokenArrow : TokenDot;
                        X->BindSlot = -1;
                        E = X;
                    }
                }
                /* Index / index+member complete — do not fall through to Ident */
                {
                    enum LexToken NxP = LexGetToken(P, NULL, FALSE);
                    if (NxP == TokenIncrement || NxP == TokenDecrement)
                    {
                        LexGetToken(P, NULL, TRUE);
                        IrbMarkWrite(E);
                        X = IrbNewExpr(IrExUnary);
                        if (X == NULL)
                            return NULL;
                        X->Op = NxP;
                        X->Left = E;
                        X->ConstVal = 1;
                        return X;
                    }
                    return E;
                }
            }
            else if (Nx == TokenDot || Nx == TokenArrow)
            {
                /* Phase A/B/C: ident.field, p->field, and nested chains
                 * (o.in.v, p->a.b). Field layout resolved in IrbLocate via
                 * Members. Intermediate fields may be struct; final load
                 * requires int-base (checked in IrbLocate). */
                char *Field;
                struct Value *FV = NULL;
                enum LexToken Nx2;
                int IsArrow = (Nx == TokenArrow);
                int Use;
                IrExpr *Chain;

                LexGetToken(P, NULL, TRUE); /* '.' or '->' */
                if (LexGetToken(P, &FV, TRUE) != TokenIdentifier || FV == NULL)
                    return NULL;
                Field = FV->Val->Identifier;

                Use = IsArrow ? IRB_USE_ARROW : IRB_USE_MEMBER;
                Slot = IrbSlot(Name, Use, Field);
                if (Slot < 0)
                    return NULL;
                E = IrbNewExpr(IrExMember);
                if (E == NULL)
                    return NULL;
                E->BindSlot = Slot;
                E->Ident = Field;
                E->Op = IsArrow ? TokenArrow : TokenDot;
                Chain = E;

                /* Phase C: chain further . / -> */
                for (;;)
                {
                    Nx2 = LexGetToken(P, NULL, FALSE);
                    if (Nx2 != TokenDot && Nx2 != TokenArrow)
                        break;
                    IsArrow = (Nx2 == TokenArrow);
                    LexGetToken(P, NULL, TRUE);
                    if (LexGetToken(P, &FV, TRUE) != TokenIdentifier || FV == NULL)
                        return NULL;
                    Field = FV->Val->Identifier;
                    /* Keep root Use; nested nodes share root binding via Left */
                    X = IrbNewExpr(IrExMember);
                    if (X == NULL)
                        return NULL;
                    X->Left = Chain;
                    X->Ident = Field;
                    X->Op = IsArrow ? TokenArrow : TokenDot;
                    X->BindSlot = -1;
                    Chain = X;
                }
                E = Chain;
                /* Nested path: clear BindMember so IrbBind only checks root
                 * aggregate type; final int-base enforced at load/index. */
                if (E->Left != NULL)
                    IrbBuilding->BindMember[Slot] = NULL;

                /* Residual: s.field[i] / s.a.b[i] — index into member array */
                while (LexGetToken(P, NULL, FALSE) == TokenLeftSquareBracket)
                {
                    IrExpr *Idx;
                    LexGetToken(P, NULL, TRUE); /* [ */
                    Idx = IrbCompileExpr(P, 0);
                    if (Idx == NULL || LexGetToken(P, NULL, TRUE) != TokenRightSquareBracket)
                        return NULL;
                    X = IrbNewExpr(IrExIndex);
                    if (X == NULL)
                        return NULL;
                    X->Left = E;
                    X->Right = Idx;
                    X->BindSlot = -1;
                    E = X;
                    IrbBuilding->BindMember[Slot] = NULL;
                }
                /* Residual: further . / -> after index-on-member (rare) */
                for (;;)
                {
                    Nx2 = LexGetToken(P, NULL, FALSE);
                    if (Nx2 != TokenDot && Nx2 != TokenArrow)
                        break;
                    {
                        int IsArr = (Nx2 == TokenArrow);
                        struct Value *FV2 = NULL;
                        char *Fld;
                        LexGetToken(P, NULL, TRUE);
                        if (LexGetToken(P, &FV2, TRUE) != TokenIdentifier || FV2 == NULL)
                            return NULL;
                        Fld = FV2->Val->Identifier;
                        X = IrbNewExpr(IrExMember);
                        if (X == NULL)
                            return NULL;
                        X->Left = E;
                        X->Ident = Fld;
                        X->Op = IsArr ? TokenArrow : TokenDot;
                        X->BindSlot = -1;
                        E = X;
                        IrbBuilding->BindMember[Slot] = NULL;
                    }
                }
                Nx2 = LexGetToken(P, NULL, FALSE);
                if (Nx2 == TokenIncrement || Nx2 == TokenDecrement)
                {
                    LexGetToken(P, NULL, TRUE);
                    IrbMarkWrite(E);
                    X = IrbNewExpr(IrExUnary);
                    if (X == NULL)
                        return NULL;
                    X->Op = Nx2;
                    X->Left = E;
                    X->ConstVal = 1; /* postfix */
                    return X;
                }
                return E;
            }
            else
            {
                Slot = IrbSlot(Name, IRB_USE_R, NULL);
                if (Slot < 0)
                    return NULL;
                E = IrbNewExpr(IrExIdent);
                if (E == NULL)
                    return NULL;
            }
            E->BindSlot = Slot;
            E->Ident = Name;
            Nx = LexGetToken(P, NULL, FALSE);
            if (Nx == TokenIncrement || Nx == TokenDecrement)
            {
                LexGetToken(P, NULL, TRUE);
                IrbMarkWrite(E);
                X = IrbNewExpr(IrExUnary);
                if (X == NULL)
                    return NULL;
                X->Op = Nx;
                X->Left = E;
                X->ConstVal = 1; /* postfix */
                return X;
            }
            return E;
        }
        default:
            return NULL;
    }
}

static IrExpr *IrbCompileExpr(struct ParseState *P, int MinPrec)
{
    IrExpr *L, *R, *B;
    enum LexToken Op;
    int Prec;

    L = IrbCompilePrimary(P);
    if (L == NULL)
        return NULL;
    for (;;)
    {
        Op = LexGetToken(P, NULL, FALSE);
        Prec = IrbPrec(Op);
        if (Prec < 0 || Prec < MinPrec)
            break;
        LexGetToken(P, NULL, TRUE);
        if (IrbIsAssignOp(Op))
        {
            if (!IrbIsLvalue(L))
                return NULL;
            R = IrbCompileExpr(P, Prec); /* right associative */
            if (R == NULL)
                return NULL;
            IrbMarkWrite(L);
            B = IrbNewExpr(IrExAssign);
            if (B == NULL)
                return NULL;
            B->Op = Op;
        }
        else
        {
            R = IrbCompileExpr(P, Prec + 1);
            if (R == NULL)
                return NULL;
            B = IrbNewExpr(IrExBinOp);
            if (B == NULL)
                return NULL;
            B->Op = Op;
        }
        B->Left = L;
        B->Right = R;
        L = B;
    }
    return L;
}

static IrStmt *IrbCompileStmt(struct ParseState *P);

/* Lexically advance past one statement without evaluating it.
 * Used when Irb cannot compile a statement and must not call ParseStatement
 * in RunModeSkip — that path ProgramFail/heap-asserts on (*s.fp)(args) (test 276). */
static int IrbLexSkipStatement(struct ParseState *P)
{
    enum LexToken Tok;
    int Brace = 0;
    int Paren = 0;
    int Square = 0;
    int Seen = 0;

    Tok = LexGetToken(P, NULL, FALSE);
    if (Tok == TokenLeftBrace)
    {
        /* compound statement: match braces */
        LexGetToken(P, NULL, TRUE);
        Brace = 1;
        while (Brace > 0)
        {
            Tok = LexGetToken(P, NULL, TRUE);
            if (Tok == TokenEOF || Tok == TokenEndOfFunction)
                return 0;
            if (Tok == TokenLeftBrace) Brace++;
            else if (Tok == TokenRightBrace) Brace--;
        }
        return 1;
    }

    /* expression / simple statement: to semicolon at paren/bracket depth 0 */
    for (;;)
    {
        Tok = LexGetToken(P, NULL, TRUE);
        Seen = 1;
        if (Tok == TokenEOF || Tok == TokenEndOfFunction)
            return 0;
        if (Tok == TokenLeftBrace) Brace++;
        else if (Tok == TokenRightBrace)
        {
            if (Brace == 0) return 0;
            Brace--;
        }
        else if (Tok == TokenOpenBracket) Paren++;
        else if (Tok == TokenCloseBracket)
        {
            if (Paren > 0) Paren--;
        }
        else if (Tok == TokenLeftSquareBracket) Square++;
        else if (Tok == TokenRightSquareBracket)
        {
            if (Square > 0) Square--;
        }
        else if (Tok == TokenSemicolon && Paren == 0 && Square == 0 && Brace == 0)
            return 1;
    }
}

/* compile one statement; if it cannot be compiled but is a plain statement,
 * delegate exactly that statement to the token path */
static IrStmt *IrbCompileStmtD(struct ParseState *P)
{
    struct ParseState Save;
    struct Value *LV = NULL;
    enum LexToken Tok;
    IrStmt *S;

    ParserCopy(&Save, P);
    Tok = LexGetToken(P, &LV, FALSE);
    ParserCopy(P, &Save);
    if (Tok == TokenEOF || Tok == TokenRightBrace || Tok == TokenGoto
        || Tok == TokenTypedef || IsTypeToken(P, Tok, LV))
        return NULL; /* declarations / goto: leave the whole enclosing statement to tokens */
    if (Tok == TokenIdentifier)
    {
        /* label?  identifier ':' */
        struct ParseState L2;
        ParserCopy(&L2, P);
        LexGetToken(&L2, NULL, TRUE);
        if (LexGetToken(&L2, NULL, FALSE) == TokenColon)
            return NULL;
    }

    S = IrbCompileStmt(P);
    if (S != NULL)
        return S;

    /* Cannot compile: lexical skip only (no ParseStatement / RunModeSkip). */
    ParserCopy(P, &Save);
    if (!IrbLexSkipStatement(P))
        return NULL;
    S = IrbNewStmt(IrStDelegate, &Save);
    if (S == NULL)
        return NULL;
    S->Resume = (struct ParseState *)malloc(sizeof(struct ParseState));
    if (S->Resume == NULL)
        return NULL;
    ParserCopy(S->Resume, &Save);
    return S;
}

static IrStmt *IrbCompileStmt(struct ParseState *P)
{
    enum LexToken Tok = LexGetToken(P, NULL, FALSE);
    IrStmt *S;

    if (Tok == TokenLeftBrace)
    {
        IrStmt **List;
        int Cap = 16, Count = 0;
        S = IrbNewStmt(IrStBlock, P);
        LexGetToken(P, NULL, TRUE);
        List = (IrStmt **)calloc((size_t)Cap, sizeof(IrStmt *));
        if (S == NULL || List == NULL)
            return NULL;
        while (LexGetToken(P, NULL, FALSE) != TokenRightBrace)
        {
            IrStmt *One;
            if (LexGetToken(P, NULL, FALSE) == TokenEOF)
                return NULL;
            One = IrbCompileStmtD(P);
            if (One == NULL)
                return NULL;
            if (Count >= Cap)
            {
                IrStmt **Bigger = (IrStmt **)realloc(List, (size_t)Cap * 2 * sizeof(IrStmt *));
                if (Bigger == NULL)
                    return NULL;
                List = Bigger;
                Cap *= 2;
            }
            List[Count++] = One;
        }
        LexGetToken(P, NULL, TRUE); /* } */
        S->Stmts = List;
        S->StmtCount = Count;
        return S;
    }
    if (Tok == TokenSemicolon)
    {
        S = IrbNewStmt(IrStBlock, P);
        LexGetToken(P, NULL, TRUE);
        return S;
    }
    if (Tok == TokenBreak || Tok == TokenContinue)
    {
        S = IrbNewStmt(Tok == TokenBreak ? IrStBreak : IrStContinue, P);
        LexGetToken(P, NULL, TRUE);
        if (LexGetToken(P, NULL, TRUE) != TokenSemicolon)
            return NULL;
        return S;
    }
    if (Tok == TokenIf)
    {
        struct ParseState RS;
        S = IrbNewStmt(IrStIf, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL || LexGetToken(P, NULL, TRUE) != TokenOpenBracket)
            return NULL;
        ParserCopy(&RS, P);
        S->Expr = IrbCompileExpr(P, 0);
        if (S->Expr == NULL || LexGetToken(P, NULL, TRUE) != TokenCloseBracket)
            return NULL;
        IrbSetReplay(S->Expr, &RS, IRB_REPLAY_COND);
        S->ThenStmt = IrbCompileStmtD(P);
        if (S->ThenStmt == NULL)
            return NULL;
        if (LexGetToken(P, NULL, FALSE) == TokenElse)
        {
            LexGetToken(P, NULL, TRUE);
            S->ElseStmt = IrbCompileStmtD(P);
            if (S->ElseStmt == NULL)
                return NULL;
        }
        return S;
    }
    if (Tok == TokenWhile)
    {
        struct ParseState RS;
        S = IrbNewStmt(IrStWhile, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL || LexGetToken(P, NULL, TRUE) != TokenOpenBracket)
            return NULL;
        ParserCopy(&RS, P);
        S->Expr = IrbCompileExpr(P, 0);
        if (S->Expr == NULL || LexGetToken(P, NULL, TRUE) != TokenCloseBracket)
            return NULL;
        IrbSetReplay(S->Expr, &RS, IRB_REPLAY_COND);
        S->BodyStmt = IrbCompileStmtD(P);
        return S->BodyStmt != NULL ? S : NULL;
    }
    if (Tok == TokenDo)
    {
        S = IrbNewStmt(IrStDo, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL)
            return NULL;
        S->BodyStmt = IrbCompileStmtD(P);
        if (S->BodyStmt == NULL || LexGetToken(P, NULL, TRUE) != TokenWhile
            || LexGetToken(P, NULL, TRUE) != TokenOpenBracket)
            return NULL;
        {
            struct ParseState RS;
            ParserCopy(&RS, P);
            S->Expr = IrbCompileExpr(P, 0);
            if (S->Expr == NULL || LexGetToken(P, NULL, TRUE) != TokenCloseBracket
                || LexGetToken(P, NULL, TRUE) != TokenSemicolon)
                return NULL;
            IrbSetReplay(S->Expr, &RS, IRB_REPLAY_COND);
        }
        return S;
    }
    if (Tok == TokenFor)
    {
        struct Value *LV = NULL;
        enum LexToken T2;
        S = IrbNewStmt(IrStFor, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL || LexGetToken(P, NULL, TRUE) != TokenOpenBracket)
            return NULL;
        T2 = LexGetToken(P, &LV, FALSE);
        if (T2 != TokenSemicolon)
        {
            IrExpr *Init;
            struct ParseState RS;
            if (IsTypeToken(P, T2, LV))
                return NULL; /* for (int i ...) → token path */
            ParserCopy(&RS, P);
            Init = IrbCompileExpr(P, 0);
            if (Init == NULL)
                return NULL;
            IrbSetReplay(Init, &RS, IRB_REPLAY_STMT);
            S->InitStmt = IrbNewStmt(IrStExpr, P);
            if (S->InitStmt == NULL)
                return NULL;
            S->InitStmt->Expr = Init;
        }
        if (LexGetToken(P, NULL, TRUE) != TokenSemicolon)
            return NULL;
        if (LexGetToken(P, NULL, FALSE) != TokenSemicolon)
        {
            struct ParseState RS;
            ParserCopy(&RS, P);
            S->Expr = IrbCompileExpr(P, 0);
            if (S->Expr == NULL)
                return NULL;
            IrbSetReplay(S->Expr, &RS, IRB_REPLAY_COND);
        }
        if (LexGetToken(P, NULL, TRUE) != TokenSemicolon)
            return NULL;
        if (LexGetToken(P, NULL, FALSE) != TokenCloseBracket)
        {
            struct ParseState RS;
            IrExpr *Inc;
            ParserCopy(&RS, P);
            Inc = IrbCompileExpr(P, 0);
            if (Inc == NULL)
                return NULL;
            IrbSetReplay(Inc, &RS, IRB_REPLAY_INC);
            S->IncStmt = IrbNewStmt(IrStExpr, P);
            if (S->IncStmt == NULL)
                return NULL;
            S->IncStmt->Expr = Inc;
        }
        if (LexGetToken(P, NULL, TRUE) != TokenCloseBracket)
            return NULL;
        S->BodyStmt = IrbCompileStmtD(P);
        return S->BodyStmt != NULL ? S : NULL;
    }
    /* switch (expr) { ... } — same IrSt* shape as Body IR */
    if (Tok == TokenSwitch)
    {
        struct ParseState RS;
        S = IrbNewStmt(IrStSwitch, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL || LexGetToken(P, NULL, TRUE) != TokenOpenBracket)
            return NULL;
        ParserCopy(&RS, P);
        S->Expr = IrbCompileExpr(P, 0);
        if (S->Expr == NULL || LexGetToken(P, NULL, TRUE) != TokenCloseBracket)
            return NULL;
        IrbSetReplay(S->Expr, &RS, IRB_REPLAY_COND);
        S->BodyStmt = IrbCompileStmt(P); /* { ... } with case/default */
        return S->BodyStmt != NULL ? S : NULL;
    }
    if (Tok == TokenCase)
    {
        struct ParseState RS;
        S = IrbNewStmt(IrStCase, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL) return NULL;
        ParserCopy(&RS, P);
        S->Expr = IrbCompileExpr(P, 0);
        if (S->Expr == NULL || LexGetToken(P, NULL, TRUE) != TokenColon)
            return NULL;
        IrbSetReplay(S->Expr, &RS, IRB_REPLAY_COND);
        return S;
    }
    if (Tok == TokenDefault)
    {
        S = IrbNewStmt(IrStDefault, P);
        LexGetToken(P, NULL, TRUE);
        if (S == NULL || LexGetToken(P, NULL, TRUE) != TokenColon)
            return NULL;
        return S;
    }
    /* return: keep as IrStDelegate (token path) so function return value
     * is written through the normal ParseStatement path. */
    if (Tok == TokenReturn)
        return NULL;
    if (Tok == TokenElse)
        return NULL;

    /* expression statement */
    S = IrbNewStmt(IrStExpr, P);
    if (S == NULL)
        return NULL;
    {
        struct ParseState RS;
        ParserCopy(&RS, P);
        S->Expr = IrbCompileExpr(P, 0);
        if (S->Expr == NULL || LexGetToken(P, NULL, TRUE) != TokenSemicolon)
            return NULL;
        IrbSetReplay(S->Expr, &RS, IRB_REPLAY_STMT);
    }
    return S;
}

/* ---- static int32 classification (C int arithmetic wraps to 32 bit) ---- */
static int IrbClassifyExpr(IrbCode *C, IrExpr *E)
{
    int L, R;
    if (E == NULL)
        return 0;
    switch (E->Kind)
    {
        case IrExConst:
            return E->I32;
        case IrExIdent:
            E->I32 = (C->BindBase[E->BindSlot] == TypeInt);
            return E->I32;
        case IrExIndex:
            IrbClassifyExpr(C, E->Right);
            E->I32 = (C->BindElem[E->BindSlot] == TypeInt);
            return E->I32;
        case IrExMember:
            E->I32 = (C->BindElem[E->BindSlot] == TypeInt);
            return E->I32;
        case IrExUnary:
            L = IrbClassifyExpr(C, E->Left);
            E->I32 = (E->Op == TokenUnaryNot) ? 1 : L;
            return E->I32;
        case IrExBinOp:
            L = IrbClassifyExpr(C, E->Left);
            R = IrbClassifyExpr(C, E->Right);
            switch (E->Op)
            {
                case TokenLessThan: case TokenGreaterThan: case TokenLessEqual:
                case TokenGreaterEqual: case TokenEqual: case TokenNotEqual:
                case TokenLogicalAnd: case TokenLogicalOr:
                    E->I32 = 1; /* result is an int 0/1; operands are compared unwrapped */
                    break;
                default:
                    E->I32 = (L && R);
                    break;
            }
            return E->I32;
        case IrExAssign:
            L = IrbClassifyExpr(C, E->Left);
            R = IrbClassifyExpr(C, E->Right);
            E->I32 = (L && R); /* wrap flag of the arithmetic of a compound op */
            return L;
        default:
            return 0;
    }
}

static void IrbClassifyStmt(IrbCode *C, IrStmt *S)
{
    int i;
    if (S == NULL)
        return;
    IrbClassifyExpr(C, S->Expr);
    IrbClassifyStmt(C, S->ThenStmt);
    IrbClassifyStmt(C, S->ElseStmt);
    IrbClassifyStmt(C, S->BodyStmt);
    IrbClassifyStmt(C, S->InitStmt);
    IrbClassifyStmt(C, S->IncStmt);
    for (i = 0; i < S->StmtCount; i++)
        IrbClassifyStmt(C, S->Stmts[i]);
}

/* Any IrStDelegate in the body (e.g. sprintf/printf Intrinsic residual):
 * Irb loop + per-iter token delegate is slower than Body-IR/token for the
 * whole loop.  Measured on printf_loop/printf_heavy. */
static int IrbBodyHasDelegate(IrStmt *S)
{
    int i;
    if (S == NULL)
        return 0;
    if (S->Kind == IrStDelegate)
        return 1;
    if (S->Kind == IrStBlock && S->Stmts != NULL)
    {
        for (i = 0; i < S->StmtCount; i++)
            if (IrbBodyHasDelegate(S->Stmts[i]))
                return 1;
    }
    if (S->Kind == IrStIf)
        return IrbBodyHasDelegate(S->ThenStmt) || IrbBodyHasDelegate(S->ElseStmt);
    if (S->Kind == IrStFor || S->Kind == IrStWhile || S->Kind == IrStDo)
        return IrbBodyHasDelegate(S->BodyStmt);
    return 0;
}

/* one compiled unit per loop site; kind 0=for 1=while/do (cond required) */
static IrbCode *IrbGetCode(int IsFor, struct ParseState *BodyStart,
                           struct ParseState *CondStart, struct ParseState *IncStart)
{
    unsigned Key = (unsigned)(((uintptr_t)BodyStart->Pos >> 3) % IRB_CACHE_SIZE);
    IrbCode *C;
    struct ParseState P;

    for (C = IrbCache[Key]; C != NULL; C = C->Next)
        if (C->Site == (const void *)BodyStart->Pos)
            return C;

    C = (IrbCode *)calloc(1, sizeof(IrbCode));
    if (C == NULL)
        return NULL;
    C->Site = (const void *)BodyStart->Pos;
    C->Next = IrbCache[Key];
    IrbCache[Key] = C;
    IrbBuilding = C;

    ParserCopy(&P, BodyStart);
    P.Mode = RunModeRun;
    C->Body = IrbCompileStmtD(&P);
    if (C->Body != NULL && C->Body->Kind == IrStDelegate)
        C->Body = NULL; /* nothing gained */

    if (C->Body != NULL && CondStart != NULL)
    {
        ParserCopy(&P, CondStart);
        P.Mode = RunModeRun;
        if (IsFor && LexGetToken(&P, NULL, FALSE) == TokenSemicolon)
            C->Cond = NULL;
        else
        {
            struct ParseState RS;
            ParserCopy(&RS, &P);
            C->Cond = IrbCompileExpr(&P, 0);
            if (C->Cond == NULL
                || LexGetToken(&P, NULL, TRUE) != (IsFor ? TokenSemicolon : TokenCloseBracket))
                C->Body = NULL;
            else
                IrbSetReplay(C->Cond, &RS, IRB_REPLAY_COND);
        }
    }
    else if (C->Body != NULL && !IsFor)
        C->Body = NULL;

    if (C->Body != NULL && IsFor && IncStart != NULL)
    {
        ParserCopy(&P, IncStart);
        P.Mode = RunModeRun;
        if (LexGetToken(&P, NULL, FALSE) == TokenCloseBracket)
            C->Inc = NULL;
        else
        {
            struct ParseState RS;
            ParserCopy(&RS, &P);
            C->Inc = IrbCompileExpr(&P, 0);
            if (C->Inc == NULL || LexGetToken(&P, NULL, TRUE) != TokenCloseBracket)
                C->Body = NULL;
            else
                IrbSetReplay(C->Inc, &RS, IRB_REPLAY_INC);
        }
    }
    IrbBuilding = NULL;
    /* sprintf/printf-only bodies: skip Irb (measured slower than Body-IR/token) */
    if (C->Body != NULL && IrbBodyHasDelegate(C->Body))
        C->Body = NULL;
    C->Ok = (C->Body != NULL);
    return C;
}

/* resolve identifiers to Values for this activation; 0 = cannot run in IR */
static int IrbBind(struct ParseState *Parser, IrbCode *C, struct Value **B)
{
    int i;
    for (i = 0; i < C->NBind; i++)
    {
        struct Value *V = NULL;
        int Use = C->BindUse[i];
        int Base, Elem = -1;

        if (!VariableDefined(Parser->pc, C->BindName[i])
            || VariableDefinedAndOutOfScope(Parser->pc, C->BindName[i]))
            return 0;
        VariableGet(Parser->pc, Parser, C->BindName[i], &V);
        if (V == NULL || V->Typ == NULL || V->Val == NULL)
            return 0;
        Base = V->Typ->Base;
        if (Use & IRB_USE_BASE)
        {
            struct ValueType *Ft;
            /* Pointer used as both p = ... and p[i] is valid.
             * Array name used as scalar decay and a[i] is valid.
             * Refuse only when a non-array/non-pointer scalar is also an index base. */
            if ((Use & IRB_USE_R) && Base != TypePointer && Base != TypeArray)
                return 0;
            if ((Base != TypeArray && Base != TypePointer) || V->Typ->FromType == NULL)
                return 0;
            Ft = V->Typ->FromType;
            while (Ft != NULL && Ft->Base == TypeArray && Ft->FromType != NULL)
                Ft = Ft->FromType;
            if (Ft == NULL)
                return 0;
            if (!IrbIsIntBase(Ft->Base) && Ft->Base != TypeStruct
                && !(Ft->Base == TypePointer && Ft->FromType != NULL
                     && Ft->FromType->Base == TypeStruct))
                return 0;
            Elem = Ft->Base;
            if (Base == TypeArray && V->Typ->FromType->Sizeof < 1)
                return 0;
        }
        else if (Use & IRB_USE_MEMBER)
        {
            struct Value *MemberValue = NULL;
            if (Base != TypeStruct || !V->IsLValue || V->Typ->Members == NULL)
                return 0;
            if (C->BindMember[i] != NULL)
            {
                if (!TableGet(V->Typ->Members, C->BindMember[i], &MemberValue, NULL, NULL, NULL))
                    return 0;
                if (MemberValue == NULL || MemberValue->Typ == NULL)
                    return 0;
                if (MemberValue->BitWidth > 0)
                    return 0;
                if (!IrbIsIntBase(MemberValue->Typ->Base)
                    && MemberValue->Typ->Base != TypeArray
                    && MemberValue->Typ->Base != TypeStruct
                    && !(MemberValue->Typ->Base == TypePointer
                         && MemberValue->Typ->FromType != NULL
                         && MemberValue->Typ->FromType->Base == TypeStruct))
                    return 0;
                Elem = MemberValue->Typ->Base;
            }
            else
                Elem = TypeInt;
        }
        else if (Use & IRB_USE_ARROW)
        {
            struct Value *MemberValue = NULL;
            struct ValueType *St;
            if (Base != TypePointer || V->Typ->FromType == NULL)
                return 0;
            St = V->Typ->FromType;
            if (St->Base != TypeStruct || St->Members == NULL)
                return 0;
            if (C->BindMember[i] != NULL)
            {
                if (!TableGet(St->Members, C->BindMember[i], &MemberValue, NULL, NULL, NULL))
                    return 0;
                if (MemberValue == NULL || MemberValue->Typ == NULL)
                    return 0;
                if (MemberValue->BitWidth > 0)
                    return 0;
                if (!IrbIsIntBase(MemberValue->Typ->Base)
                    && MemberValue->Typ->Base != TypeArray
                    && MemberValue->Typ->Base != TypeStruct
                    && !(MemberValue->Typ->Base == TypePointer
                         && MemberValue->Typ->FromType != NULL
                         && MemberValue->Typ->FromType->Base == TypeStruct))
                    return 0;
                Elem = MemberValue->Typ->Base;
            }
            else
                Elem = TypeInt;
        }
        else
        {
            /* Scalar int, or array/pointer used as rvalue (p = a; q = p). */
            if (Base == TypeArray || Base == TypePointer)
                Elem = Base;
            else if (!IrbIsIntBase(Base) || !V->IsLValue)
                return 0;
        }
        if ((Use & IRB_USE_W) && V->IsConst)
            return 0;
        if (C->Classified)
        {
            if (C->BindBase[i] != Base || C->BindElem[i] != Elem)
                return 0;
        }
        else
        {
            C->BindBase[i] = (signed char)Base;
            C->BindElem[i] = (signed char)Elem;
        }
        B[i] = V;
    }
    if (!C->Classified)
    {
        IrbClassifyExpr(C, C->Cond);
        IrbClassifyExpr(C, C->Inc);
        IrbClassifyStmt(C, C->Body);
        C->Classified = 1;
    }
    return 1;
}

/* ---- typed memory access ---- */
static long long IrbLoadAddr(int Base, void *A)
{
    switch (Base)
    {
        case TypeInt:      return *(int *)A;
        case TypeChar:     return *(char *)A;
        case TypeShort:    return *(short *)A;
        case TypeLong:     return *(long *)A;
        case TypePointer:  return (long long)(intptr_t)(*(void **)A);
        case TypeArray:    return (long long)(intptr_t)A; /* decay: address of first elem */
        default:           return *(long long *)A;
    }
}

static long long IrbNarrow(int Base, long long V)
{
    switch (Base)
    {
        case TypeInt:      return (int)V;
        case TypeChar:     return (char)V;
        case TypeShort:    return (short)V;
        case TypeLong:     return (long)V;
        default:           return V;
    }
}

static void IrbStoreAddr(int Base, void *A, long long V)
{
    switch (Base)
    {
        case TypeInt:      *(int *)A = (int)V; break;
        case TypeChar:     *(char *)A = (char)V; break;
        case TypeShort:    *(short *)A = (short)V; break;
        case TypeLong:     *(long *)A = (long)V; break;
        case TypePointer:  *(void **)A = (void *)(intptr_t)V; break;
        default:           *(long long *)A = V; break;
    }
}

typedef struct IrbLoc {
    void *Addr;
    struct Value *Var;
    struct ValueType *Typ;  /* type of object at Addr (for residual chaining) */
    int Base;
    int Size;
    int IsElem;
    unsigned char BitWidth;   /* 0 = normal; >0 bitfield in unit at Addr */
    unsigned char BitOffset;
    unsigned char BitSigned;
} IrbLoc;

static long long IrbEval(struct ParseState *Parser, struct Value **B, IrExpr *E);

static void IrbLocate(struct ParseState *Parser, struct Value **B, IrExpr *E, IrbLoc *L)
{
    L->BitWidth = 0;
    L->BitOffset = 0;
    L->BitSigned = 0;
    if (E->Kind == IrExIdent)
    {
        struct Value *V = B[E->BindSlot];
        L->Addr = (void *)V->Val;
        L->Var = V;
        L->Typ = V->Typ;
        L->Base = V->Typ->Base;
        L->Size = 0;
        L->IsElem = 0;
    }
    else if (E->Kind == IrExMember)
    {
        /* Flat or nested member path. Walk from root leaf to E applying
         * each . / ->. Op on a node is the operator used to access that
         * field from its parent (or from the bound variable for the root). */
        struct Value *MemberValue = NULL;
        struct Value *RootVar;
        struct ValueType *Cur = NULL;
        char *BaseAddr = NULL;
        IrExpr *Stack[16];
        int N = 0, k;
        IrExpr *T;

        /* Build member chain; Left of root may be Index (a[i].m) */
        for (T = E; T != NULL && T->Kind == IrExMember && N < 16; T = T->Left)
            Stack[N++] = T;
        if (N < 1)
            IrbFail(Parser, 0, "internal Irb member chain", NULL);

        {
            IrExpr *Root = Stack[N - 1];
            if (Root->Left != NULL && Root->Left->Kind == IrExIndex)
            {
                /* a[i].m — locate the index expression first */
                IrbLoc Parent;
                IrbLocate(Parser, B, Root->Left, &Parent);
                RootVar = Parent.Var;
                if (Root->Op == TokenArrow)
                {
                    if (Parent.Typ == NULL || Parent.Typ->Base != TypePointer)
                        IrbFail(Parser, 0, "can't use '->' on non-pointer", NULL);
                    BaseAddr = *(char **)Parent.Addr;
                    if (BaseAddr == NULL)
                        IrbFail(Parser, 0, DiagnosticMsgNullPointerDereference, NULL);
                    Cur = Parent.Typ->FromType;
                }
                else
                {
                    if (Parent.Typ == NULL || Parent.Typ->Base != TypeStruct)
                        IrbFail(Parser, 0, "can't use '.' on non-struct", NULL);
                    BaseAddr = (char *)Parent.Addr;
                    Cur = Parent.Typ;
                }
            }
            else if (Root->BindSlot >= 0)
            {
                struct Value *V = B[Root->BindSlot];
                RootVar = V;
                if (Root->Op == TokenArrow)
                {
                    if (V->Typ->Base != TypePointer || V->Typ->FromType == NULL)
                        IrbFail(Parser, 0, "can't use '->' on something that's not a struct or union pointer", NULL);
                    BaseAddr = (char *)V->Val->Pointer;
                    if (BaseAddr == NULL)
                        IrbFail(Parser, 0, DiagnosticMsgNullPointerDereference, NULL);
                    Cur = V->Typ->FromType;
                }
                else
                {
                    Cur = V->Typ;
                    BaseAddr = (char *)V->Val;
                }
            }
            else
                IrbFail(Parser, 0, "internal Irb member chain", NULL);
        }

        /* Apply each field from root to E inclusive.
         * Root Op was consumed in setup (var is struct vs pointer).
         * Nested Op==Arrow means the current Cur is a pointer (e.g. s.p->v
         * after applying .p): deref before looking up the next field. */
        for (k = N - 1; k >= 0; k--)
        {
            if (k < N - 1 && Stack[k]->Op == TokenArrow)
            {
                if (Cur == NULL || Cur->Base != TypePointer || Cur->FromType == NULL)
                    IrbFail(Parser, 0, "can't use '->' on non-pointer", NULL);
                BaseAddr = *(char **)BaseAddr;
                if (BaseAddr == NULL)
                    IrbFail(Parser, 0, DiagnosticMsgNullPointerDereference, NULL);
                Cur = Cur->FromType;
            }
            if (Cur == NULL || Cur->Members == NULL
                || !TableGet(Cur->Members, Stack[k]->Ident, &MemberValue, NULL, NULL, NULL)
                || MemberValue == NULL)
                IrbFail(Parser, 0, "doesn't have a member called '%s'", Stack[k]->Ident);
            BaseAddr = BaseAddr + MemberValue->Val->Integer;
            Cur = MemberValue->Typ;
        }

        if (MemberValue == NULL || MemberValue->Typ == NULL)
            IrbFail(Parser, 0, "doesn't have a member called '%s'", E->Ident);
        L->Addr = BaseAddr;
        L->Var = RootVar;
        L->Typ = MemberValue->Typ;
        L->Base = MemberValue->Typ->Base;
        L->Size = MemberValue->Typ->Sizeof;
        L->IsElem = 1;
        L->BitWidth = MemberValue->BitWidth;
        L->BitOffset = MemberValue->BitOffset;
        L->BitSigned = 0;
        if (MemberValue->BitWidth > 0 && MemberValue->Typ != NULL)
        {
            switch (MemberValue->Typ->Base)
            {
                case TypeUnsignedInt: case TypeUnsignedShort:
                case TypeUnsignedLong: case TypeUnsignedLongLong:
                case TypeUnsignedChar:
                    L->BitSigned = 0; break;
                default:
                    L->BitSigned = 1; break;
            }
        }
    }
    else if (E->Kind == IrExIndex)
    {
        long long Idx = IrbEval(Parser, B, E->Right);
        char *Start;
        struct ValueType *Et;
        struct Value *RootVar;

        if (E->Left != NULL)
        {
            if (E->Left->Kind == IrExIndex)
            {
                /* Multi-dim a[i][j]: walk pure index chain from root array */
                IrExpr *Stack[16];
                int N = 0, k;
                IrExpr *T = E;
                struct Value *V;
                struct ValueType *Cur;
                while (T != NULL && T->Kind == IrExIndex && N < 16)
                {
                    Stack[N++] = T;
                    T = T->Left;
                }
                if (N < 1 || Stack[N - 1]->BindSlot < 0)
                    IrbFail(Parser, 0, "internal Irb multi-dim", NULL);
                V = B[Stack[N - 1]->BindSlot];
                RootVar = V;
                Cur = V->Typ;
                Start = (Cur->Base == TypeArray) ? (char *)V->Val : (char *)V->Val->Pointer;
                if (Start == NULL)
                    IrbFail(Parser, 0, DiagnosticMsgNullPointerDereference, NULL);
                for (k = N - 1; k >= 0; k--)
                {
                    long long Ix = IrbEval(Parser, B, Stack[k]->Right);
                    if (Cur->FromType == NULL)
                        IrbFail(Parser, 0, "cannot index non-array", NULL);
                    if (Cur->Base == TypeArray && Cur->ArraySize > 0
                        && (Ix < 0 || Ix >= Cur->ArraySize))
                        IrbFail(Parser, 0, DiagnosticMsgArrayIndexOutOfBounds, NULL);
                    Start = Start + (ptrdiff_t)Ix * Cur->FromType->Sizeof;
                    Cur = Cur->FromType;
                }
                L->Addr = Start;
                L->Var = RootVar;
                L->Typ = Cur;
                L->Base = Cur->Base;
                L->Size = Cur->Sizeof;
                L->IsElem = 1;
                return;
            }
            /* Residual: field[i] — Left is Member (array field) */
            {
                IrbLoc Parent;
                long long Ix = Idx;
                IrbLocate(Parser, B, E->Left, &Parent);
                if (Parent.Typ == NULL || Parent.Typ->FromType == NULL)
                    IrbFail(Parser, 0, "cannot index non-array member", NULL);
                if (Parent.Typ->Base == TypeArray)
                    Start = (char *)Parent.Addr;
                else if (Parent.Typ->Base == TypePointer)
                {
                    Start = *(char **)Parent.Addr;
                    if (Start == NULL)
                        IrbFail(Parser, 0, DiagnosticMsgNullPointerDereference, NULL);
                }
                else
                    IrbFail(Parser, 0, "cannot index non-array member", NULL);
                Et = Parent.Typ->FromType;
                L->Addr = Start + (ptrdiff_t)Ix * Et->Sizeof;
                L->Var = Parent.Var;
                L->Typ = Et;
                L->Base = Et->Base;
                L->Size = Et->Sizeof;
                L->IsElem = 1;
                return;
            }
        }

        {
            struct Value *V = B[E->BindSlot];
            RootVar = V;
            Et = V->Typ->FromType;
            if (V->Typ->Base == TypeArray && V->Typ->ArraySize > 0
                && (Idx < 0 || Idx >= V->Typ->ArraySize))
                IrbFail(Parser, 0, DiagnosticMsgArrayIndexOutOfBounds, NULL);
            if (V->Typ->Base == TypeArray)
                Start = (char *)V->Val;
            else
            {
                Start = (char *)V->Val->Pointer;
                if (Start == NULL)
                    IrbFail(Parser, 0, DiagnosticMsgNullPointerDereference, NULL);
            }
            L->Addr = Start + (ptrdiff_t)Idx * Et->Sizeof;
            L->Var = RootVar;
            L->Typ = Et;
            L->Base = Et->Base;
            L->Size = Et->Sizeof;
            L->IsElem = 1;
        }
    }
    else
        IrbFail(Parser, 0, "internal IrbLocate", NULL);
}

static unsigned long long IrbBitMask(int Width)
{
    if (Width <= 0) return 0;
    if (Width >= 32) return 0xFFFFFFFFu;
    return (1ULL << Width) - 1ULL;
}

static long long IrbLocLoad(struct ParseState *Parser, IrbLoc *L, IrExpr *E)
{
    if (!L->IsElem)
    {
        if (!L->Var->IsInitialized)
            IrbFail(Parser, 1, DiagnosticMsgUninitializedVariableNamed, E->Ident ? E->Ident : "?");
    }
    else if (L->Var->ElemInit != NULL && !L->Var->IsInitialized
             && (L->Var->Typ->Base == TypeArray || L->Var->Typ->Base == TypeStruct)
             && !ExpressionInitLeafAddr(L->Var, L->Addr, L->Size > 0 ? L->Size : 4))
        IrbFail(Parser, 1, DiagnosticMsgUninitializedVariable, NULL);
    if (L->BitWidth > 0)
    {
        unsigned int Word = 0;
        unsigned long long Raw;
        memcpy(&Word, L->Addr, sizeof(Word));
        Raw = ((unsigned long long)Word >> L->BitOffset) & IrbBitMask(L->BitWidth);
        if (L->BitSigned && L->BitWidth > 0 && (Raw & (1ULL << (L->BitWidth - 1))))
            Raw |= ~IrbBitMask(L->BitWidth);
        return (long long)Raw;
    }
    if (L->Base == TypePointer)
        return IrbLoadAddr(TypePointer, L->Addr);
    if (!IrbIsIntBase(L->Base))
        IrbFail(Parser, 0, "Irb load requires integer type", NULL);
    return IrbLoadAddr(L->Base, L->Addr);
}

static void IrbLocStore(IrbLoc *L, long long V)
{
    if (L->BitWidth > 0)
    {
        unsigned int Word = 0;
        unsigned int Mask = (unsigned int)(IrbBitMask(L->BitWidth) << L->BitOffset);
        unsigned int Field = (unsigned int)(((unsigned long long)V << L->BitOffset) & Mask);
        memcpy(&Word, L->Addr, sizeof(Word));
        Word = (Word & ~Mask) | Field;
        memcpy(L->Addr, &Word, sizeof(Word));
    }
    else
        IrbStoreAddr(L->Base, L->Addr, V);
    if (!L->IsElem)
        L->Var->IsInitialized = TRUE;
    else if (L->Var->ElemInit != NULL && !L->Var->IsInitialized
             && (L->Var->Typ->Base == TypeArray || L->Var->Typ->Base == TypeStruct))
        ExpressionInitMarkAddr(L->Var, L->Addr, L->BitWidth > 0 ? 4 : L->Size);
}

static long long IrbArith(struct ParseState *Parser, enum LexToken Op,
                          long long L, long long R, int Wrap)
{
    long long V;
    switch (Op)
    {
        case TokenPlus:     V = (long long)((unsigned long long)L + (unsigned long long)R); break;
        case TokenMinus:    V = (long long)((unsigned long long)L - (unsigned long long)R); break;
        case TokenAsterisk: V = (long long)((unsigned long long)L * (unsigned long long)R); break;
        case TokenSlash:
            if (R == 0)
                IrbFail(Parser, 1, DiagnosticMsgDivisionByZero, NULL);
            V = (R == -1) ? (long long)(0ULL - (unsigned long long)L) : L / R;
            break;
        case TokenModulus:
            if (R == 0)
                IrbFail(Parser, 1, DiagnosticMsgModuloByZero, NULL);
            V = (R == -1) ? 0 : L % R;
            break;
        case TokenAmpersand:      V = L & R; break;
        case TokenArithmeticOr:   V = L | R; break;
        case TokenArithmeticExor: V = L ^ R; break;
        case TokenShiftLeft:      V = (long long)((unsigned long long)L << (R & 63)); break;
        case TokenShiftRight:     V = L >> (R & 63); break;
        default:                  V = 0; break;
    }
    return Wrap ? (long long)(int)V : V;
}

static long long IrbEval(struct ParseState *Parser, struct Value **B, IrExpr *E)
{
    long long L, R;

    switch (E->Kind)
    {
        case IrExConst:
            return E->ConstVal;

        case IrExIdent:
        {
            struct Value *V = B[E->BindSlot];
            if (!V->IsInitialized && V->Typ->Base != TypeArray)
                IrbFail(Parser, 1, DiagnosticMsgUninitializedVariableNamed, E->Ident ? E->Ident : "?");
            if (V->Typ->Base == TypeArray)
                return (long long)(intptr_t)(void *)V->Val; /* decay */
            if (V->Typ->Base == TypePointer)
                return (long long)(intptr_t)V->Val->Pointer;
            return IrbLoadAddr(V->Typ->Base, (void *)V->Val);
        }

        case IrExIndex:
        case IrExMember:
        {
            IrbLoc Loc;
            IrbLocate(Parser, B, E, &Loc);
            return IrbLocLoad(Parser, &Loc, E);
        }

        case IrExBinOp:
            if (E->Op == TokenLogicalAnd)
                return IrbEval(Parser, B, E->Left) ? (IrbEval(Parser, B, E->Right) != 0) : 0;
            if (E->Op == TokenLogicalOr)
                return IrbEval(Parser, B, E->Left) ? 1 : (IrbEval(Parser, B, E->Right) != 0);
            L = IrbEval(Parser, B, E->Left);
            R = IrbEval(Parser, B, E->Right);
            /* Shared evaluator when no int32 wrap; else IrbArith (unsigned wrap). */
            if (!E->I32)
                return IrApplyBinOp(Parser, E->Op, L, R, E);
            switch (E->Op)
            {
                case TokenLessThan:     return L < R;
                case TokenGreaterThan:  return L > R;
                case TokenLessEqual:    return L <= R;
                case TokenGreaterEqual: return L >= R;
                case TokenEqual:        return L == R;
                case TokenNotEqual:     return L != R;
                default:                return IrbArith(Parser, E->Op, L, R, E->I32);
            }

        case IrExUnary:
        {
            if (E->Op == TokenUnaryNot)
                return IrbEval(Parser, B, E->Left) ? 0 : 1;
            if (E->Op == TokenMinus)
            {
                L = IrbEval(Parser, B, E->Left);
                L = (long long)(0ULL - (unsigned long long)L);
                return E->I32 ? (long long)(int)L : L;
            }
            if (E->Op == TokenUnaryExor)
            {
                L = ~IrbEval(Parser, B, E->Left);
                return E->I32 ? (long long)(int)L : L;
            }
            {
                /* ++ / -- , prefix (ConstVal 0) or postfix (ConstVal 1) */
                IrbLoc Loc;
                long long Old, New;
                IrbLocate(Parser, B, E->Left, &Loc);
                Old = IrbLocLoad(Parser, &Loc, E->Left);
                New = IrbNarrow(Loc.Base, (E->Op == TokenIncrement) ? Old + 1 : Old - 1);
                IrbLocStore(&Loc, New);
                return E->ConstVal ? Old : New;
            }
        }

        case IrExAssign:
        {
            IrbLoc Loc;
            long long V;
            IrbLocate(Parser, B, E->Left, &Loc);
            if (E->Op == TokenAssign)
                V = IrbEval(Parser, B, E->Right);
            else
            {
                long long Old = IrbLocLoad(Parser, &Loc, E->Left);
                R = IrbEval(Parser, B, E->Right);
                V = IrbArith(Parser, IrbAssignToBin(E->Op), Old, R, E->I32);
            }
            V = IrbNarrow(Loc.Base, V);
            IrbLocStore(&Loc, V);
            return V;
        }

        default:
            return 0;
    }
}

/* run one statement on the token path; translate the resulting Mode */
static int IrbDelegate(struct ParseState *Parser, IrStmt *S)
{
    ParserCopyPos(Parser, S->Resume);
    if (ParseStatement(Parser, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");
    if (Parser->Mode == RunModeRun)
        return IRB_EXEC_NORMAL;
    if (Parser->Mode == RunModeBreak)
    {
        Parser->Mode = RunModeRun;
        return IRB_EXEC_BREAK;
    }
    if (Parser->Mode == RunModeContinue)
    {
        Parser->Mode = RunModeRun;
        return IRB_EXEC_CONTINUE;
    }
    return IRB_EXEC_EXIT;
}

static int IrbExec(struct ParseState *Parser, struct Value **B, IrStmt *S)
{
    int i, R;

    switch (S->Kind)
    {
        case IrStExpr:
            Parser->Line = S->SourceLine;
            IRB_EVAL(Parser, B, S->Expr);
            return IRB_EXEC_NORMAL;

        case IrStBlock:
            for (i = 0; i < S->StmtCount; i++)
            {
                R = IrbExec(Parser, B, S->Stmts[i]);
                if (R != IRB_EXEC_NORMAL)
                    return R;
            }
            return IRB_EXEC_NORMAL;

        case IrStIf:
            Parser->Line = S->SourceLine;
            if (IRB_EVAL(Parser, B, S->Expr))
                return IrbExec(Parser, B, S->ThenStmt);
            if (S->ElseStmt != NULL)
                return IrbExec(Parser, B, S->ElseStmt);
            return IRB_EXEC_NORMAL;

        case IrStBreak:
            return IRB_EXEC_BREAK;
        case IrStContinue:
            return IRB_EXEC_CONTINUE;

        case IrStCase:
        case IrStDefault:
            return IRB_EXEC_NORMAL;

        case IrStReturn:
            /* Signal exit from Irb loop; Body IR / token path handles real return
             * when not inside a tight Irb loop. Inside Irb, treat as EXIT. */
            if (S->Expr != NULL)
            {
                Parser->Line = S->SourceLine;
                (void)IRB_EVAL(Parser, B, S->Expr);
            }
            Parser->Mode = RunModeReturn;
            return IRB_EXEC_EXIT;

        case IrStSwitch:
        {
            long long V;
            IrStmt *Body;
            int si, start = -1, defi = -1;
            Parser->Line = S->SourceLine;
            V = IRB_EVAL(Parser, B, S->Expr);
            Body = S->BodyStmt;
            if (Body == NULL)
                return IRB_EXEC_NORMAL;
            if (Body->Kind == IrStBlock)
            {
                for (si = 0; si < Body->StmtCount; si++)
                {
                    IrStmt *T = Body->Stmts[si];
                    if (T == NULL) continue;
                    if (T->Kind == IrStCase && T->Expr != NULL
                        && IRB_EVAL(Parser, B, T->Expr) == V)
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
                    return IRB_EXEC_NORMAL;
                for (si = start; si < Body->StmtCount; si++)
                {
                    IrStmt *T = Body->Stmts[si];
                    int R2;
                    if (T == NULL) continue;
                    if (T->Kind == IrStCase || T->Kind == IrStDefault)
                        continue;
                    R2 = IrbExec(Parser, B, T);
                    if (R2 == IRB_EXEC_BREAK)
                        return IRB_EXEC_NORMAL; /* break leaves switch, not outer loop */
                    if (R2 != IRB_EXEC_NORMAL)
                        return R2;
                }
            }
            else
            {
                int R2 = IrbExec(Parser, B, Body);
                if (R2 == IRB_EXEC_BREAK)
                    return IRB_EXEC_NORMAL;
                return R2;
            }
            return IRB_EXEC_NORMAL;
        }

        case IrStDelegate:
            return IrbDelegate(Parser, S);

        case IrStWhile:
            for (;;)
            {
                Parser->Line = S->SourceLine;
                if (!IRB_EVAL(Parser, B, S->Expr))
                    break;
                R = IrbExec(Parser, B, S->BodyStmt);
                if (R == IRB_EXEC_BREAK)
                    break;
                if (R == IRB_EXEC_EXIT)
                    return R;
            }
            return IRB_EXEC_NORMAL;

        case IrStDo:
            for (;;)
            {
                R = IrbExec(Parser, B, S->BodyStmt);
                if (R == IRB_EXEC_BREAK)
                    break;
                if (R == IRB_EXEC_EXIT)
                    return R;
                Parser->Line = S->SourceLine;
                if (!IRB_EVAL(Parser, B, S->Expr))
                    break;
            }
            return IRB_EXEC_NORMAL;

        case IrStFor:
            if (S->InitStmt != NULL)
            {
                Parser->Line = S->SourceLine;
                IRB_EVAL(Parser, B, S->InitStmt->Expr);
            }
            for (;;)
            {
                Parser->Line = S->SourceLine;
                if (S->Expr != NULL && !IRB_EVAL(Parser, B, S->Expr))
                    break;
                R = IrbExec(Parser, B, S->BodyStmt);
                if (R == IRB_EXEC_BREAK)
                    break;
                if (R == IRB_EXEC_EXIT)
                    return R;
                if (S->IncStmt != NULL)
                    IRB_EVAL(Parser, B, S->IncStmt->Expr);
            }
            return IRB_EXEC_NORMAL;

        default:
            return IRB_EXEC_NORMAL;
    }
}

/* Continue a loop whose first iteration already ran on the token path.
 * IsFor: inc; cond; body ...   while/do: cond; body ... */
static void IrbRun(struct ParseState *Parser, IrbCode *C, struct Value **B, int IsFor)
{
    int SavedLine = Parser->Line;
    for (;;)
    {
        int R;
        if (IsFor && C->Inc != NULL)
            IRB_EVAL(Parser, B, C->Inc);
        if (C->Cond != NULL && !IRB_EVAL(Parser, B, C->Cond))
            break;
        R = IrbExec(Parser, B, C->Body);
        if (R == IRB_EXEC_BREAK || R == IRB_EXEC_EXIT)
            break;
    }
    Parser->Line = SavedLine;
}

/* Try to run the rest of a loop in Irb.  Returns 1 if it did. */
int IrbTryLoop(struct ParseState *Parser, int IsFor, struct ParseState *BodyStart,
                      struct ParseState *CondStart, struct ParseState *IncStart)
{
    IrbCode *C;
    struct Value *B[IRB_MAX_BIND];
    static int Enabled = -1;

    if (Enabled < 0)
    {
        const char *Tr = getenv("BABYC_IRB_TRACE"); /* diagnostics: which loops run in Irb */
        const char *Env = getenv("BABYC_NO_IRB");   /* differential testing / bisecting */
        IrbTrace = (Tr != NULL && Tr[0] != '\0' && Tr[0] != '0');
        Enabled = (Env != NULL && Env[0] != '\0' && Env[0] != '0') ? 0 : 1;
    }
    if (!Enabled)
        return 0;

    C = IrbGetCode(IsFor, BodyStart, CondStart, IncStart);
    if (C == NULL || !C->Ok)
    {
        if (IrbTrace)
            fprintf(stderr, "[babyc-irb] line %d: not compilable\n", (int)BodyStart->Line);
        return 0;
    }
    if (!IrbBind(Parser, C, B))
    {
        if (IrbTrace)
            fprintf(stderr, "[babyc-irb] line %d: bind refused\n", (int)BodyStart->Line);
        return 0;
    }
    if (IrbTrace)
        fprintf(stderr, "[babyc-irb] line %d: engaged\n", (int)BodyStart->Line);
    IrbRun(Parser, C, B, IsFor);
    return 1;
}

