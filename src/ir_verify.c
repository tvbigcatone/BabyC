/* F2 — IrVerify: structural type-flag checks on compiled Body-IR.
 * Enabled only when BABYC_IR_VERIFY is set in the environment (test builds).
 * Does not change semantics; reports to stderr and counts violations.
 */
#include "picoc.h"
#include <stdio.h>
#include <stdlib.h>
#include "ir.h"

static int IrVerifyOn;
static int IrV_Unresolved;
static int IrV_IndexNoSize;
static int IrV_Nodes;
static int IrV_KindHist[16];

static void IrVerifyExpr(IrExpr *E)
{
    int i;
    if (E == NULL) return;
    IrV_Nodes++;
    IrVerifyExpr(E->Left);
    IrVerifyExpr(E->Right);
    if (E->Args != NULL)
        for (i = 0; i < E->ArgCount; i++)
            IrVerifyExpr(E->Args[i]);

    /* Index should carry element size after annotate for array/pointer bases */
    if (E->Kind == IrExIndex)
    {
        /* Need either concrete ElemSize or a typed result (INT/FP/PTR). */
        if (E->ElemSize <= 0
            && !(E->TypeFlags & (IR_TF_FP | IR_TF_INT | IR_TF_PTR | IR_TF_RESOLVED)))
            IrV_IndexNoSize++;
    }
    /* Value-producing nodes ideally resolved after annotate */
    if (E->Kind == IrExConst || E->Kind == IrExIdent || E->Kind == IrExBinOp
        || E->Kind == IrExUnary || E->Kind == IrExIndex || E->Kind == IrExAssign
        || E->Kind == IrExCall || E->Kind == IrExTernary || E->Kind == IrExMember)
    {
        if (!(E->TypeFlags & (IR_TF_RESOLVED | IR_TF_PTR | IR_TF_FP | IR_TF_INT)))
        {
            IrV_Unresolved++;
            if (E->Kind > 0 && E->Kind < 16) IrV_KindHist[E->Kind]++;
        }
    }
}

static void IrVerifyStmt(IrStmt *S)
{
    int i;
    if (S == NULL) return;
    IrVerifyExpr(S->Expr);
    IrVerifyStmt(S->ThenStmt);
    IrVerifyStmt(S->ElseStmt);
    IrVerifyStmt(S->BodyStmt);
    IrVerifyStmt(S->InitStmt);
    IrVerifyStmt(S->IncStmt);
    if (S->Stmts != NULL)
        for (i = 0; i < S->StmtCount; i++)
            IrVerifyStmt(S->Stmts[i]);
    if (S->DeclInitExprs != NULL)
        for (i = 0; i < S->DeclInitCount; i++)
            IrVerifyExpr(S->DeclInitExprs[i]);
}

void IrVerifyBody(IrStmt *Body, const char *FuncHint)
{
    static int boot;
    if (!boot)
    {
        IrVerifyOn = getenv("BABYC_IR_VERIFY") != NULL;
        boot = 1;
    }
    if (!IrVerifyOn || Body == NULL)
        return;
    IrV_Unresolved = 0;
    IrV_IndexNoSize = 0;
    IrV_Nodes = 0; { int _i; for (_i=0;_i<16;_i++) IrV_KindHist[_i]=0; }
    IrVerifyStmt(Body);
    if (IrV_Unresolved || IrV_IndexNoSize)
    {
        fprintf(stderr, "[IrVerify] %s nodes=%d unresolved=%d index_nosize=%d\n",
                FuncHint ? FuncHint : "?", IrV_Nodes, IrV_Unresolved, IrV_IndexNoSize);
        fprintf(stderr, "[IrVerify] kinds: Const=%d Ident=%d BinOp=%d Assign=%d Call=%d Unary=%d Index=%d Member=%d Tern=%d\n",
            IrV_KindHist[1], IrV_KindHist[2], IrV_KindHist[3], IrV_KindHist[4],
            IrV_KindHist[5], IrV_KindHist[6], IrV_KindHist[7], IrV_KindHist[8], IrV_KindHist[9]);
    }
}

void IrVerifyReportTotals(void)
{
    /* reserved for suite-end summary */
}
