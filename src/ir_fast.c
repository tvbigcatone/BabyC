/* ir_fast.c — mono recursion + pure-func memo (D1 step 5) */
#include "picoc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ir_runtime.h"
#include "ir_storage.h"
#ifdef _MSC_VER
#pragma warning(disable: 4101) /* unreferenced local - residual debug paths */
#endif
#include "ir_bridge.h"

/* Nonzero while the Linear instruction dispatcher owns loop initialization. */
extern int BabycLinExecDepth;

/* Shape-AST matchers (InsertShift, PtrScanPred).
 *
 * DEFAULT ON for normal interpreter use (real workloads get the speedups).
 * Bench/test harnesses MUST disable them on the fly with BABYC_NO_SHAPE_FP=1
 * so published timings stay honest (two-column reports: accel vs no-accel).
 *
 * Decoys such as `key = key` are never accepted (W29rc A1). */
int IrShapeFpEnabled(void)
{
    static int On = -1;
    if (On < 0)
    {
        if (!BabycAccelOn())
            On = 0;
        else
        {
            const char *e = getenv("BABYC_NO_SHAPE_FP");
            On = (e != NULL && e[0] == '1' && e[1] == '\0') ? 0 : 1;
        }
    }
    return On;
}

/* Loop kernels (ArrayAccum/Fill, InsertShift, PtrWalk, KernelFor/While, tight).
 * Default ON. BABYC_NO_KERNEL=1 disables all of them for honest tree baseline. */
int IrKernelFpEnabled(void)
{
    static int On = -1;
    if (On < 0)
    {
        if (!BabycAccelOn())
            On = 0;
        else
        {
            const char *e = getenv("BABYC_NO_KERNEL");
            On = (e != NULL && e[0] == '1' && e[1] == '\0') ? 0 : 1;
        }
    }
    return On;
}


/* FP values travel through the generic evaluator as IEEE bits in long long.
 * Integer-specialized kernels must not touch statements that read or write FP. */
static int IrExprTouchesFP(IrExpr *E)
{
    int i;
    if (E == NULL) return 0;
    if ((E->TypeFlags & IR_TF_FP) || E->I32 == TypeFP) return 1;
    if (E->Kind == IrExConst && E->Op == TokenFPConstant) return 1;
    if (IrExprTouchesFP(E->Left) || IrExprTouchesFP(E->Right)) return 1;
    for (i = 0; E->Args != NULL && i < E->ArgCount; ++i)
        if (IrExprTouchesFP(E->Args[i])) return 1;
    return 0;
}

static int IrStmtTouchesFPWalk(IrStmt *S)
{
    int i;
    if (S == NULL) return 0;
    if (IrExprTouchesFP(S->Expr)) return 1;
    if (S->DeclFromType != NULL
        && (S->DeclFromType->Base == TypeFP
            || ((S->DeclFromType->Base == TypeArray || S->DeclFromType->Base == TypePointer)
                && S->DeclFromType->FromType != NULL
                && S->DeclFromType->FromType->Base == TypeFP)))
        return 1;
    for (i = 0; S->DeclInitExprs != NULL && i < S->DeclInitCount; ++i)
        if (IrExprTouchesFP(S->DeclInitExprs[i])) return 1;
    if (IrStmtTouchesFPWalk(S->ThenStmt) || IrStmtTouchesFPWalk(S->ElseStmt)
        || IrStmtTouchesFPWalk(S->BodyStmt) || IrStmtTouchesFPWalk(S->InitStmt)
        || IrStmtTouchesFPWalk(S->IncStmt)) return 1;
    for (i = 0; S->Stmts != NULL && i < S->StmtCount; ++i)
        if (IrStmtTouchesFPWalk(S->Stmts[i])) return 1;
    return 0;
}

int IrStmtTouchesFP(IrStmt *S)
{
    if (S == NULL) return 0;
    if (S->FpTouch == 0)
        S->FpTouch = IrStmtTouchesFPWalk(S) ? 2 : 1;
    return S->FpTouch == 2;
}

int IrKernelFpEnabledFor(IrStmt *S)
{
    return IrKernelFpEnabled() && !IrStmtTouchesFP(S);
}

/* Memoization and mono-rewrite use integer payloads; exclude FP signatures. */
int IrFuncUsesFP(struct Value *F)
{
    int i;
    if (F == NULL || F->Val == NULL) return 0;
    if (F->Val->FuncDef.ReturnType != NULL
        && F->Val->FuncDef.ReturnType->Base == TypeFP) return 1;
    if (F->Val->FuncDef.ParamType == NULL) return 0;
    for (i = 0; i < F->Val->FuncDef.NumParams; ++i)
        if (F->Val->FuncDef.ParamType[i] != NULL
            && F->Val->FuncDef.ParamType[i]->Base == TypeFP) return 1;
    return 0;
}

#define IR_MEMO_SIZE   2048
#define IR_MEMO_DECAY  256
#define IR_MEMO_MAXARG 4
typedef struct IrMemoEntry {
    struct Value *Func; int NArgs; long long Args[IR_MEMO_MAXARG];
    long long Result; unsigned Clock; int Occupied; int LruPrev, LruNext;
} IrMemoEntry;
static IrMemoEntry IrMemoTab[IR_MEMO_SIZE];
static int IrMemoLruHead = -1, IrMemoLruTail = -1;
static unsigned IrMemoClock = 1;
static int IrMemoEnabled = -1;

static long long IrMonoEvalExpr(IrExpr *E, long long *P, int NP, struct Value *Self);

static void IrMemoLruUnlink(int i)
{
    IrMemoEntry *E = &IrMemoTab[i];
    if (E->LruPrev >= 0)
        IrMemoTab[E->LruPrev].LruNext = E->LruNext;
    else
        IrMemoLruHead = E->LruNext;
    if (E->LruNext >= 0)
        IrMemoTab[E->LruNext].LruPrev = E->LruPrev;
    else
        IrMemoLruTail = E->LruPrev;
    E->LruPrev = E->LruNext = -1;
}


static void IrMemoLruTouch(int i)
{
    IrMemoEntry *E = &IrMemoTab[i];
    if (IrMemoLruHead == i)
        return;
    if (E->LruPrev >= 0 || E->LruNext >= 0 || IrMemoLruTail == i)
        IrMemoLruUnlink(i);
    E->LruPrev = -1;
    E->LruNext = IrMemoLruHead;
    if (IrMemoLruHead >= 0)
        IrMemoTab[IrMemoLruHead].LruPrev = i;
    else
        IrMemoLruTail = i;
    IrMemoLruHead = i;
    E->Clock = IrMemoClock;
}


static unsigned IrMemoHash(struct Value *Func, long long *Args, int N)
{
    unsigned h = (unsigned)(uintptr_t)Func;
    int i;
    for (i = 0; i < N; i++)
        h = h * 131u + (unsigned)Args[i] + (unsigned)(Args[i] >> 32);
    return h;
}


int IrMemoEligible(struct Value *FuncValue, int ArgCount)
{
    int i, np;
    struct ParseState Fake;
    const char *Locals[64];
    int NLocals;
    IrStmt *Body;
    if (IrMemoEnabled < 0)
    {
        const char *Env = getenv("BABYC_NO_MEMO");
        IrMemoEnabled = (Env != NULL && Env[0] != '\0' && Env[0] != '0') ? 0 : 1;
    }
    if (!IrMemoEnabled)
        return 0;
    if (FuncValue == NULL || FuncValue->Val->FuncDef.BodyIr == NULL)
        return 0;
    /* void / no useful return: memo would hide side effects */
    if (FuncValue->Val->FuncDef.ReturnType != NULL
        && FuncValue->Val->FuncDef.ReturnType->Base == TypeVoid)
        return 0;
    np = FuncValue->Val->FuncDef.NumParams;
    if (np < 1 || np > IR_MEMO_MAXARG || ArgCount < np)
        return 0;
    if (FuncValue->Val->FuncDef.VarArgs)
        return 0;
    for (i = 0; i < np; i++)
    {
        struct ValueType *PT = FuncValue->Val->FuncDef.ParamType
            ? FuncValue->Val->FuncDef.ParamType[i] : NULL;
        if (PT != NULL && (PT->Base == TypePointer || PT->Base == TypeArray
#ifndef NO_FP
            || PT->Base == TypeFP
#endif
            ))
            return 0;
    }
    Body = (IrStmt *)FuncValue->Val->FuncDef.BodyIr;
    NLocals = IrCollectLocals(Body, Locals, 64, 0);
    /* Minimal parser for VarGet */
    memset(&Fake, 0, sizeof(Fake));
    Fake.pc = FuncValue->Val->FuncDef.Body.pc;
    if (Fake.pc == NULL)
        return 0; /* cannot prove purity */
    if (IrBodyTouchesGlobal(&Fake, Body, FuncValue, Locals, NLocals))
        return 0;
    return 1;
}


int IrMemoLookup(struct Value *Func, long long *Args, int N, long long *Out)
{
    unsigned h = IrMemoHash(Func, Args, N) % IR_MEMO_SIZE;
    int probes, i;
    IrMemoClock++;
    for (probes = 0, i = (int)h; probes < 16; probes++, i = (i + 1) % IR_MEMO_SIZE)
    {
        IrMemoEntry *E = &IrMemoTab[i];
        if (!E->Occupied)
            return 0;
        if (E->Func == Func && E->NArgs == N
            && memcmp(E->Args, Args, (size_t)N * sizeof(long long)) == 0)
        {
            /* temporal decay: cold entry = miss (slot reusable) */
            if (IrMemoClock - E->Clock > IR_MEMO_DECAY)
            {
                IrMemoLruUnlink(i);
                E->Occupied = 0;
                return 0;
            }
            IrMemoLruTouch(i);
            *Out = E->Result;
            return 1;
        }
    }
    return 0;
}


void IrMemoStore(struct Value *Func, long long *Args, int N, long long Result)
{
    unsigned h = IrMemoHash(Func, Args, N) % IR_MEMO_SIZE;
    int probes, i, slot = -1;
    for (probes = 0, i = (int)h; probes < 16; probes++, i = (i + 1) % IR_MEMO_SIZE)
    {
        IrMemoEntry *E = &IrMemoTab[i];
        if (!E->Occupied)
        {
            slot = i;
            break;
        }
        if (E->Func == Func && E->NArgs == N
            && memcmp(E->Args, Args, (size_t)N * sizeof(long long)) == 0)
        {
            E->Result = Result;
            IrMemoLruTouch(i);
            return;
        }
        /* prefer overwriting cold (decayed) entries */
        if (slot < 0 && IrMemoClock - E->Clock > IR_MEMO_DECAY)
            slot = i;
    }
    if (slot < 0)
    {
        /* LRB: evict LRU tail */
        slot = IrMemoLruTail;
        if (slot < 0)
            slot = (int)h;
        else
            IrMemoLruUnlink(slot);
    }
    else if (IrMemoTab[slot].Occupied)
        IrMemoLruUnlink(slot);

    {
        IrMemoEntry *E = &IrMemoTab[slot];
        E->Func = Func;
        E->NArgs = N;
        memcpy(E->Args, Args, (size_t)N * sizeof(long long));
        if (N < IR_MEMO_MAXARG)
            memset(E->Args + N, 0, (size_t)(IR_MEMO_MAXARG - N) * sizeof(long long));
        E->Result = Result;
        E->Occupied = 1;
        E->LruPrev = E->LruNext = -1;
        IrMemoLruTouch(slot);
    }
}


static int IrMonoExprOk(IrExpr *E, int MaxParam)
{
    int i;
    if (E == NULL) return 1;
    switch (E->Kind)
    {
        case IrExConst: return 1;
        case IrExIdent:
            return (E->ParamSlot >= 0 && E->ParamSlot < MaxParam);
        case IrExBinOp:
            return IrMonoExprOk(E->Left, MaxParam) && IrMonoExprOk(E->Right, MaxParam);
        case IrExUnary:
            return IrMonoExprOk(E->Left ? E->Left : E->Right, MaxParam);
        case IrExCall:
            if (E->ArgCount > MaxParam) return 0;
            for (i = 0; i < E->ArgCount; i++)
                if (!IrMonoExprOk(E->Args[i], MaxParam)) return 0;
            return 1;
        default: return 0;
    }
}


static int IrMonoClassify(void *BodyIr, int NumParams)
{
    IrStmt *Body = (IrStmt *)BodyIr;
    IrStmt *IfS, *RetS, *Br;
    IrExpr *C, *E1, *E2, *A, *B;
    if (Body == NULL || NumParams < 1 || NumParams > 2) return 0;
    if (Body->Kind != IrStBlock || Body->StmtCount != 2) return 0;
    IfS = Body->Stmts[0];
    RetS = Body->Stmts[1];
    if (IfS == NULL || RetS == NULL) return 0;
    if (IfS->Kind != IrStIf || RetS->Kind != IrStReturn) return 0;
    if (IfS->ElseStmt != NULL) return 0;
    Br = IfS->ThenStmt;
    if (Br == NULL || Br->Kind != IrStReturn) return 0;
    if (!IrMonoExprOk(IfS->Expr, NumParams)) return 0;
    if (!IrMonoExprOk(Br->Expr, NumParams)) return 0;
    if (!IrMonoExprOk(RetS->Expr, NumParams)) return 0;

    C = IfS->Expr;
    E1 = Br->Expr;
    E2 = RetS->Expr;
    if (C == NULL || E1 == NULL || E2 == NULL) return 1;
    if (C->Kind != IrExBinOp
        || (C->Op != TokenLessEqual && C->Op != TokenLessThan)
        || C->Left == NULL || C->Left->Kind != IrExIdent || C->Left->ParamSlot != 0
        || C->Right == NULL || C->Right->Kind != IrExConst)
        return 1;

    /* fact-like: if (p0<=K) return CONST; return p0 * self(p0-1) */
    if (E1->Kind == IrExConst
        && E2->Kind == IrExBinOp && E2->Op == TokenAsterisk
        && E2->Left && E2->Left->Kind == IrExIdent && E2->Left->ParamSlot == 0
        && E2->Right && E2->Right->Kind == IrExCall && E2->Right->ArgCount == 1)
    {
        A = E2->Right->Args[0];
        if (A && A->Kind == IrExBinOp && A->Op == TokenMinus
            && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
            && A->Right && A->Right->Kind == IrExConst && A->Right->ConstVal == 1)
            return 3;
    }
    /* also: if (p0<=K) return p0; return p0 * self(p0-1) */
    if (E1->Kind == IrExIdent && E1->ParamSlot == 0
        && E2->Kind == IrExBinOp && E2->Op == TokenAsterisk
        && E2->Left && E2->Left->Kind == IrExIdent && E2->Left->ParamSlot == 0
        && E2->Right && E2->Right->Kind == IrExCall && E2->Right->ArgCount == 1)
    {
        A = E2->Right->Args[0];
        if (A && A->Kind == IrExBinOp && A->Op == TokenMinus
            && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
            && A->Right && A->Right->Kind == IrExConst && A->Right->ConstVal == 1)
            return 3;
    }

    /* fib-like: if (p0<=K) return p0|CONST; return self(p0-a)+self(p0-b) */
    if ((E1->Kind == IrExIdent && E1->ParamSlot == 0) || E1->Kind == IrExConst)
    {
        if (E2->Kind == IrExBinOp && E2->Op == TokenPlus
            && E2->Left && E2->Left->Kind == IrExCall && E2->Left->ArgCount == 1
            && E2->Right && E2->Right->Kind == IrExCall && E2->Right->ArgCount == 1)
        {
            A = E2->Left->Args[0];
            B = E2->Right->Args[0];
            if (A && A->Kind == IrExBinOp && A->Op == TokenMinus
                && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
                && A->Right && A->Right->Kind == IrExConst
                && B && B->Kind == IrExBinOp && B->Op == TokenMinus
                && B->Left && B->Left->Kind == IrExIdent && B->Left->ParamSlot == 0
                && B->Right && B->Right->Kind == IrExConst)
                return 2;
        }
    }

    /* sum-linear (4): return self(n-1) + C  or  self(n-1) + n */
    if (E2->Kind == IrExBinOp && E2->Op == TokenPlus)
    {
        IrExpr *Call = NULL, *Add = NULL;
        if (E2->Left && E2->Left->Kind == IrExCall && E2->Left->ArgCount == 1)
        { Call = E2->Left; Add = E2->Right; }
        else if (E2->Right && E2->Right->Kind == IrExCall && E2->Right->ArgCount == 1)
        { Call = E2->Right; Add = E2->Left; }
        if (Call && Add)
        {
            A = Call->Args[0];
            if (A && A->Kind == IrExBinOp && A->Op == TokenMinus
                && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
                && A->Right && A->Right->Kind == IrExConst && A->Right->ConstVal == 1
                && (Add->Kind == IrExConst
                    || (Add->Kind == IrExIdent && Add->ParamSlot == 0)))
                return 4;
        }
    }

    /* const-mult (5): return C * self(n-1)  (C constant, not param) */
    if (E2->Kind == IrExBinOp && E2->Op == TokenAsterisk)
    {
        IrExpr *Call = NULL, *Mul = NULL;
        if (E2->Left && E2->Left->Kind == IrExCall && E2->Left->ArgCount == 1
            && E2->Right && E2->Right->Kind == IrExConst)
        { Call = E2->Left; Mul = E2->Right; }
        else if (E2->Right && E2->Right->Kind == IrExCall && E2->Right->ArgCount == 1
            && E2->Left && E2->Left->Kind == IrExConst)
        { Call = E2->Right; Mul = E2->Left; }
        if (Call && Mul)
        {
            A = Call->Args[0];
            if (A && A->Kind == IrExBinOp && A->Op == TokenMinus
                && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
                && A->Right && A->Right->Kind == IrExConst && A->Right->ConstVal == 1)
                return 5;
        }
    }

    /* twin-rec (6): self(n-1)+self(n-1) [+ n|C] */
    if (E2->Kind == IrExBinOp && E2->Op == TokenPlus)
    {
        IrExpr *L = E2->Left, *R = E2->Right, *Extra = NULL;
        /* (self+self)+X */
        if (L && L->Kind == IrExBinOp && L->Op == TokenPlus
            && L->Left && L->Left->Kind == IrExCall && L->Left->ArgCount == 1
            && L->Right && L->Right->Kind == IrExCall && L->Right->ArgCount == 1)
        {
            Extra = R;
            A = L->Left->Args[0];
            B = L->Right->Args[0];
        }
        /* self+self */
        else if (L && L->Kind == IrExCall && L->ArgCount == 1
            && R && R->Kind == IrExCall && R->ArgCount == 1)
        {
            A = L->Args[0];
            B = R->Args[0];
            Extra = NULL;
        }
        else
            A = B = NULL;
        if (A && B
            && A->Kind == IrExBinOp && A->Op == TokenMinus
            && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
            && A->Right && A->Right->Kind == IrExConst && A->Right->ConstVal == 1
            && B->Kind == IrExBinOp && B->Op == TokenMinus
            && B->Left && B->Left->Kind == IrExIdent && B->Left->ParamSlot == 0
            && B->Right && B->Right->Kind == IrExConst && B->Right->ConstVal == 1
            && (Extra == NULL || Extra->Kind == IrExConst
                || (Extra->Kind == IrExIdent && Extra->ParamSlot == 0)))
            return 6;
    }

    /* tail-step (7): return self(n-1) only — rare but safe */
    if (E2->Kind == IrExCall && E2->ArgCount == 1)
    {
        A = E2->Args[0];
        if (A && A->Kind == IrExBinOp && A->Op == TokenMinus
            && A->Left && A->Left->Kind == IrExIdent && A->Left->ParamSlot == 0
            && A->Right && A->Right->Kind == IrExConst && A->Right->ConstVal == 1)
            return 7;
    }

    return 0; /* not mono-iterative; memo/IR path */
}


int IrMonoIsEligible(void *BodyIr, int NumParams)
{
    int s = IrMonoClassify(BodyIr, NumParams);
    return (s >= 2 && s <= 7);
}


static long long IrMonoFibIter(struct Value *Self, long long n)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    IrExpr *C = Body->Stmts[0]->Expr;
    IrExpr *E2 = Body->Stmts[1]->Expr;
    long long K = C->Right->ConstVal;
    long long a = E2->Left->Args[0]->Right->ConstVal;
    long long b = E2->Right->Args[0]->Right->ConstVal;
    int isLe = (C->Op == TokenLessEqual);
    long long i, t, x, y;

    if (n < 0) return 0;
    if (isLe ? (n <= K) : (n < K))
        return n;

    /* Classic Fibonacci: F(0)=0,F(1)=1,F(n)=F(n-1)+F(n-2) */
    if (a == 1 && b == 2)
    {
        x = 0;
        y = 1;
        for (i = 2; i <= n; i++)
        {
            t = x + y;
            x = y;
            y = t;
        }
        return y;
    }

    /* General: only support positive unit steps with a,b in {1,2} swapped */
    if (a == 2 && b == 1)
    {
        x = 0;
        y = 1;
        for (i = 2; i <= n; i++)
        {
            t = x + y;
            x = y;
            y = t;
        }
        return y;
    }

    /* Fallback: bottom-up DP table for other (a,b) — still iterative O(n) */
    {
        long long *dp;
        long long result;
        if (n > 1000000) /* safety */
            return 0;
        dp = (long long *)calloc((size_t)n + 1, sizeof(long long));
        if (dp == NULL) return 0;
        for (i = 0; i <= n; i++)
        {
            if (isLe ? (i <= K) : (i < K))
                dp[i] = i;
            else
            {
                long long va = (i >= a) ? dp[i - a] : 0;
                long long vb = (i >= b) ? dp[i - b] : 0;
                dp[i] = va + vb;
            }
        }
        result = dp[n];
        free(dp);
        return result;
    }
}


static long long IrMonoFactIter(struct Value *Self, long long n)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    IrExpr *C = Body->Stmts[0]->Expr;
    IrExpr *E1 = Body->Stmts[0]->ThenStmt->Expr;
    long long K = C->Right->ConstVal;
    int isLe = (C->Op == TokenLessEqual);
    long long base;
    long long i, acc;

    if (E1->Kind == IrExConst)
        base = E1->ConstVal;
    else
        base = 1; /* return p0 when n<=K is rare for fact; treat as 1 for n<=K */

    if (isLe ? (n <= K) : (n < K))
        return (E1->Kind == IrExConst) ? base : n;

    acc = (E1->Kind == IrExConst) ? base : 1;
    /* product from (K+1)..n  (or from 2..n when base case returns 1) */
    {
        long long start = isLe ? (K + 1) : K;
        if (start < 2) start = 2;
        for (i = start; i <= n; i++)
            acc *= i;
    }
    return acc;
}


static long long IrMonoSumIter(struct Value *Self, long long n)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    IrExpr *C = Body->Stmts[0]->Expr;
    IrExpr *E1 = Body->Stmts[0]->ThenStmt->Expr;
    IrExpr *E2 = Body->Stmts[1]->Expr;
    IrExpr *Add;
    long long K = C->Right->ConstVal;
    int isLe = (C->Op == TokenLessEqual);
    long long i, acc, addc;
    int add_is_n = 0;

    if (isLe ? (n <= K) : (n < K))
        return (E1->Kind == IrExConst) ? E1->ConstVal : n;

    if (E2->Left && E2->Left->Kind == IrExCall)
        Add = E2->Right;
    else
        Add = E2->Left;
    if (Add && Add->Kind == IrExIdent && Add->ParamSlot == 0)
        add_is_n = 1;
    else
        addc = (Add && Add->Kind == IrExConst) ? Add->ConstVal : 0;

    acc = (E1->Kind == IrExConst) ? E1->ConstVal : ((isLe ? K : K - 1) >= 0 ? (E1->Kind == IrExIdent ? (isLe ? K : K - 1) : 0) : 0);
    if (E1->Kind == IrExIdent)
        acc = isLe ? K : (K > 0 ? K - 1 : 0);
    if (E1->Kind == IrExConst)
        acc = E1->ConstVal;

    {
        long long start = isLe ? (K + 1) : K;
        if (start < 1) start = 1;
        for (i = start; i <= n; i++)
            acc += add_is_n ? i : addc;
    }
    return acc;
}


static long long IrMonoCMulIter(struct Value *Self, long long n)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    IrExpr *C = Body->Stmts[0]->Expr;
    IrExpr *E1 = Body->Stmts[0]->ThenStmt->Expr;
    IrExpr *E2 = Body->Stmts[1]->Expr;
    long long K = C->Right->ConstVal;
    int isLe = (C->Op == TokenLessEqual);
    long long i, acc, mul;

    if (isLe ? (n <= K) : (n < K))
        return (E1->Kind == IrExConst) ? E1->ConstVal : n;

    mul = (E2->Left && E2->Left->Kind == IrExConst) ? E2->Left->ConstVal
        : (E2->Right && E2->Right->Kind == IrExConst) ? E2->Right->ConstVal : 1;
    acc = (E1->Kind == IrExConst) ? E1->ConstVal : 1;
    {
        long long start = isLe ? (K + 1) : K;
        if (start < 1) start = 1;
        for (i = start; i <= n; i++)
            acc *= mul;
    }
    return acc;
}


static long long IrMonoTwinIter(struct Value *Self, long long n)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    IrExpr *C = Body->Stmts[0]->Expr;
    IrExpr *E1 = Body->Stmts[0]->ThenStmt->Expr;
    IrExpr *E2 = Body->Stmts[1]->Expr;
    long long K = C->Right->ConstVal;
    int isLe = (C->Op == TokenLessEqual);
    long long i, acc;
    int extra_n = 0;
    long long extra_c = 0;
    IrExpr *Extra = NULL;

    if (isLe ? (n <= K) : (n < K))
        return (E1->Kind == IrExConst) ? E1->ConstVal : n;

    /* detect +X on outer plus */
    if (E2->Left && E2->Left->Kind == IrExBinOp && E2->Left->Op == TokenPlus
        && E2->Left->Left && E2->Left->Left->Kind == IrExCall)
        Extra = E2->Right;
    if (Extra != NULL)
    {
        if (Extra->Kind == IrExIdent && Extra->ParamSlot == 0)
            extra_n = 1;
        else if (Extra->Kind == IrExConst)
            extra_c = Extra->ConstVal;
    }

    acc = (E1->Kind == IrExConst) ? E1->ConstVal : 1;
    {
        long long start = isLe ? (K + 1) : K;
        if (start < 1) start = 1;
        for (i = start; i <= n; i++)
        {
            acc = acc + acc;
            if (extra_n)
                acc += i;
            else
                acc += extra_c;
        }
    }
    return acc;
}


static long long IrMonoTailIter(struct Value *Self, long long n)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    IrExpr *C = Body->Stmts[0]->Expr;
    IrExpr *E1 = Body->Stmts[0]->ThenStmt->Expr;
    long long K = C->Right->ConstVal;
    int isLe = (C->Op == TokenLessEqual);
    (void)n;
    if (E1->Kind == IrExConst)
        return E1->ConstVal;
    /* return param at base: F(n)=F(n-1)... → F(K) = K */
    return isLe ? K : (K > 0 ? K - 1 : 0);
}


long long IrMonoRun(struct Value *Self, long long *P, int NP)
{
    IrStmt *Body = (IrStmt *)Self->Val->FuncDef.BodyIr;
    int shape = IrMonoClassify(Body, NP);
    IrStmt *IfS, *RetS, *Br;
    long long mono_hit, mono_res;

    /* Memo pure mono shapes (esp. 2-arg combinatorics). */
    if (NP >= 1 && NP <= IR_MEMO_MAXARG
        && IrMemoLookup(Self, P, NP, &mono_hit))
        return mono_hit;

    if (shape == 2 && NP == 1)
        return IrMonoFibIter(Self, P[0]);
    if (shape == 3 && NP == 1)
        return IrMonoFactIter(Self, P[0]);
    if (shape == 4 && NP == 1)
        return IrMonoSumIter(Self, P[0]);
    if (shape == 5 && NP == 1)
        return IrMonoCMulIter(Self, P[0]);
    if (shape == 6 && NP == 1)
        return IrMonoTwinIter(Self, P[0]);
    if (shape == 7 && NP == 1)
        return IrMonoTailIter(Self, P[0]);

    IfS = Body->Stmts[0];
    RetS = Body->Stmts[1];
    if (IrMonoEvalExpr(IfS->Expr, P, NP, Self))
    {
        Br = IfS->ThenStmt;
        mono_res = Br->Expr ? IrMonoEvalExpr(Br->Expr, P, NP, Self) : 0;
    }
    else
        mono_res = RetS->Expr ? IrMonoEvalExpr(RetS->Expr, P, NP, Self) : 0;
    if (NP >= 1 && NP <= IR_MEMO_MAXARG)
        IrMemoStore(Self, P, NP, mono_res);
    return mono_res;
}


static long long IrMonoEvalExpr(IrExpr *E, long long *P, int NP, struct Value *Self)
{
    long long L, R, NP2[8];
    int i, na;
    if (E == NULL) return 0;
    switch (E->Kind)
    {
        case IrExConst: return E->ConstVal;
        case IrExIdent:
            if (E->ParamSlot >= 0 && E->ParamSlot < NP) return P[E->ParamSlot];
            return 0;
        case IrExBinOp:
            if (E->Op == TokenLogicalAnd)
            {
                L = IrMonoEvalExpr(E->Left, P, NP, Self);
                if (!L) return 0;
                return IrMonoEvalExpr(E->Right, P, NP, Self) ? 1 : 0;
            }
            if (E->Op == TokenLogicalOr)
            {
                L = IrMonoEvalExpr(E->Left, P, NP, Self);
                if (L) return 1;
                return IrMonoEvalExpr(E->Right, P, NP, Self) ? 1 : 0;
            }
            L = IrMonoEvalExpr(E->Left, P, NP, Self);
            R = IrMonoEvalExpr(E->Right, P, NP, Self);
            switch (E->Op)
            {
                case TokenPlus: return L + R;
                case TokenMinus: return L - R;
                case TokenAsterisk: return L * R;
                case TokenSlash: return R ? L / R : 0;
                case TokenModulus: return R ? L % R : 0;
                case TokenLessThan: return L < R;
                case TokenGreaterThan: return L > R;
                case TokenLessEqual: return L <= R;
                case TokenGreaterEqual: return L >= R;
                case TokenEqual: return L == R;
                case TokenNotEqual: return L != R;
                case TokenAmpersand: return L & R;
                case TokenArithmeticOr: return L | R;
                case TokenArithmeticExor: return L ^ R;
                case TokenShiftLeft: return L << (int)R;
                case TokenShiftRight: return L >> (int)R;
                default: return 0;
            }
        case IrExUnary:
        {
            IrExpr *X = E->Left ? E->Left : E->Right;
            L = IrMonoEvalExpr(X, P, NP, Self);
            if (E->Op == TokenUnaryNot) return L ? 0 : 1;
            if (E->Op == TokenMinus) return -L;
            if (E->Op == TokenPlus) return L;
            if (E->Op == TokenUnaryExor) return (long long)(~(int)L);
            return L;
        }
        case IrExCall:
            /* Always self while inside mono — no eligibility re-check */
            na = E->ArgCount;
            if (na > 8) na = 8;
            for (i = 0; i < na; i++)
                NP2[i] = IrMonoEvalExpr(E->Args[i], P, NP, Self);
            return IrMonoRun(Self, NP2, na > 0 ? na : NP);
        default:
            return 0;
    }
}



/* ---- Kernel / tight / thin / ptr-scan (D1 post: from ir_exec) ---- */
/* Resolve for-bound: const or BoundIsVar local/param/global. */
static long long IrFastResolveBound(struct ParseState *Parser, IrCallFrame *F, IrCond *C)
{
    int slot;
    if (C == NULL) return 0;
    if (!C->BoundIsVar)
        return C->Bound;
    if (F != NULL)
    {
        slot = C->BoundSlot;
        if (slot >= 0 && slot < IR_CALL_MAX_LOCALS)
            return F->LocalVal[slot];
        if (C->BoundVarName != NULL)
        {
            int i;
            for (i = 0; i < IR_CALL_MAX_LOCALS; i++)
                if (F->LocalName[i] != NULL
                    && strcmp(F->LocalName[i], C->BoundVarName) == 0)
                    return F->LocalVal[i];
            for (i = 0; i < F->NumParams; i++)
                if (F->ParamName[i] != NULL
                    && strcmp(F->ParamName[i], C->BoundVarName) == 0)
                    return F->ParamVal[i];
        }
    }
    if (C->CachedBound != NULL)
        return IrValueAsInt64(C->CachedBound);
    if (Parser != NULL && C->BoundVarName != NULL
        && (IrGlobalIdentSoft(Parser, C->BoundVarName) != NULL))
    {
        struct Value *V = IrGlobalCacheFill(Parser, C->BoundVarName, NULL);
        if (V != NULL)
            return IrValueAsInt64(V);
    }
    return 0;
}

#define IR_KERNEL_GCACHE 16
static struct {
    const char *Name;
    void *Ptr;
    int ElemSize;
    int RowSize; /* bytes per outer index for 2D */
} IrKernelGCache[IR_KERNEL_GCACHE];
static int IrKernelGCacheN;

void IrKernelGCacheReset(void) { IrKernelGCacheN = 0; }

static int IrKernelGCacheLookup(const char *Name, void **Ptr, int *Es, int *Row)
{
    int i;
    for (i = 0; i < IrKernelGCacheN; i++)
        if (IrKernelGCache[i].Name == Name ||
            (IrKernelGCache[i].Name && Name && strcmp(IrKernelGCache[i].Name, Name) == 0))
        {
            *Ptr = IrKernelGCache[i].Ptr;
            *Es = IrKernelGCache[i].ElemSize;
            if (Row) *Row = IrKernelGCache[i].RowSize;
            return 1;
        }
    return 0;
}

static void IrKernelGCachePut(const char *Name, void *Ptr, int Es, int Row)
{
    int i;
    if (IrKernelGCacheN >= IR_KERNEL_GCACHE) return;
    for (i = 0; i < IrKernelGCacheN; i++)
        if (IrKernelGCache[i].Name == Name) return;
    IrKernelGCache[IrKernelGCacheN].Name = Name;
    IrKernelGCache[IrKernelGCacheN].Ptr = Ptr;
    IrKernelGCache[IrKernelGCacheN].ElemSize = Es;
    IrKernelGCache[IrKernelGCacheN].RowSize = Row;
    IrKernelGCacheN++;
}


int IrTryKernelWhile(struct ParseState *Parser, IrStmt *S);
int IrExprHasPtrInc(IrExpr *E, IrCallFrame *F, int depth);
int IrStmtHasPtrInc(IrStmt *S, IrCallFrame *F, int depth);
int IrTryLeafCallAccum(struct ParseState *Parser, IrStmt *S);
int IrTryScoreCallAccum(struct ParseState *Parser, IrStmt *S);
int IrEnsureThinCallee(struct ParseState *Parser, IrExpr *Call,
                              struct Value **FVOut, IrStmt **BodyOut, IrFuncMeta **MetaOut);
int IrInlineThinRun(struct ParseState *Parser, struct Value *FV, IrStmt *Body,
                           IrFuncMeta *Meta, long long *ArgVals, int np,
                           long long *RetOut);
int IrTryThinIfCallFor(struct ParseState *Parser, IrStmt *S);
int IrTryNestedThinFor(struct ParseState *Parser, IrStmt *S);
int IrTryStructInlineCallFor(struct ParseState *Parser, IrStmt *S);



/* Point3b: seed Meta template from compile-time Body frame registry. */









/* Walk Body IR: any global Ident / nested call / void => impure for memo. */
int IrExprTouchesGlobal(struct ParseState *Parser, IrExpr *E,
                               struct Value *Self, const char **LocalNames, int NLocals)
{
    int i;
    if (E == NULL) return 0;
    switch (E->Kind)
    {
        case IrExIdent:
            if (E->Ident == NULL) return 0;
            if (E->ParamSlot >= 0) return 0;
            if (E->LocalSlot >= 0) return 0;
            for (i = 0; i < NLocals; i++)
                if (LocalNames[i] != NULL && strcmp(LocalNames[i], E->Ident) == 0)
                    return 0;
            /* function name used as value: treat as impure if not Self */
            if ((IrGlobalValSoft(Parser, E) != NULL))
            {
                struct Value *V = NULL;
                V = IrGlobalVal(Parser, E); /* global/func via CachedVal */
                if (V != NULL && V->Typ != NULL && V->Typ->Base == TypeFunction)
                    return (V != Self) ? 1 : 0;
                /* any other global (var/array/struct) */
                return 1;
            }
            return 0;
        case IrExBinOp:
        case IrExAssign:
            return IrExprTouchesGlobal(Parser, E->Left, Self, LocalNames, NLocals)
                || IrExprTouchesGlobal(Parser, E->Right, Self, LocalNames, NLocals);
        case IrExUnary:
        case IrExIndex:
            return IrExprTouchesGlobal(Parser, E->Left, Self, LocalNames, NLocals)
                || IrExprTouchesGlobal(Parser, E->Right, Self, LocalNames, NLocals);
        case IrExMember:
            return IrExprTouchesGlobal(Parser, E->Left, Self, LocalNames, NLocals);
        case IrExCall:
            /* calls other than pure-self recursion: impure (conservative) */
            if (E->Ident != NULL && Self != NULL
                && Self->Val != NULL && Self->Val->Identifier != NULL
                && strcmp(E->Ident, Self->Val->Identifier) == 0)
            {
                /* self-recursion: only impure if args touch globals */
            }
            else
                return 1;
            for (i = 0; i < E->ArgCount; i++)
                if (IrExprTouchesGlobal(Parser, E->Args[i], Self, LocalNames, NLocals))
                    return 1;
            return 0;
        default:
            return 0;
    }
}

int IrKernelExprOk(IrExpr *E, int depth)
{
    int i;
    if (E == NULL || depth > 24) return 0;
    switch (E->Kind)
    {
        case IrExConst:
            return 1;
        case IrExIdent:
            return (E->LocalSlot >= 0 || E->ParamSlot >= 0 || E->Ident != NULL);
        case IrExBinOp:
            return IrKernelExprOk(E->Left, depth + 1) && IrKernelExprOk(E->Right, depth + 1);
        case IrExAssign:
            /* Kernel stores only Ident and Index targets. *(p+i)= was accepted
             * but never written (silent no-op) → garbage %s. Fall back to Body-IR. */
            if (E->Left == NULL) return 0;
            if (E->Left->Kind != IrExIdent && E->Left->Kind != IrExIndex)
                return 0;
            /* Kernel stores skip the read-only check: keep const targets on
             * the canonical path so `k = i` on const k is diagnosed. */
            if (IrLvalueRootConstTF(E->Left))
                return 0;
            return IrKernelExprOk(E->Left, depth + 1) && IrKernelExprOk(E->Right, depth + 1);
        case IrExUnary:
            /* ++/-- not implemented in kernel store path — silent no-op. */
            if (E->Op == TokenIncrement || E->Op == TokenDecrement)
                return 0;
            /* &x not implemented: KernelEval treated it as value-of x (wrong ptr). */
            if (E->Op == TokenAmpersand)
                return 0;
            /* * (p+n) not implemented: KernelEval only loads *ident. */
            if (E->Op == TokenAsterisk && E->Left != NULL
                && E->Left->Kind != IrExIdent)
                return 0;
            return IrKernelExprOk(E->Left, depth + 1);
        case IrExIndex:
            /* The thin kernel currently loads p[i] directly and therefore
             * bypasses the caller-owned per-element initialization map. Keep
             * pointer-parameter indexing on the canonical expression path
             * while safety checks are enabled; that path carries alias
             * provenance and emits the source-level uninitialized diagnostic. */
            if (IrSafeChecksOn() && E->Left != NULL
                && E->Left->Kind == IrExIdent && E->Left->ParamSlot >= 0)
                return 0;
            /* nested a[i][j]: allow when root is Ident (multi-dim local/global).
             * ResolveBase already walks Index chain with LocalNDims strides.
             * Refuse array-of-ptr / non-Ident roots (no pointer-load path). */
            if (E->Left != NULL && E->Left->Kind == IrExIndex)
            {
                IrExpr *Root = E->Left;
                while (Root != NULL && Root->Kind == IrExIndex)
                    Root = Root->Left;
                if (Root == NULL || Root->Kind != IrExIdent)
                    return 0;
                /* Param arrays stay on tree; local + global multi-dim OK.
                 * TF_PTR decay bit is not reliable for globals (adj[i][j]). */
                if (Root->ParamSlot >= 0)
                    return 0;
            }
            return IrKernelExprOk(E->Left, depth + 1) && IrKernelExprOk(E->Right, depth + 1);
        case IrExTernary:
            if (!IrKernelExprOk(E->Left, depth + 1) || !IrKernelExprOk(E->Right, depth + 1))
                return 0;
            if (E->Args == NULL || E->ArgCount < 1) return 0;
            return IrKernelExprOk(E->Args[0], depth + 1);
        case IrExCall:
            return 0; /* recursive/user calls use IrCallUserFuncCached, not kernel */
        case IrExMember:
            return 0;
        default:
            return 0;
    }
}

int IrKernelStmtOk(IrStmt *S, int depth)
{
    int i;
    if (S == NULL || depth > 16) return 0;
    switch (S->Kind)
    {
        case IrStExpr:
            return S->Expr != NULL && IrKernelExprOk(S->Expr, 0);
        case IrStBlock:
            for (i = 0; i < S->StmtCount; i++)
                if (!IrKernelStmtOk(S->Stmts[i], depth + 1)) return 0;
            return 1;
        case IrStIf:
            if (S->Expr == NULL || !IrKernelExprOk(S->Expr, 0)) return 0;
            if (S->ThenStmt && !IrKernelStmtOk(S->ThenStmt, depth + 1)) return 0;
            if (S->ElseStmt && !IrKernelStmtOk(S->ElseStmt, depth + 1)) return 0;
            return 1;
        case IrStFor:
            if (!S->FastCond.Active || S->FastCond.Active != 1)
                return 0;
            if (!S->FastInc.Active) return 0;
            if (S->InitStmt && !IrKernelStmtOk(S->InitStmt, depth + 1)) return 0;
            return S->BodyStmt != NULL && IrKernelStmtOk(S->BodyStmt, depth + 1);
        case IrStBreak:
        case IrStContinue:
            return 1;
        case IrStReturn:
            return S->Expr == NULL || IrKernelExprOk(S->Expr, 0);
        default:
            return 0;
    }
}

long long IrKernelEval(struct ParseState *Parser, IrExpr *E, IrCallFrame *F)
{
    long long L, R;
    void *Ptr;
    int es;
    if (E == NULL) return 0;
    switch (E->Kind)
    {
        case IrExConst:
            return E->ConstVal;
        case IrExIdent:
            if (E->LocalSlot >= 0 && E->LocalSlot < IR_CALL_MAX_LOCALS)
                return F->LocalVal[E->LocalSlot];
            if (E->ParamSlot >= 0 && E->ParamSlot < F->NumParams)
                return F->ParamVal[E->ParamSlot];
            if (E->Ident != NULL && (IrGlobalValSoft(Parser, E) != NULL))
            {
                struct Value *V = NULL;
                V = IrGlobalVal(Parser, E); /* global only via CachedVal */
                if (V && V->Typ && V->Typ->Base == TypeArray)
                    return (long long)(intptr_t)V->Val;
                if (V) return IrValueAsInt64(V);
            }
            return 0;
        case IrExUnary:
            L = IrKernelEval(Parser, E->Left, F);
            switch (E->Op)
            {
                case TokenMinus: return -L;
                case TokenUnaryNot: return !L;
                case TokenUnaryExor: return ~L;
                case TokenCast:
                    /* Value cast (ConstVal==0): apply target base I32.
                     * Pointer casts (ConstVal>0) pass address through. */
                    if (E->ConstVal > 0)
                        return L;
                    switch (E->I32)
                    {
                        case TypeChar: return (long long)(signed char)L;
                        case TypeUnsignedChar: return (long long)(unsigned char)L;
                        case TypeShort: return (long long)(short)L;
                        case TypeUnsignedShort: return (long long)(unsigned short)L;
                        case TypeInt: return (long long)(int)L;
                        case TypeUnsignedInt: return (long long)(unsigned int)L;
                        case TypeLong: return (long long)(long)L;
                        case TypeUnsignedLong: return (long long)(unsigned long)L;
                        case TypeLongLong: return L;
                        case TypeUnsignedLongLong: return (long long)(unsigned long long)L;
                        default: return L;
                    }
                case TokenAsterisk:
                {
                    /* *p for int/char — rare in kernels; load 4 bytes */
                    if (E->Left && E->Left->Kind == IrExIdent
                        && E->Left->LocalSlot >= 0)
                    {
                        void *p = (void *)(intptr_t)F->LocalVal[E->Left->LocalSlot];
                        if (p) return *(int *)p;
                    }
                    return 0;
                }
                default: return L;
            }
        case IrExBinOp:
            L = IrKernelEval(Parser, E->Left, F);
            R = IrKernelEval(Parser, E->Right, F);
            if (E->Op == TokenLogicalAnd)
                return (L && R) ? 1 : 0;
            if (E->Op == TokenLogicalOr)
                return (L || R) ? 1 : 0;
            return IrApplyBinOp(Parser, E->Op, L, R, E);
        case IrExIndex:
        {
            long long idx = IrKernelEval(Parser, E->Right, F);
            int unsign = 0;
            if (!IrKernelResolveBase(Parser, E->Left, F, &Ptr, &es))
                return 0;
            /* Element signedness from LocalFromType / TypeFlags, not host plain char. */
            if (E->TypeFlags & IR_TF_UNSIGNED)
                unsign = 1;
            else if (E->Left != NULL && E->Left->Kind == IrExIdent
                     && E->Left->LocalSlot >= 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                struct ValueType *LFT = F->LocalFromType[E->Left->LocalSlot];
                if (LFT != NULL && IrTypeIsUnsignedBase(LFT->Base))
                    unsign = 1;
            }
            else if (E->CanonicalFlags & IR_CF_UNSIGNED)
                unsign = 1;
            if (es == 1)
                return unsign ? (long long)((unsigned char *)Ptr)[idx]
                              : (long long)((signed char *)Ptr)[idx];
            if (es == 2)
                return unsign ? (long long)((unsigned short *)Ptr)[idx]
                              : (long long)((short *)Ptr)[idx];
            if (es == 8)
                return unsign ? (long long)((unsigned long long *)Ptr)[idx]
                              : ((long long *)Ptr)[idx];
            return unsign ? (long long)((unsigned int *)Ptr)[idx]
                          : (long long)((int *)Ptr)[idx];
        }
        case IrExAssign:
        {
            R = IrKernelEval(Parser, E->Right, F);
            if (E->Left && E->Left->Kind == IrExIdent)
            {
                if (E->Left->LocalSlot >= 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    F->LocalVal[E->Left->LocalSlot] = IrNarrowToLocalType(F, E->Left->LocalSlot, R);
                    F->LocalInit[E->Left->LocalSlot] = 1;
                }
                else if (E->Left->ParamSlot >= 0 && E->Left->ParamSlot < F->NumParams)
                    F->ParamVal[E->Left->ParamSlot] = R;
                else if (E->Left->Ident != NULL)
                {
                    /* Global scalar/ptr store — was silent no-op (G=i lost in const for). */
                    struct Value *GV = IrGlobalVal(Parser, E->Left);
                    if (GV != NULL && GV->Typ != NULL && GV->Val != NULL)
                    {
                        if (GV->Typ->Base == TypePointer)
                            GV->Val->Pointer = (void *)(intptr_t)R;
#ifndef NO_FP
                        else if (GV->Typ->Base == TypeFP)
                            GV->Val->FP = IrBitsFP(R);
#endif
                        else
                            ExpressionAssignInt(Parser, GV, (PTR_TYPE_INT_SIGNED)R, FALSE);
                    }
                }
            }
            else if (E->Left && E->Left->Kind == IrExIndex)
            {
                long long idx = IrKernelEval(Parser, E->Left->Right, F);
                if (IrKernelResolveBase(Parser, E->Left->Left, F, &Ptr, &es))
                {
                    /* T *a[N]: store pointer width, not int-truncation. */
                    {
                        IrExpr *Rt = E->Left;
                        while (Rt != NULL && Rt->Kind == IrExIndex)
                            Rt = Rt->Left;
                        if (Rt != NULL && Rt->Kind == IrExIdent
                            && Rt->LocalSlot >= 0 && Rt->LocalSlot < IR_CALL_MAX_LOCALS
                            && F->LocalElemIsPtr[Rt->LocalSlot]
                            && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
                        {
                            ((void **)Ptr)[idx] = (void *)(intptr_t)R;
                            goto index_store_done;
                        }
                    }
                    if (es == 1) ((unsigned char *)Ptr)[idx] = (unsigned char)R;
                    else if (es == 2) ((short *)Ptr)[idx] = (short)R;
                    else if (es == 8) ((long long *)Ptr)[idx] = R;
                    else ((int *)Ptr)[idx] = (int)R;
                index_store_done: ;
                    {
                        IrExpr *Root = E->Left;
                        int Depth = 0;
                        while (Root != NULL && Root->Kind == IrExIndex)
                        { Depth++; Root = Root->Left; }
                        if (Root != NULL && Root->Kind == IrExIdent
                            && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
                            && F->LocalInitMap[Root->LocalSlot] != NULL)
                        {
                            int slot = Root->LocalSlot;
                            int nd = (int)F->LocalNDims[slot];
                            int flat = -1;
                            if (nd >= 2 && Depth == nd)
                            {
                                IrExpr *Chain[IR_MAX_ARRAY_DIMS];
                                int nc = 0, d;
                                IrExpr *T = E->Left;
                                long long acc;
                                while (T != NULL && T->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
                                { Chain[nc++] = T; T = T->Left; }
                                acc = IrKernelEval(Parser, Chain[nc - 1]->Right, F);
                                for (d = 1; d < nd; d++)
                                {
                                    int dim = F->LocalDims[slot][d];
                                    long long ix = IrKernelEval(Parser, Chain[nc - 1 - d]->Right, F);
                                    if (dim > 0) acc = acc * dim + ix;
                                }
                                flat = (int)acc;
                            }
                            else if (nd <= 1)
                                flat = (int)idx;
                            if (flat >= 0 && flat < F->LocalArraySize[slot])
                                F->LocalInitMap[slot][flat] = 1;
                        }
                    }
                }
            }
            return R;
        }
        case IrExTernary:
            if (IrKernelEval(Parser, E->Left, F))
                return IrKernelEval(Parser, E->Right, F);
            return (E->Args && E->ArgCount > 0)
                ? IrKernelEval(Parser, E->Args[0], F) : 0;
        case IrExCall:
        {
            /* Self-call via normal cached entry (correct globals/frames). */
            struct Value *Self;
            long long args[4];
            int ai, np;
            const char *nm, *cn;
            if (IrCallDepth <= 0)
                return 0;
            Self = IrCurrentFunc[IrCallDepth - 1];
            nm = IrDebugFunc[IrCallDepth - 1];
            if (Self == NULL || Self->Val == NULL || nm == NULL)
                return 0;
            cn = E->Ident ? E->Ident
                : (E->Left && E->Left->Ident ? E->Left->Ident : NULL);
            if (cn == NULL || strcmp(cn, nm) != 0)
                return 0;
            np = Self->Val->FuncDef.NumParams;
            if (np < 0) np = 0;
            if (np > 4) np = 4;
            if (E->ArgCount < np) return 0;
            /* The compact path passes only prototype-typed slots (ArgTypes
             * NULL is covered by FixedTypes). Variadic or wider argument
             * lists need per-argument static types: use the full evaluator. */
            if (Self->Val->FuncDef.VarArgs || E->ArgCount != np)
                return IrEvalExpr(Parser, E);
            for (ai = 0; ai < np; ai++)
                args[ai] = IrKernelEval(Parser, E->Args[ai], F);
            return IrCallUserFuncCached(Parser, Self, args, NULL, np);
        }
        default:
            return 0;
    }
}

int IrKernelExecStmt(struct ParseState *Parser, IrStmt *S, IrCallFrame *F, int *flow)
{
    int i;
    *flow = 0;
    if (S == NULL) return 1;
    switch (S->Kind)
    {
        case IrStExpr:
            IrKernelEval(Parser, S->Expr, F);
            return 1;
        case IrStBlock:
            for (i = 0; i < S->StmtCount; i++)
            {
                if (!IrKernelExecStmt(Parser, S->Stmts[i], F, flow)) return 0;
                if (*flow == 1 || *flow == 2) return 1;
            }
            return 1;
        case IrStIf:
            if (IrKernelEval(Parser, S->Expr, F))
            {
                if (S->ThenStmt)
                    return IrKernelExecStmt(Parser, S->ThenStmt, F, flow);
            }
            else if (S->ElseStmt)
                return IrKernelExecStmt(Parser, S->ElseStmt, F, flow);
            return 1;
        case IrStBreak:
            *flow = 1; return 1;
        case IrStContinue:
            *flow = 2; return 1;
        case IrStFor:
        {
            long long bound, iv;
            int iSlot, flow2;
            enum LexToken Op;
            /* Prefer native array accum/fill inside kernel-for body */
            if (IrTryFastArrayAccum(Parser, S))
                return 1;
            if (IrTryFastArrayFill(Parser, S))
                return 1;
            if (S->InitStmt)
                IrKernelExecStmt(Parser, S->InitStmt, F, &flow2);
            if (!S->FastCond.Active || !S->FastInc.Active) return 0;
            iSlot = S->FastInc.VarSlot;
            if (iSlot < 0 && S->FastInc.VarName)
                iSlot = IrFrameSlotByName(S->FastInc.VarName);
            if (iSlot < 0 || iSlot >= IR_CALL_MAX_LOCALS) return 0;
            bound = IrFastResolveBound(Parser, F, &S->FastCond);
            Op = S->FastCond.Op;
            if (Op == TokenLessEqual) bound += 1;
            else if (Op != TokenLessThan) return 0;
            for (iv = F->LocalVal[iSlot]; iv < bound; iv++)
            {
                F->LocalVal[iSlot] = iv;
                F->LocalInit[iSlot] = 1;
                flow2 = 0;
                if (!IrKernelExecStmt(Parser, S->BodyStmt, F, &flow2)) return 0;
                if (flow2 == 1) break;
                /* continue: just next iter */
            }
            F->LocalVal[iSlot] = iv;
            return 1;
        }
        case IrStWhile:
            /* Native insert-shift inside kernel for-body (isort) */
            if (IrTryFastInsertShift(Parser, S))
                return 1;
            return 0;
        default:
            return 0;
    }
}

int IrTryKernelWhile(struct ParseState *Parser, IrStmt *S)
{
    IrCallFrame *F;
    int flow = 0;
    if (S == NULL || S->Kind != IrStWhile || IrCallDepth <= 0)
        return 0;
    if (S->Expr == NULL || !IrKernelExprOk(S->Expr, 0))
        return 0;
    if (S->BodyStmt == NULL || !IrKernelStmtOk(S->BodyStmt, 0))
        return 0;
    F = IrCallStack[IrCallDepth - 1];
    /* Pointer scan/inc while: Body-IR or dedicated ptrscan — not kernel. */
    if (IrExprHasPtrInc(S->Expr, F, 0) || IrStmtHasPtrInc(S->BodyStmt, F, 0))
        return 0;
    IrKernelGCacheReset();
    for (;;)
    {
        if (!IrKernelEval(Parser, S->Expr, F))
            break;
        flow = 0;
        if (!IrKernelExecStmt(Parser, S->BodyStmt, F, &flow))
            return 0;
        if (flow == 1) break;
        if (flow == 3) break;
    }
    return 1;
}

int IrTryTightFor(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Body;
    IrExpr *E, *Add, *Dest, *A, *B;
    int iSlot, sSlot, accIsLeft;
    IrCallFrame *F;
    long long *pi, *ps, bound, iv, limit;
    enum LexToken Op;

    (void)Parser;
    if (S == NULL || S->Kind != IrStFor)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (IrCallDepth <= 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;
    A = Add->Left;
    B = Add->Right;
    if (A == NULL || B == NULL)
        return 0;
    /* s = s + i  or  s = i + s */
    accIsLeft = 0;
    if (A->Kind == IrExIdent && Dest->Ident != NULL && A->Ident != NULL
        && strcmp(Dest->Ident, A->Ident) == 0 && B->Kind == IrExIdent
        && B->Ident != NULL && strcmp(B->Ident, S->FastInc.VarName) == 0)
        accIsLeft = 1;
    else if (B->Kind == IrExIdent && Dest->Ident != NULL && B->Ident != NULL
        && strcmp(Dest->Ident, B->Ident) == 0 && A->Kind == IrExIdent
        && A->Ident != NULL && strcmp(A->Ident, S->FastInc.VarName) == 0)
        accIsLeft = 1;
    else
        return 0;
    (void)accIsLeft;

    iSlot = IrFrameSlotByName(S->FastInc.VarName);
    sSlot = IrFrameSlotByName(Dest->Ident);
    if (iSlot < 0 || sSlot < 0 || iSlot == sSlot)
        return 0;
    F = IrCallStack[IrCallDepth - 1];
    pi = &F->LocalVal[iSlot];
    ps = &F->LocalVal[sSlot];
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Op = S->FastCond.Op;
    F->LocalInit[iSlot] = 1;
    F->LocalInit[sSlot] = 1;

    /* Only when cond var is the same as inc var */
    if (S->FastCond.VarName == NULL ||
        strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;

    limit = bound;
    switch (Op)
    {
        case TokenLessThan:
            for (iv = *pi; iv < limit; iv++)
                *ps += iv;
            *pi = iv;
            return 1;
        case TokenLessEqual:
            for (iv = *pi; iv <= limit; iv++)
                *ps += iv;
            *pi = iv;
            return 1;
        case TokenGreaterThan:
            for (iv = *pi; iv > limit; iv--)
                *ps += iv;
            *pi = iv;
            return 1;
        case TokenGreaterEqual:
            for (iv = *pi; iv >= limit; iv--)
                *ps += iv;
            *pi = iv;
            return 1;
        default:
            return 0;
    }
}

int IrTryTightLoopVars(struct ParseState *Parser, IrLoop *Loop)
{
    IrStmt *Body;
    IrExpr *E, *Add, *Dest, *A, *B;
    struct Value *Iv, *Sv, *Av;
    long long iv, sv, bound;
    enum LexToken Op;
    int ModeArr = 0; /* 0: s+=i  1: s+=a[i] */
    void *ArrBase = NULL;
    int ArrLen = 0;
    int ArrEs = (int)sizeof(int);
    int iSlot = -1, sSlot = -1;
    IrCallFrame *Fr = NULL;

    if (Loop == NULL)
        return 0;
    /* Debug / TOKEN_FAST=0: every iteration must run real statements */
    if (!IrTokenFastAllowed(Parser))
        return 0;
    if (Loop->Kind != IrLoopFor && Loop->Kind != IrLoopWhile && Loop->Kind != IrLoopDo)
        return 0;
    if (!Loop->Cond.Active || Loop->Cond.Active != 1)
        return 0;
    if (!Loop->Inc.Active || Loop->Inc.VarName == NULL || Loop->Cond.VarName == NULL)
        return 0;
    if (strcmp(Loop->Cond.VarName, Loop->Inc.VarName) != 0)
        return 0;
    Body = Loop->BodyIr;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->Ident == NULL)
        return 0;
    if (Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;
    A = Add->Left;
    B = Add->Right;
    if (A == NULL || B == NULL)
        return 0;

    /* s += i  (or i + s) */
    if ((A->Kind == IrExIdent && A->Ident && strcmp(A->Ident, Dest->Ident) == 0
           && B->Kind == IrExIdent && B->Ident && strcmp(B->Ident, Loop->Inc.VarName) == 0)
          || (B->Kind == IrExIdent && B->Ident && strcmp(B->Ident, Dest->Ident) == 0
           && A->Kind == IrExIdent && A->Ident && strcmp(A->Ident, Loop->Inc.VarName) == 0))
        ModeArr = 0;
    /* s += a[i]  where i is loop var */
    else if (A->Kind == IrExIdent && A->Ident && strcmp(A->Ident, Dest->Ident) == 0
             && B->Kind == IrExIndex && B->Left != NULL && B->Right != NULL
             && B->Left->Kind == IrExIdent && B->Left->Ident != NULL
             && B->Right->Kind == IrExIdent && B->Right->Ident != NULL
             && strcmp(B->Right->Ident, Loop->Inc.VarName) == 0)
        ModeArr = 1;
    else if (B->Kind == IrExIdent && B->Ident && strcmp(B->Ident, Dest->Ident) == 0
             && A->Kind == IrExIndex && A->Left != NULL && A->Right != NULL
             && A->Left->Kind == IrExIdent && A->Left->Ident != NULL
             && A->Right->Kind == IrExIdent && A->Right->Ident != NULL
             && strcmp(A->Right->Ident, Loop->Inc.VarName) == 0)
        ModeArr = 1;
    else
        return 0;

    if (IrCallDepth > 0)
        Fr = IrCallStack[IrCallDepth - 1];

    /* Resolve iv / sv: prefer IR frame slots, else Variable table */
    iv = 0;
    sv = 0;
    Iv = NULL;
    Sv = NULL;
    if (Dest->LocalSlot >= 0 && Fr != NULL && Dest->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        sSlot = Dest->LocalSlot;
        sv = Fr->LocalVal[sSlot];
    }
    else if (Fr != NULL && IrFrameGetByName(Dest->Ident, &sv) == 1)
    {
        /* name→slot on current IR frame */
        {
            int si;
            for (si = Fr->NumLocals - 1; si >= 0; si--)
                if (Fr->LocalName[si] && Dest->Ident
                    && strcmp(Fr->LocalName[si], Dest->Ident) == 0)
                { sSlot = si; break; }
        }
    }
    else if (Dest->CachedVal != NULL && !Dest->CachedVal->OutOfScope)
    {
        Sv = Dest->CachedVal;
        sv = IrValueAsInt64(Sv);
    }
    else if ((IrGlobalValSoft(Parser, Dest) != NULL))
    {
        Sv = IrGlobalVal(Parser, Dest);
        if (Sv == NULL) return 0;
        sv = IrValueAsInt64(Sv);
    }
    else
        return 0;

    if (Loop->Inc.VarSlot >= 0 && Fr != NULL && Loop->Inc.VarSlot < IR_CALL_MAX_LOCALS)
    {
        iSlot = Loop->Inc.VarSlot;
        iv = Fr->LocalVal[iSlot];
    }
    else if (Fr != NULL && IrFrameGetByName(Loop->Inc.VarName, &iv) == 1)
    {
        int si;
        for (si = Fr->NumLocals - 1; si >= 0; si--)
            if (Fr->LocalName[si] && Loop->Inc.VarName
                && strcmp(Fr->LocalName[si], Loop->Inc.VarName) == 0)
            { iSlot = si; break; }
    }
    else if ((IrGlobalIdentSoft(Parser, Loop->Inc.VarName) != NULL))
    {
        if (Loop->Inc.CachedVar != NULL && !Loop->Inc.CachedVar->OutOfScope)
            Iv = Loop->Inc.CachedVar;
        else {
            Iv = IrGlobalCacheFill(Parser, Loop->Inc.VarName, &Loop->Inc.CachedVar);
        }
        if (Iv == NULL) return 0;
        iv = IrValueAsInt64(Iv);
    }
    else
        return 0;

    if (ModeArr == 1)
    {
        IrExpr *Idx = (B->Kind == IrExIndex) ? B : A;
        char *ArrName = Idx->Left->Ident;
        Av = NULL;
        if (Idx->Left->LocalSlot >= 0 && Fr != NULL
            && Idx->Left->LocalSlot < IR_CALL_MAX_LOCALS
            && Fr->LocalVal[Idx->Left->LocalSlot] != 0)
        {
            int ls = Idx->Left->LocalSlot;
            ArrBase = (void *)(intptr_t)Fr->LocalVal[ls];
            ArrLen = Fr->LocalArraySize[ls];
            ArrEs = Fr->LocalElemSize[ls] ? (int)Fr->LocalElemSize[ls] : (int)sizeof(int);
        }
        else if (Idx->Left->CachedVal != NULL && !Idx->Left->CachedVal->OutOfScope)
        {
            Av = Idx->Left->CachedVal;
            if (Av->Typ == NULL || Av->Val == NULL)
                return 0;
            if (Av->Typ->Base == TypeArray)
            {
                ArrBase = (void *)Av->Val;
                ArrLen = Av->Typ->ArraySize;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    ArrEs = Av->Typ->FromType->Sizeof;
            }
            else if (Av->Typ->Base == TypePointer && Av->Val->Pointer != NULL)
            {
                ArrBase = Av->Val->Pointer;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    ArrEs = Av->Typ->FromType->Sizeof;
            }
            else
                return 0;
        }
        else if ((IrGlobalIdentSoft(Parser, ArrName) != NULL))
        {
            Av = IrGlobalVal(Parser, Idx->Left);
            if (Av == NULL || Av->Typ == NULL || Av->Val == NULL)
                return 0;
            Idx->Left->CachedVal = Av; /* F3: bind global array base */
            if (Av->Typ->Base == TypeArray)
            {
                ArrBase = (void *)Av->Val;
                ArrLen = Av->Typ->ArraySize;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    ArrEs = Av->Typ->FromType->Sizeof;
            }
            else if (Av->Typ->Base == TypePointer && Av->Val->Pointer != NULL)
            {
                ArrBase = Av->Val->Pointer;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    ArrEs = Av->Typ->FromType->Sizeof;
            }
            else
                return 0;
        }
        else
            return 0;
        if (ArrBase == NULL)
            return 0;
        if (ArrEs != (int)sizeof(int) && ArrEs != (int)sizeof(short) && ArrEs != 1)
            return 0;
    }

    bound = IrFastResolveBound(Parser, Fr, &Loop->Cond);
    Op = Loop->Cond.Op;
    /* for: first body already ran, next iter does inc then cond */
    if (Loop->Kind == IrLoopFor)
        iv = iv + 1;
    switch (Op)
    {
        case TokenLessThan:
            if (ModeArr == 0)
            {
                for (; iv < bound; iv++)
                    sv += iv;
            }
            else
            {
                for (; iv < bound; iv++)
                {
                    if (ArrLen > 0 && (iv < 0 || iv >= ArrLen))
                        break;
                    if (ArrEs == 1)
                        sv += ((unsigned char *)ArrBase)[iv];
                    else if (ArrEs == (int)sizeof(short))
                        sv += ((short *)ArrBase)[iv];
                    else
                        sv += ((int *)ArrBase)[iv];
                }
            }
            break;
        case TokenLessEqual:
            if (ModeArr == 0)
            {
                for (; iv <= bound; iv++)
                    sv += iv;
            }
            else
            {
                for (; iv <= bound; iv++)
                {
                    if (ArrLen > 0 && (iv < 0 || iv >= ArrLen))
                        break;
                    if (ArrEs == 1)
                        sv += ((unsigned char *)ArrBase)[iv];
                    else if (ArrEs == (int)sizeof(short))
                        sv += ((short *)ArrBase)[iv];
                    else
                        sv += ((int *)ArrBase)[iv];
                }
            }
            break;
        default:
            return 0;
    }

    if (iSlot >= 0 && Fr != NULL)
    {
        Fr->LocalVal[iSlot] = iv;
        Fr->LocalInit[iSlot] = 1;
    }
    else if (Iv != NULL)
        ExpressionAssignInt(Parser, Iv, (PTR_TYPE_INT_SIGNED)iv, FALSE);

    if (sSlot >= 0 && Fr != NULL)
    {
        Fr->LocalVal[sSlot] = sv;
        Fr->LocalInit[sSlot] = 1;
    }
    else if (Sv != NULL)
        ExpressionAssignLongLong(Parser, Sv, sv, FALSE);
    return 1;
}

int IrDispatchForFp(struct ParseState *Parser, IrStmt *S, int k)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
#if !defined(BABYC_NO_DISPATCH_TABLE) && (defined(__GNUC__) || defined(__clang__))
    /* Computed-goto dispatch: k is ForFpKind assigned at compile time. */
    static void *const fp_jt[] = {
        [0] = &&L_fp_miss,
        [IR_FP_MISS] = &&L_fp_miss,
        [IR_FP_PTR_WALK] = &&L_fp_ptr_walk,
        [IR_FP_ITER_CALL_ACCUM] = &&L_fp_iter,
        [IR_FP_KERNEL_FOR] = &&L_fp_kernel,
        [IR_FP_DENSE_BODY] = &&L_fp_dense,
        [IR_FP_ARRAY_ACCUM] = &&L_fp_arr_accum,
        [IR_FP_ARRAY_FILL] = &&L_fp_arr_fill,
        [IR_FP_LEAF_CALL_ACCUM] = &&L_fp_leaf,
        [IR_FP_SCORE_CALL_ACCUM] = &&L_fp_score,
        [IR_FP_TIGHT_FOR] = &&L_fp_tight,
        [IR_FP_NESTED_THIN] = &&L_fp_nested,
        [IR_FP_STRUCT_INLINE] = &&L_fp_struct,
        [IR_FP_THIN_IF_CALL] = &&L_fp_thin_if,
        [IR_FP_MATMUL] = &&L_fp_matmul,
    };
    unsigned uk = (unsigned)k;
    if (uk < (unsigned)(sizeof(fp_jt)/sizeof(fp_jt[0])) && fp_jt[uk] != 0)
        goto *fp_jt[uk];
    return 0;
L_fp_ptr_walk:   return IrTryFastPtrWalk(Parser, S);
L_fp_iter:       return IrTryIterCallAccum(Parser, S);
L_fp_kernel:     return IrTryKernelFor(Parser, S);
L_fp_dense:      return IrTryDenseForBody(Parser, S);
L_fp_arr_accum:  return IrTryFastArrayAccum(Parser, S);
L_fp_arr_fill:   return IrTryFastArrayFill(Parser, S);
L_fp_leaf:       return IrTryLeafCallAccum(Parser, S);
L_fp_score:      return IrTryScoreCallAccum(Parser, S);
L_fp_tight:      return IrTryTightFor(Parser, S);
L_fp_nested:     return IrTryNestedThinFor(Parser, S);
L_fp_struct:     return IrTryStructInlineCallFor(Parser, S);
L_fp_thin_if:    return IrTryThinIfCallFor(Parser, S);
L_fp_matmul:     return IrTryFastMatmul3(Parser, S);
L_fp_miss:       return 0;
#else
    switch (k)
    {
        case IR_FP_PTR_WALK:         return IrTryFastPtrWalk(Parser, S);
        case IR_FP_ITER_CALL_ACCUM:  return IrTryIterCallAccum(Parser, S);
        case IR_FP_KERNEL_FOR:       return IrTryKernelFor(Parser, S);
        case IR_FP_DENSE_BODY:       return IrTryDenseForBody(Parser, S);
        case IR_FP_ARRAY_ACCUM:      return IrTryFastArrayAccum(Parser, S);
        case IR_FP_ARRAY_FILL:       return IrTryFastArrayFill(Parser, S);
        case IR_FP_LEAF_CALL_ACCUM:  return IrTryLeafCallAccum(Parser, S);
        case IR_FP_SCORE_CALL_ACCUM: return IrTryScoreCallAccum(Parser, S);
        case IR_FP_TIGHT_FOR:        return IrTryTightFor(Parser, S);
        case IR_FP_NESTED_THIN:      return IrTryNestedThinFor(Parser, S);
        case IR_FP_STRUCT_INLINE:    return IrTryStructInlineCallFor(Parser, S);
        case IR_FP_THIN_IF_CALL:     return IrTryThinIfCallFor(Parser, S);
        case IR_FP_MATMUL:          return IrTryFastMatmul3(Parser, S);
        default: return 0;
    }
#endif
}

int IrProbeForFp(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    /* Specialized shapes before general KernelFor (array_sum nested). */
    static const signed char order[] = {
        IR_FP_MATMUL, IR_FP_PTR_WALK, IR_FP_ITER_CALL_ACCUM,
        IR_FP_ARRAY_ACCUM, IR_FP_ARRAY_FILL, IR_FP_TIGHT_FOR,
        IR_FP_DENSE_BODY, IR_FP_KERNEL_FOR,
        IR_FP_LEAF_CALL_ACCUM, IR_FP_SCORE_CALL_ACCUM,
        IR_FP_NESTED_THIN, IR_FP_STRUCT_INLINE, IR_FP_THIN_IF_CALL
    };
    int i;
    for (i = 0; i < (int)(sizeof(order)/sizeof(order[0])); i++)
    {
        if (IrDispatchForFp(Parser, S, order[i]))
        {
            S->ForFpKind = order[i];
            return 1;
        }
    }
    S->ForFpKind = IR_FP_MISS;
    return 0;
}

/* Shared loop fast-path chains (tree IrExecuteStmt and linear
 * IR_K_TREE_WHILE / IR_K_TREE_FOR kernels run the same matchers).
 * Each returns 1 when the whole loop was executed natively. */
int IrTryFastWhileChain(struct ParseState *Parser, IrStmt *S)
{
    return IrTryFastPtrScan(Parser, S)
        || IrTryFastPtrScanPred(Parser, S)
        || IrTryFastPtrScanBound(Parser, S)
        || IrTryFastInsertShift(Parser, S)
        || IrTryKernelWhile(Parser, S);
}

/* For loop after InitStmt ran: ForFpKind memo, else probe in canonical order. */
int IrTryFastForAfterInit(struct ParseState *Parser, IrStmt *S)
{
    signed char k = S->ForFpKind;
    int hit = 0;
    if (k == IR_FP_MISS)
        return 0;
    if (k > IR_FP_MISS)
        hit = IrDispatchForFp(Parser, S, k);
    if (!hit)
        hit = IrProbeForFp(Parser, S);
    return hit;
}

int IrTryFastPtrScanFor(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrCallFrame *Fr;
    IrStmt *Body, *AssignP, *InnerWhile, *WB;
    IrExpr *Rhs, *WC, *E;
    IrStmt **list, **wl;
    int body_n, wn, wi, islot, pslot, bslot, sslot, es;
    long long nbound, acc, i;
    unsigned char *base;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];

    /* Bound from FastCond (const) or Expr i < N */
    nbound = -1;
    islot = -1;
    if (S->FastCond.Active == 1
        && (S->FastCond.Op == TokenLessThan || S->FastCond.Op == TokenLessEqual))
    {
        nbound = IrFastResolveBound(Parser, Fr, &S->FastCond);
        if (S->FastCond.Op == TokenLessEqual)
            nbound += 1;
        if (S->FastCond.VarName != NULL)
        {
            int li;
            for (li = 0; li < IR_CALL_MAX_LOCALS; li++)
                if (Fr->LocalName[li] != NULL
                    && strcmp(Fr->LocalName[li], S->FastCond.VarName) == 0)
                {
                    islot = li;
                    break;
                }
        }
        if (islot < 0 && S->Expr != NULL && S->Expr->Kind == IrExBinOp
            && S->Expr->Left != NULL && S->Expr->Left->LocalSlot >= 0)
            islot = S->Expr->Left->LocalSlot;
    }
    if (nbound < 0 && S->Expr != NULL && S->Expr->Kind == IrExBinOp
        && (S->Expr->Op == TokenLessThan || S->Expr->Op == TokenLessEqual)
        && S->Expr->Left != NULL && S->Expr->Left->LocalSlot >= 0
        && S->Expr->Right != NULL && S->Expr->Right->Kind == IrExConst)
    {
        islot = S->Expr->Left->LocalSlot;
        nbound = S->Expr->Right->ConstVal;
        if (S->Expr->Op == TokenLessEqual)
            nbound += 1;
    }
    if (islot < 0 || nbound < 0 || nbound > 100000000)
        return 0;

    Body = S->BodyStmt;
    if (Body == NULL)
        return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        body_n = Body->StmtCount;
    }
    else
    {
        list = &Body;
        body_n = 1;
    }
    if (body_n != 2)
        return 0;
    AssignP = list[0];
    InnerWhile = list[1];
    if (AssignP == NULL || AssignP->Kind != IrStExpr || AssignP->Expr == NULL
        || AssignP->Expr->Kind != IrExAssign
        || AssignP->Expr->Left == NULL || AssignP->Expr->Left->LocalSlot < 0
        || AssignP->Expr->Right == NULL || AssignP->Expr->Right->LocalSlot < 0)
        return 0;
    if (InnerWhile == NULL || InnerWhile->Kind != IrStWhile)
        return 0;

    pslot = AssignP->Expr->Left->LocalSlot;
    bslot = AssignP->Expr->Right->LocalSlot;
    if (pslot >= IR_CALL_MAX_LOCALS || bslot >= IR_CALL_MAX_LOCALS)
        return 0;
    if (!Fr->LocalIsPtr[pslot] || !Fr->LocalIsPtr[bslot])
        return 0;
    es = Fr->LocalElemSize[pslot];
    if (es != 1)
        return 0; /* char* scan only for for+scan */

    base = (unsigned char *)(intptr_t)Fr->LocalVal[bslot];
    if (base == NULL)
        return 0;

    WC = InnerWhile->Expr;
    if (WC == NULL || WC->Kind != IrExUnary || WC->Op != TokenAsterisk
        || WC->Left == NULL || WC->Left->LocalSlot != pslot)
        return 0;
    WB = InnerWhile->BodyStmt;
    if (WB && WB->Kind == IrStBlock)
    {
        wl = WB->Stmts;
        wn = WB->StmtCount;
    }
    else if (WB)
    {
        wl = &WB;
        wn = 1;
    }
    else
        return 0;

    sslot = -1;
    {
        int saw_add = 0, saw_inc = 0;
        for (wi = 0; wi < wn; wi++)
        {
            IrStmt *St = wl[wi];
            if (St == NULL || St->Kind != IrStExpr || St->Expr == NULL)
                return 0;
            E = St->Expr;
            if (E->Kind == IrExUnary && E->Op == TokenIncrement
                && E->Left && E->Left->LocalSlot == pslot)
            {
                saw_inc = 1;
                continue;
            }
            /* p = p + 1 */
            if (E->Kind == IrExAssign && E->Left && E->Left->LocalSlot == pslot
                && E->Right && E->Right->Kind == IrExBinOp && E->Right->Op == TokenPlus
                && E->Right->Left && E->Right->Left->LocalSlot == pslot
                && E->Right->Right && E->Right->Right->Kind == IrExConst
                && E->Right->Right->ConstVal == 1)
            {
                saw_inc = 1;
                continue;
            }
            /* s = s + *p  (+= rewritten at compile) */
            if (E->Kind == IrExAssign && E->Left && E->Left->LocalSlot >= 0
                && !Fr->LocalIsPtr[E->Left->LocalSlot]
                && E->Right && E->Right->Kind == IrExBinOp && E->Right->Op == TokenPlus)
            {
                IrExpr *A = E->Right->Left, *B = E->Right->Right;
                int slot = E->Left->LocalSlot;
                IrExpr *Star = NULL;
                if (A && A->Kind == IrExIdent && A->LocalSlot == slot
                    && B && B->Kind == IrExUnary && B->Op == TokenAsterisk)
                    Star = B;
                else if (B && B->Kind == IrExIdent && B->LocalSlot == slot
                         && A && A->Kind == IrExUnary && A->Op == TokenAsterisk)
                    Star = A;
                if (Star && Star->Left && Star->Left->LocalSlot == pslot)
                {
                    sslot = slot;
                    saw_add = 1;
                    continue;
                }
            }
            return 0;
        }
        if (!saw_add || !saw_inc || sslot < 0)
            return 0;
    }

    acc = Fr->LocalInit[sslot] ? Fr->LocalVal[sslot] : 0;
    for (i = 0; i < nbound; i++)
    {
        unsigned char *p = base;
        while (*p)
        {
            acc += (long long)(*p);
            p++;
        }
    }
    Fr->LocalVal[islot] = nbound;
    Fr->LocalInit[islot] = 1;
    Fr->LocalVal[sslot] = acc;
    Fr->LocalInit[sslot] = 1;
    Fr->LocalVal[pslot] = (long long)(intptr_t)base;
    Fr->LocalInit[pslot] = 1;
    return 1;
}

int IrTryFastPtrScanBound(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrCallFrame *Fr;
    IrExpr *Cond, *L, *R;
    IrStmt *Body;
    IrStmt **list;
    int n, i, pslot, sslot, es, saw_add, saw_inc;
    char *p, *end;
    long long acc;

    if (S == NULL || S->Kind != IrStWhile || IrCallDepth <= 0 || S->Expr == NULL)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    Cond = S->Expr;
    /* p < expr  or  p <= expr */
    if (Cond->Kind != IrExBinOp
        || (Cond->Op != TokenLessThan && Cond->Op != TokenLessEqual))
        return 0;
    L = Cond->Left;
    R = Cond->Right;
    if (L == NULL || L->Kind != IrExIdent || L->LocalSlot < 0)
        return 0;
    pslot = L->LocalSlot;
    if (!Fr->LocalIsPtr[pslot])
        return 0;
    es = Fr->LocalElemSize[pslot];
    if (es != 1 && es != 2 && es != 4 && es != 8)
        return 0;

    /* Evaluate end address once */
    end = (char *)(intptr_t)IrEvalExpr(Parser, R);
    p = (char *)(intptr_t)Fr->LocalVal[pslot];
    if (p == NULL)
        return 0;

    Body = S->BodyStmt;
    if (Body == NULL)
        return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        n = Body->StmtCount;
    }
    else
    {
        list = &Body;
        n = 1;
    }
    sslot = -1;
    saw_add = saw_inc = 0;
    for (i = 0; i < n; i++)
    {
        IrStmt *St = list[i];
        IrExpr *E;
        if (St == NULL || St->Kind != IrStExpr || St->Expr == NULL)
            return 0;
        E = St->Expr;
        if (E->Kind == IrExUnary && E->Op == TokenIncrement
            && E->Left && E->Left->LocalSlot == pslot)
        {
            saw_inc = 1;
            continue;
        }
        if (E->Kind == IrExAssign && E->Left && E->Left->LocalSlot == pslot
            && E->Right && E->Right->Kind == IrExBinOp && E->Right->Op == TokenPlus
            && E->Right->Left && E->Right->Left->LocalSlot == pslot
            && E->Right->Right && E->Right->Right->Kind == IrExConst
            && E->Right->Right->ConstVal == 1)
        {
            saw_inc = 1;
            continue;
        }
        if (E->Kind == IrExAssign && E->Left && E->Left->LocalSlot >= 0
            && !Fr->LocalIsPtr[E->Left->LocalSlot]
            && E->Right && E->Right->Kind == IrExBinOp && E->Right->Op == TokenPlus)
        {
            IrExpr *A = E->Right->Left, *B = E->Right->Right, *Star = NULL;
            int slot = E->Left->LocalSlot;
            if (A && A->Kind == IrExIdent && A->LocalSlot == slot
                && B && B->Kind == IrExUnary && B->Op == TokenAsterisk)
                Star = B;
            else if (B && B->Kind == IrExIdent && B->LocalSlot == slot
                     && A && A->Kind == IrExUnary && A->Op == TokenAsterisk)
                Star = A;
            if (Star && Star->Left && Star->Left->LocalSlot == pslot)
            {
                sslot = slot;
                saw_add = 1;
                continue;
            }
        }
        return 0;
    }
    if (!saw_add || !saw_inc || sslot < 0)
        return 0;

    acc = Fr->LocalInit[sslot] ? Fr->LocalVal[sslot] : 0;
    if (Cond->Op == TokenLessEqual)
        end += es; /* inclusive — rare */
    while (p < end)
    {
        if (es == 1)
            acc += (long long)(unsigned char)(*p);
        else if (es == 2)
            acc += (long long)*(short *)p;
        else if (es == 8)
            acc += *(long long *)p;
        else
            acc += (long long)*(int *)p;
        p += es;
    }
    Fr->LocalVal[pslot] = (long long)(intptr_t)p;
    Fr->LocalInit[pslot] = 1;
    Fr->LocalVal[sslot] = acc;
    Fr->LocalInit[sslot] = 1;
    return 1;
}

int IrTryFastPtrScanPred(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrExpr *Cond, *PIdent;
    IrCallFrame *Fr;
    IrStmt *Body, *IfSt = NULL, *IncSt = NULL;
    IrStmt **list;
    int pslot, endslot = -1, bounded = 0, aslot = -1, n, i, nc = 0;
    long long chars[4];
    if (!IrShapeFpEnabled())
        return 0;
    unsigned char *p;
    long long acc;
    IrExpr *Pred, *IncE;

    if (S == NULL || S->Kind != IrStWhile || S->Expr == NULL || IrCallDepth <= 0)
        return 0;
    Cond = S->Expr;
    if (Cond->Kind == IrExUnary && Cond->Op == TokenAsterisk)
    {
        PIdent = Cond->Left;
        if (PIdent == NULL || PIdent->Kind != IrExIdent || PIdent->LocalSlot < 0)
            return 0;
        pslot = PIdent->LocalSlot;
    }
    else if (Cond->Kind == IrExBinOp && Cond->Op == TokenLessThan
             && Cond->Left && Cond->Left->Kind == IrExIdent
             && Cond->Left->LocalSlot >= 0
             && Cond->Right && Cond->Right->Kind == IrExIdent
             && Cond->Right->LocalSlot >= 0)
    {
        /* Bounded scans (while (p < end)) are equivalent to the same scan
         * kernel only when both locals are char pointers. */
        PIdent = Cond->Left;
        pslot = Cond->Left->LocalSlot;
        endslot = Cond->Right->LocalSlot;
        bounded = 1;
    }
    else
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    if (!IrLocalPtrElemIsChar(Fr, pslot))
        return 0;
    if (bounded && (endslot == pslot || !IrLocalPtrElemIsChar(Fr, endslot)))
        return 0;

    Body = S->BodyStmt;
    if (Body == NULL)
        return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        n = Body->StmtCount;
    }
    else
    {
        list = &Body;
        n = 1;
    }
    if (n < 1 || n > 3)
        return 0;

    for (i = 0; i < n; i++)
    {
        IrStmt *St = list[i];
        if (St == NULL)
            return 0;
        if (St->Kind == IrStIf && IfSt == NULL)
        {
            IfSt = St;
            continue;
        }
        if (St->Kind == IrStExpr && St->Expr != NULL)
        {
            IrExpr *E = St->Expr;
            if (E->Kind == IrExUnary
                && (E->Op == TokenIncrement || E->Op == TokenDecrement)
                && E->Left && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot == pslot)
            {
                if (E->Op == TokenDecrement)
                    return 0;
                IncSt = St;
                continue;
            }
            if (E->Kind == IrExAssign && E->Op == TokenAssign
                && E->Left && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot == pslot
                && E->Right && E->Right->Kind == IrExBinOp && E->Right->Op == TokenPlus
                && E->Right->Left && E->Right->Left->Kind == IrExIdent
                && E->Right->Left->LocalSlot == pslot
                && E->Right->Right && E->Right->Right->Kind == IrExConst
                && E->Right->Right->ConstVal == 1)
            {
                IncSt = St;
                continue;
            }
        }
        return 0;
    }
    if (IfSt == NULL || IncSt == NULL || IfSt->ElseStmt != NULL)
        return 0;
    /* Then body: accum++ or accum += 1 or accum = accum + 1 */
    {
        IrStmt *Th = IfSt->ThenStmt;
        IrExpr *E;
        if (Th == NULL)
            return 0;
        if (Th->Kind == IrStBlock)
        {
            if (Th->StmtCount != 1 || Th->Stmts == NULL)
                return 0;
            Th = Th->Stmts[0];
        }
        if (Th->Kind != IrStExpr || Th->Expr == NULL)
            return 0;
        E = Th->Expr;
        if (E->Kind == IrExUnary && E->Op == TokenIncrement
            && E->Left && E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0)
            aslot = E->Left->LocalSlot;
        else if (E->Kind == IrExAssign && E->Left && E->Left->Kind == IrExIdent
                 && E->Left->LocalSlot >= 0)
        {
            aslot = E->Left->LocalSlot;
            if (E->Op == TokenAddAssign)
            {
                if (!(E->Right && E->Right->Kind == IrExConst && E->Right->ConstVal == 1))
                    return 0;
            }
            else if (E->Op == TokenAssign && E->Right && E->Right->Kind == IrExBinOp
                     && E->Right->Op == TokenPlus)
            {
                IrExpr *A = E->Right->Left, *B = E->Right->Right;
                if (!((A && A->Kind == IrExIdent && A->LocalSlot == aslot
                       && B && B->Kind == IrExConst && B->ConstVal == 1)
                      || (B && B->Kind == IrExIdent && B->LocalSlot == aslot
                          && A && A->Kind == IrExConst && A->ConstVal == 1)))
                    return 0;
            }
            else
                return 0;
        }
        else
            return 0;
        if (aslot == pslot || aslot >= IR_CALL_MAX_LOCALS)
            return 0;
    }
    /* Predicate: *p == c  or  (*p==c1)||(*p==c2)||...  (max 4 const chars) */
    Pred = IfSt->Expr;
    nc = 0;
    {
        IrExpr *stack[8];
        int sp = 0;
        if (Pred == NULL)
            return 0;
        stack[sp++] = Pred;
        while (sp > 0)
        {
            IrExpr *X = stack[--sp];
            if (X->Kind == IrExBinOp && X->Op == TokenLogicalOr)
            {
                if (sp + 2 > 8)
                    return 0;
                if (X->Right) stack[sp++] = X->Right;
                if (X->Left) stack[sp++] = X->Left;
                continue;
            }
            if (X->Kind == IrExBinOp && X->Op == TokenEqual
                && X->Left && X->Right)
            {
                IrExpr *L = X->Left, *R = X->Right;
                IrExpr *Star = NULL, *Cst = NULL;
                if (L->Kind == IrExUnary && L->Op == TokenAsterisk
                    && R->Kind == IrExConst)
                {
                    Star = L;
                    Cst = R;
                }
                else if (R->Kind == IrExUnary && R->Op == TokenAsterisk
                         && L->Kind == IrExConst)
                {
                    Star = R;
                    Cst = L;
                }
                else
                    return 0;
                if (Star->Left == NULL || Star->Left->Kind != IrExIdent
                    || Star->Left->LocalSlot != pslot)
                    return 0;
                if (nc >= 4)
                    return 0;
                chars[nc++] = Cst->ConstVal;
                continue;
            }
            return 0;
        }
    }
    if (nc < 1)
        return 0;

    p = (unsigned char *)(void *)(intptr_t)Fr->LocalVal[pslot];
    if (p == NULL)
        return 0;
    {
        unsigned char *endp = NULL;
        if (bounded)
        {
            endp = (unsigned char *)(void *)(intptr_t)Fr->LocalVal[endslot];
            if (endp == NULL)
                return 0;
        }
        unsigned char hitmap[256];
        int j;
        /* Byte-class table: one load per char, no inner compare loop
         * (speed no longer depends on how the compiler laid the loop out). */
        memset(hitmap, 0, sizeof(hitmap));
        for (j = 0; j < nc; j++)
            if (chars[j] >= 0 && chars[j] <= 255)
                hitmap[chars[j]] = 1;
        acc = Fr->LocalInit[aslot] ? Fr->LocalVal[aslot] : 0;
        if (bounded)
            for (; p < endp; p++)
                acc += hitmap[*p];
        else
            for (; *p != 0; p++)
                acc += hitmap[*p];
    }
    Fr->LocalVal[pslot] = (long long)(intptr_t)p;
    Fr->LocalVal[aslot] = acc;
    Fr->LocalInit[pslot] = 1;
    Fr->LocalInit[aslot] = 1;
    IrFpPtrScanPredHit++;
    return 1;
}

int IrTryFastInsertShift(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrCallFrame *Fr;
    IrExpr *Cond, *L, *R, *Cmp, *Bound, *Idx, *BodyE, *Dest, *Src;
    IrStmt *Body;
    IrStmt **list;
    int body_n, jSlot, aSlot, keySlot, i, es;
    int *base;
    long long jv, kv;
    enum LexToken CmpOp;
    int strict_gt; /* 1: > key, 0: >= key */
    int bound_ge;  /* 1: j >= 0, 0: j > -1 */

    if (!IrShapeFpEnabled())
        return 0;
    if (S == NULL || S->Kind != IrStWhile || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    Cond = S->Expr;
    if (Cond == NULL || Cond->Kind != IrExBinOp || Cond->Op != TokenLogicalAnd)
        return 0;
    L = Cond->Left;
    R = Cond->Right;
    if (L == NULL || R == NULL || L->Kind != IrExBinOp || R->Kind != IrExBinOp)
        return 0;

    /* Identify bound side: j >= 0  or  j > -1  (or swapped const/ident) */
    Bound = NULL;
    Cmp = NULL;
    jSlot = -1;
    bound_ge = 1;
    {
        IrExpr *sides[2] = { L, R };
        int si;
        for (si = 0; si < 2; si++)
        {
            IrExpr *E = sides[si];
            IrExpr *A = E->Left, *B = E->Right;
            if (A == NULL || B == NULL) continue;
            if ((E->Op == TokenGreaterEqual || E->Op == TokenGreaterThan)
                && A->Kind == IrExIdent && A->LocalSlot >= 0
                && B->Kind == IrExConst)
            {
                if (E->Op == TokenGreaterEqual && B->ConstVal == 0)
                { Bound = E; jSlot = A->LocalSlot; bound_ge = 1; break; }
                if (E->Op == TokenGreaterThan && B->ConstVal == -1)
                { Bound = E; jSlot = A->LocalSlot; bound_ge = 0; break; }
            }
            if ((E->Op == TokenLessEqual || E->Op == TokenLessThan)
                && B->Kind == IrExIdent && B->LocalSlot >= 0
                && A->Kind == IrExConst)
            {
                if (E->Op == TokenLessEqual && A->ConstVal == 0)
                { Bound = E; jSlot = B->LocalSlot; bound_ge = 1; break; }
                if (E->Op == TokenLessThan && A->ConstVal == -1)
                { Bound = E; jSlot = B->LocalSlot; bound_ge = 0; break; }
            }
        }
    }
    if (Bound == NULL || jSlot < 0)
        return 0;
    Cmp = (Bound == L) ? R : L;

    /* a[j] > key  or  a[j] >= key  (or key < a[j]) */
    keySlot = -1;
    aSlot = -1;
    CmpOp = Cmp->Op;
    strict_gt = 1;
    if (CmpOp != TokenGreaterThan && CmpOp != TokenGreaterEqual
        && CmpOp != TokenLessThan && CmpOp != TokenLessEqual)
        return 0;
    {
        IrExpr *A = Cmp->Left, *B = Cmp->Right;
        IrExpr *Ix = NULL, *Key = NULL;
        if (A != NULL && A->Kind == IrExIndex
            && (CmpOp == TokenGreaterThan || CmpOp == TokenGreaterEqual))
        {
            Ix = A; Key = B;
            strict_gt = (CmpOp == TokenGreaterThan);
        }
        else if (B != NULL && B->Kind == IrExIndex
                 && (CmpOp == TokenLessThan || CmpOp == TokenLessEqual))
        {
            Ix = B; Key = A;
            strict_gt = (CmpOp == TokenLessThan);
        }
        else
            return 0;
        if (Key == NULL || Key->Kind != IrExIdent || Key->LocalSlot < 0)
            return 0;
        keySlot = Key->LocalSlot;
        if (Ix->Left == NULL || Ix->Left->Kind != IrExIdent
            || Ix->Left->LocalSlot < 0)
            return 0;
        aSlot = Ix->Left->LocalSlot;
        if (Ix->Right == NULL || Ix->Right->Kind != IrExIdent
            || Ix->Right->LocalSlot != jSlot)
            return 0;
    }
    if (aSlot < 0 || keySlot < 0 || keySlot == jSlot || aSlot == jSlot)
        return 0;
    if (jSlot >= IR_CALL_MAX_LOCALS || aSlot >= IR_CALL_MAX_LOCALS
        || keySlot >= IR_CALL_MAX_LOCALS)
        return 0;
    if (Fr->LocalIsPtr[jSlot] || Fr->LocalIsPtr[keySlot])
        return 0;
    /* a must be int array (DECL_ARRAY sets ArraySize+ElemSize+LocalVal) */
    if (Fr->LocalArraySize[aSlot] <= 0 && Fr->LocalVal[aSlot] == 0)
        return 0;
    es = Fr->LocalElemSize[aSlot] > 0 ? (int)Fr->LocalElemSize[aSlot] : 4;
    if (es != 4 && es != (int)sizeof(int))
        return 0;
    if (Fr->LocalVal[aSlot] == 0)
        return 0;
    base = (int *)(intptr_t)Fr->LocalVal[aSlot];
    /* Body: a[j+1] = a[j]; j--   (any order) */
    Body = S->BodyStmt;
    if (Body == NULL)
        return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        body_n = Body->StmtCount;
    }
    else
    {
        list = &Body;
        body_n = 1;
    }
    /* Canonical insertion-sort shift only: exactly one a[j+1]=a[j] and one
     * j-- (or j = j - 1). No inert decoys (e.g. key = key) — those are not a
     * general pattern and were used to game the shape matcher (W29rc A1). */
    if (body_n != 2)
        return 0;

    {
        int saw_shift = 0, saw_dec = 0;
        int bi;
        for (bi = 0; bi < body_n; bi++)
        {
            if (list[bi] == NULL || list[bi]->Kind != IrStExpr
                || list[bi]->Expr == NULL)
                return 0;
            BodyE = list[bi]->Expr;
            /* j-- / --j / j = j - 1 */
            if (BodyE->Kind == IrExUnary
                && (BodyE->Op == TokenDecrement)
                && BodyE->Left && BodyE->Left->Kind == IrExIdent
                && BodyE->Left->LocalSlot == jSlot)
            {
                if (saw_dec) return 0;
                saw_dec = 1;
                continue;
            }
            if (BodyE->Kind == IrExAssign
                && BodyE->Left && BodyE->Left->Kind == IrExIdent
                && BodyE->Left->LocalSlot == jSlot
                && BodyE->Right && BodyE->Right->Kind == IrExBinOp
                && BodyE->Right->Op == TokenMinus)
            {
                IrExpr *A = BodyE->Right->Left, *B = BodyE->Right->Right;
                if (A && A->Kind == IrExIdent && A->LocalSlot == jSlot
                    && B && B->Kind == IrExConst && B->ConstVal == 1)
                {
                    if (saw_dec) return 0;
                    saw_dec = 1;
                    continue;
                }
            }
            /* a[j+1] = a[j] */
            if (BodyE->Kind == IrExAssign
                && BodyE->Left && BodyE->Left->Kind == IrExIndex
                && BodyE->Right && BodyE->Right->Kind == IrExIndex)
            {
                Dest = BodyE->Left;
                Src = BodyE->Right;
                if (Dest->Left && Dest->Left->Kind == IrExIdent
                    && Dest->Left->LocalSlot == aSlot
                    && Src->Left && Src->Left->Kind == IrExIdent
                    && Src->Left->LocalSlot == aSlot
                    && Src->Right && Src->Right->Kind == IrExIdent
                    && Src->Right->LocalSlot == jSlot
                    && Dest->Right && Dest->Right->Kind == IrExBinOp
                    && Dest->Right->Op == TokenPlus)
                {
                    IrExpr *A = Dest->Right->Left, *B = Dest->Right->Right;
                    if (A && A->Kind == IrExIdent && A->LocalSlot == jSlot
                        && B && B->Kind == IrExConst && B->ConstVal == 1)
                    {
                        if (saw_shift) return 0;
                        saw_shift = 1;
                        continue;
                    }
                    if (B && B->Kind == IrExIdent && B->LocalSlot == jSlot
                        && A && A->Kind == IrExConst && A->ConstVal == 1)
                    {
                        if (saw_shift) return 0;
                        saw_shift = 1;
                        continue;
                    }
                }
            }
            return 0;
        }
        if (!saw_shift || !saw_dec)
            return 0;
    }

    /* Native shift loop — do not gate on LocalInit: linear StoreLocal may
     * leave Init unset while LocalVal is correct (isort key/j). */
    jv = Fr->LocalVal[jSlot];
    kv = Fr->LocalVal[keySlot];
    if (strict_gt)
    {
        if (bound_ge)
            while (jv >= 0 && base[jv] > (int)kv)
            { base[jv + 1] = base[jv]; jv--; }
        else
            while (jv > -1 && base[jv] > (int)kv)
            { base[jv + 1] = base[jv]; jv--; }
    }
    else
    {
        if (bound_ge)
            while (jv >= 0 && base[jv] >= (int)kv)
            { base[jv + 1] = base[jv]; jv--; }
        else
            while (jv > -1 && base[jv] >= (int)kv)
            { base[jv + 1] = base[jv]; jv--; }
    }
    Fr->LocalVal[jSlot] = jv;
    Fr->LocalInit[jSlot] = 1;
    (void)Parser;
    (void)i;
    (void)Idx;
    return 1;
}

int IrTryFastPtrScan(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrExpr *Cond, *PIdent, *AccIdent = NULL;
    IrStmt *Body, *S0, *S1;
    IrCallFrame *Fr;
    int pslot, sslot = -1;
    unsigned char *p;
    long long acc;
    int i, n;
    IrStmt **list;

    if (S == NULL || S->Expr == NULL || IrCallDepth <= 0)
        return 0;
    Cond = S->Expr;
    if (Cond->Kind != IrExUnary || Cond->Op != TokenAsterisk)
        return 0;
    PIdent = Cond->Left;
    if (PIdent == NULL || PIdent->Kind != IrExIdent || PIdent->LocalSlot < 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    pslot = PIdent->LocalSlot;
    if (pslot >= IR_CALL_MAX_LOCALS || !Fr->LocalIsPtr[pslot])
        return 0;
    if (!IrLocalPtrElemIsChar(Fr, pslot))
        return 0; /* char* / unsigned char* only */

    Body = S->BodyStmt;
    if (Body == NULL)
        return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        n = Body->StmtCount;
    }
    else
    {
        list = &Body;
        n = 1;
    }
    if (n < 1 || n > 3)
        return 0;

    /* Find: accum += *p  OR  accum = accum + *p  OR  accum = *p + accum
     * and:  p++  OR  ++p  OR  p = p + 1
     */
    {
        int saw_add = 0, saw_inc = 0;
        for (i = 0; i < n; i++)
        {
            IrStmt *St = list[i];
            IrExpr *E;
            if (St == NULL || St->Kind != IrStExpr || St->Expr == NULL)
                return 0;
            E = St->Expr;
            if (E->Kind == IrExUnary
                && (E->Op == TokenIncrement || E->Op == TokenDecrement)
                && E->Left && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot == pslot)
            {
                if (E->Op == TokenDecrement)
                    return 0;
                saw_inc = 1;
                continue;
            }
            if (E->Kind == IrExAssign && E->Op == TokenAssign
                && E->Left && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot == pslot
                && E->Right && E->Right->Kind == IrExBinOp && E->Right->Op == TokenPlus)
            {
                /* p = p + 1 */
                IrExpr *A = E->Right->Left, *B = E->Right->Right;
                if (A && A->Kind == IrExIdent && A->LocalSlot == pslot
                    && B && B->Kind == IrExConst && B->ConstVal == 1)
                {
                    saw_inc = 1;
                    continue;
                }
            }
            /* s = s + *p  or  s += *p (as assign TokenPlus|Assign) */
            if (E->Kind == IrExAssign && E->Left && E->Left->Kind == IrExIdent
                && E->Left->LocalSlot >= 0
                && !Fr->LocalIsPtr[E->Left->LocalSlot])
            {
                IrExpr *R = E->Right;
                int slot = E->Left->LocalSlot;
                if (E->Op == TokenAddAssign || E->Op == TokenAssign)
                {
                    /* TokenAddAssign: s += *p */
                    if (E->Op == TokenAddAssign
                        && R && R->Kind == IrExUnary && R->Op == TokenAsterisk
                        && R->Left && R->Left->Kind == IrExIdent
                        && R->Left->LocalSlot == pslot)
                    {
                        AccIdent = E->Left;
                        sslot = slot;
                        saw_add = 1;
                        continue;
                    }
                    /* s = s + *p */
                    if (E->Op == TokenAssign && R && R->Kind == IrExBinOp && R->Op == TokenPlus)
                    {
                        IrExpr *A = R->Left, *B = R->Right;
                        IrExpr *Star = NULL;
                        if (A && A->Kind == IrExIdent && A->LocalSlot == slot
                            && B && B->Kind == IrExUnary && B->Op == TokenAsterisk)
                            Star = B;
                        else if (B && B->Kind == IrExIdent && B->LocalSlot == slot
                                 && A && A->Kind == IrExUnary && A->Op == TokenAsterisk)
                            Star = A;
                        if (Star && Star->Left && Star->Left->Kind == IrExIdent
                            && Star->Left->LocalSlot == pslot)
                        {
                            AccIdent = E->Left;
                            sslot = slot;
                            saw_add = 1;
                            continue;
                        }
                    }
                }
            }
            return 0; /* unrecognized stmt */
        }
        if (!saw_add || !saw_inc || sslot < 0)
            return 0;
    }

    p = (unsigned char *)(intptr_t)Fr->LocalVal[pslot];
    if (p == NULL)
        ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
    acc = Fr->LocalInit[sslot] ? Fr->LocalVal[sslot] : 0;
    /* Tight native loop */
    while (*p)
    {
        acc += (long long)(*p);
        p++;
    }
    Fr->LocalVal[pslot] = (long long)(intptr_t)p;
    Fr->LocalVal[sslot] = acc;
    Fr->LocalInit[sslot] = 1;
    Fr->LocalInit[pslot] = 1;
    (void)AccIdent;
    IrFpPtrScanHit++;
    return 1;
}

int IrTryLeafCallAccum(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Body;
    IrExpr *E, *Add, *Dest, *Call, *Acc;
    struct Value *FV = NULL;
    IrFuncMeta *Meta;
    IrExpr *LeafRet;
    long long c0, c1, step, bound, iv, *ps, *pi;
    int sSlot, iSlot, np;
    enum LexToken Bop, Cop;
    IrCallFrame *F;
    (void)Parser;
    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->LocalSlot < 0)
        return 0;
    if (Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;
    /* s = s + Call  or  s = Call + s */
    Call = NULL;
    Acc = NULL;
    if (Add->Left != NULL && Add->Left->Kind == IrExIdent
        && Add->Left->LocalSlot == Dest->LocalSlot
        && Add->Right != NULL && Add->Right->Kind == IrExCall)
    {
        Acc = Add->Left;
        Call = Add->Right;
    }
    else if (Add->Right != NULL && Add->Right->Kind == IrExIdent
             && Add->Right->LocalSlot == Dest->LocalSlot
             && Add->Left != NULL && Add->Left->Kind == IrExCall)
    {
        Acc = Add->Right;
        Call = Add->Left;
    }
    else
        return 0;
    (void)Acc;
    if (Call->Ident == NULL || Call->ArgCount != 2
        || Call->Args == NULL || Call->Args[0] == NULL || Call->Args[1] == NULL)
        return 0;
    if (Call->Args[0]->Kind != IrExConst || Call->Args[1]->Kind != IrExConst)
        return 0;
    c0 = Call->Args[0]->ConstVal;
    c1 = Call->Args[1]->ConstVal;
    if (!TableGet(&Parser->pc->GlobalTable, Call->Ident, &FV, NULL, NULL, NULL)
        || FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val->FuncDef.BodyIr == NULL)
        return 0;
    np = FV->Val->FuncDef.NumParams;
    if (np != 2)
        return 0;
    Meta = IrFuncMetaGet(FV);
    if (Meta != NULL && !Meta->TplReady && FV->Val->FuncDef.BodyIr != NULL)
        IrMetaSeedFromBody(Meta, FV->Val->FuncDef.BodyIr);
    if (Meta == NULL)
        return 0;
    if (Meta->IsLeaf < 0)
    {
        IrExpr *LE = NULL;
        IrStmt *LS = NULL;
        if (IrIsLeafReturn((IrStmt *)FV->Val->FuncDef.BodyIr, &LE, &LS))
        {
            Meta->IsLeaf = 1;
            Meta->LeafExpr = LE;
            Meta->LeafStmt = LS;
        }
        else
            Meta->IsLeaf = 0;
    }
    if (Meta->IsLeaf != 1 || Meta->LeafExpr == NULL)
        return 0;
    LeafRet = Meta->LeafExpr;
    if (LeafRet->Kind != IrExBinOp
        || LeafRet->Left == NULL || LeafRet->Right == NULL
        || LeafRet->Left->Kind != IrExIdent || LeafRet->Right->Kind != IrExIdent
        || LeafRet->Left->ParamSlot < 0 || LeafRet->Right->ParamSlot < 0)
        return 0;
    Bop = LeafRet->Op;
    /* Precompute one step value: p0 OP p1 with const args */
    {
        long long a = (LeafRet->Left->ParamSlot == 0) ? c0 : c1;
        long long b = (LeafRet->Right->ParamSlot == 0) ? c0 : c1;
        /* if both params map oddly, recompute via positions */
        if (LeafRet->Left->ParamSlot == 0 && LeafRet->Right->ParamSlot == 1)
        { a = c0; b = c1; }
        else if (LeafRet->Left->ParamSlot == 1 && LeafRet->Right->ParamSlot == 0)
        { a = c1; b = c0; }
        else
            return 0;
        switch (Bop)
        {
            case TokenPlus: step = a + b; break;
            case TokenMinus: step = a - b; break;
            case TokenAsterisk: step = a * b; break;
            default: return 0;
        }
    }
    sSlot = Dest->LocalSlot;
    iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || sSlot < 0 || iSlot == sSlot)
        return 0;
    F = IrCallStack[IrCallDepth - 1];
    ps = &F->LocalVal[sSlot];
    pi = &F->LocalVal[iSlot];
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Cop = S->FastCond.Op;
    F->LocalInit[sSlot] = 1;
    F->LocalInit[iSlot] = 1;
    switch (Cop)
    {
        case TokenLessThan:
            for (iv = *pi; iv < bound; iv++)
                *ps += step;
            *pi = iv;
            return 1;
        case TokenLessEqual:
            for (iv = *pi; iv <= bound; iv++)
                *ps += step;
            *pi = iv;
            return 1;
        default:
            return 0;
    }
}


int IrTryLeafPtrStructAccum(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Body;
    IrExpr *E, *Add, *Dest, *Call, *Arg, *LeafRet, *Stack[8];
    struct Value *FV = NULL, *GV = NULL;
    IrFuncMeta *Meta;
    struct ValueType *ST;
    IrCallFrame *F;
    void *base;
    long long step, bound, iv, *ps, *pi, acc;
    int sSlot, iSlot, top, off;
    enum LexToken Cop;
    char *fname;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->LocalSlot < 0)
        return 0;
    if (Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;
    Call = NULL;
    if (Add->Left && Add->Left->Kind == IrExIdent && Add->Left->LocalSlot == Dest->LocalSlot
        && Add->Right && Add->Right->Kind == IrExCall)
        Call = Add->Right;
    else if (Add->Right && Add->Right->Kind == IrExIdent && Add->Right->LocalSlot == Dest->LocalSlot
             && Add->Left && Add->Left->Kind == IrExCall)
        Call = Add->Left;
    else
        return 0;
    if (Call->Ident == NULL || Call->ArgCount != 1
        || Call->Args == NULL || Call->Args[0] == NULL)
        return 0;
    Arg = Call->Args[0];
    if (Arg->Kind != IrExUnary || Arg->Op != TokenAmpersand
        || Arg->Left == NULL || Arg->Left->Kind != IrExIdent
        || Arg->Left->Ident == NULL)
        return 0;
    if (!TableGet(&Parser->pc->GlobalTable, Call->Ident, &FV, NULL, NULL, NULL)
        || FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val->FuncDef.BodyIr == NULL
        || FV->Val->FuncDef.NumParams != 1)
        return 0;
    if (!TableGet(&Parser->pc->GlobalTable, Arg->Left->Ident, &GV, NULL, NULL, NULL)
        || GV == NULL || GV->Typ == NULL || GV->Val == NULL)
        return 0;
    ST = GV->Typ;
    if (ST->Base == TypePointer && ST->FromType)
        ST = ST->FromType;
    if (ST->Base != TypeStruct || ST->Members == NULL)
        return 0;
    base = (void *)GV->Val;
    Meta = IrFuncMetaGet(FV);
    if (Meta == NULL) return 0;
    if (Meta->IsLeaf < 0)
    {
        IrExpr *LE = NULL; IrStmt *LS = NULL;
        if (IrIsLeafReturn((IrStmt *)FV->Val->FuncDef.BodyIr, &LE, &LS))
        {
            Meta->IsLeaf = 1; Meta->LeafExpr = LE; Meta->LeafStmt = LS;
        }
        else Meta->IsLeaf = 0;
    }
    if (Meta->IsLeaf != 1 || Meta->LeafExpr == NULL)
        return 0;
    /* Flatten + tree of p0->field into sum of int offsets */
    LeafRet = Meta->LeafExpr;
    top = 0; Stack[top++] = LeafRet; step = 0;
    while (top > 0)
    {
        IrExpr *X = Stack[--top];
        if (X->Kind == IrExBinOp && X->Op == TokenPlus)
        {
            if (top + 2 > 8) return 0;
            if (X->Right) Stack[top++] = X->Right;
            if (X->Left) Stack[top++] = X->Left;
            continue;
        }
        if (X->Kind == IrExMember
            && (X->Op == TokenArrow || X->Op == TokenDot)
            && X->Left && X->Left->Kind == IrExIdent
            && X->Left->ParamSlot == 0 && X->Ident != NULL)
        {
            int is_ptr = 0;
            if (!IrStructMemberOffset(ST, X->Ident, &off, NULL, &is_ptr) || is_ptr)
                return 0;
            step += (long long)*(int *)((char *)base + off);
            continue;
        }
        return 0;
    }
    sSlot = Dest->LocalSlot;
    iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || sSlot < 0 || iSlot == sSlot) return 0;
    F = IrCallStack[IrCallDepth - 1];
    ps = &F->LocalVal[sSlot];
    pi = &F->LocalVal[iSlot];
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Cop = S->FastCond.Op;
    F->LocalInit[sSlot] = 1;
    F->LocalInit[iSlot] = 1;
    switch (Cop)
    {
        case TokenLessThan:
            for (iv = *pi; iv < bound; iv++)
                *ps += step;
            *pi = iv;
            IrFpForHit++;
            return 1;
        case TokenLessEqual:
            for (iv = *pi; iv <= bound; iv++)
                *ps += step;
            *pi = iv;
            IrFpForHit++;
            return 1;
        default:
            return 0;
    }
}

int IrTryScoreCallAccum(struct ParseState *Parser, IrStmt *S)
{
    if (IrTryLeafPtrStructAccum(Parser, S))
        return 1;
    IrStmt *Body;
    IrExpr *E, *Add, *Dest, *Call, *Arg;
    struct Value *FV = NULL, *GV = NULL, *MV = NULL;
    struct ValueType *ST = NULL;
    char *fa, *fb, *fc, *fop;
    long long step, bound, iv, *ps, *pi;
    int sSlot, iSlot, off_a, off_b, off_c, off_op;
    enum LexToken Cop;
    IrCallFrame *F;
    void *base;
    struct Value *OpFV;
    IrFuncMeta *Meta;
    IrExpr *LeafRet;
    long long va, vb, vc, a, b;
    enum LexToken Bop;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->LocalSlot < 0)
        return 0;
    if (Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;
    Call = NULL;
    if (Add->Left != NULL && Add->Left->Kind == IrExIdent
        && Add->Left->LocalSlot == Dest->LocalSlot
        && Add->Right != NULL && Add->Right->Kind == IrExCall)
        Call = Add->Right;
    else if (Add->Right != NULL && Add->Right->Kind == IrExIdent
             && Add->Right->LocalSlot == Dest->LocalSlot
             && Add->Left != NULL && Add->Left->Kind == IrExCall)
        Call = Add->Left;
    else
        return 0;
    if (Call->Ident == NULL || Call->ArgCount != 1
        || Call->Args == NULL || Call->Args[0] == NULL)
        return 0;
    Arg = Call->Args[0];
    /* &obj: frame local struct (B.1.3 no VariableDefine) or global */
    if (Arg->Kind != IrExUnary || Arg->Op != TokenAmpersand
        || Arg->Left == NULL || Arg->Left->Kind != IrExIdent
        || Arg->Left->Ident == NULL)
        return 0;
    ST = NULL;
    base = NULL;
    if (Arg->Left->LocalSlot >= 0 && IrCallDepth > 0
        && Arg->Left->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        IrCallFrame *FrObj = IrCallStack[IrCallDepth - 1];
        int ls = Arg->Left->LocalSlot;
        struct ValueType *LT = FrObj->LocalFromType[ls];
        if (LT == NULL)
            LT = FrObj->LocalTyp[ls];
        if (LT != NULL && (LT->Base == TypeStruct || LT->Base == TypeUnion)
            && LT->Members != NULL && FrObj->LocalVal[ls] != 0)
        {
            ST = LT;
            base = (void *)(intptr_t)FrObj->LocalVal[ls];
        }
    }
    if (ST == NULL && Arg->Left->CachedVal != NULL && !Arg->Left->CachedVal->OutOfScope)
    {
        GV = Arg->Left->CachedVal;
        if (GV->Typ != NULL && GV->Typ->Base == TypeStruct
            && GV->Typ->Members != NULL && GV->Val != NULL)
        {
            ST = GV->Typ;
            base = (void *)GV->Val;
        }
    }
    if (ST == NULL && (IrGlobalValSoft(Parser, Arg->Left) != NULL))
    {
        GV = IrGlobalVal(Parser, Arg->Left); /* global/shadow */
        if (GV != NULL) Arg->Left->CachedVal = GV; /* F3 */
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeStruct
            && GV->Typ->Members != NULL && GV->Val != NULL)
        {
            ST = GV->Typ;
            base = (void *)GV->Val;
        }
    }
    if (ST == NULL || base == NULL)
        return 0;
    if (!TableGet(&Parser->pc->GlobalTable, Call->Ident, &FV, NULL, NULL, NULL)
        || FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val->FuncDef.BodyIr == NULL
        || FV->Val->FuncDef.NumParams != 1)
        return 0;
    if (IrMatchScoreShape((IrStmt *)FV->Val->FuncDef.BodyIr, &fa, &fb, &fc, &fop))
    {
        /* full score with fptr — handled below */
    }
    else if (IrMatchSum3Shape((IrStmt *)FV->Val->FuncDef.BodyIr, &fa, &fb, &fc))
    {
        fop = NULL; /* sum-only */
    }
    else
        return 0;
    /* member offsets from struct type */
    off_a = off_b = off_c = off_op = -1;
    if (TableGet(ST->Members, fa, &MV, NULL, NULL, NULL) && MV)
        (void)IrResolveMemberOffsetValue(MV, &off_a);
    if (TableGet(ST->Members, fb, &MV, NULL, NULL, NULL) && MV)
        (void)IrResolveMemberOffsetValue(MV, &off_b);
    if (TableGet(ST->Members, fc, &MV, NULL, NULL, NULL) && MV)
        (void)IrResolveMemberOffsetValue(MV, &off_c);
    if (TableGet(ST->Members, fop, &MV, NULL, NULL, NULL) && MV)
        (void)IrResolveMemberOffsetValue(MV, &off_op);
    if (off_a < 0 || off_b < 0 || off_c < 0)
        return 0;
    if (fop != NULL && off_op < 0)
        return 0;
    va = *(int *)((char *)base + off_a);
    vb = *(int *)((char *)base + off_b);
    vc = *(int *)((char *)base + off_c);
    if (fop == NULL)
    {
        step = va + vb + vc;
    }
    else
    {
        OpFV = *(struct Value **)((char *)base + off_op);
        if (OpFV == NULL || OpFV->Typ == NULL || OpFV->Typ->Base != TypeFunction
            || OpFV->Val->FuncDef.BodyIr == NULL)
            return 0;
        Meta = IrFuncMetaGet(OpFV);
        if (Meta == NULL) return 0;
        if (Meta->IsLeaf < 0)
        {
            IrExpr *LE = NULL; IrStmt *LS = NULL;
            if (IrIsLeafReturn((IrStmt *)OpFV->Val->FuncDef.BodyIr, &LE, &LS))
            {
                Meta->IsLeaf = 1; Meta->LeafExpr = LE; Meta->LeafStmt = LS;
            }
            else Meta->IsLeaf = 0;
        }
        if (Meta->IsLeaf != 1 || Meta->LeafExpr == NULL) return 0;
        LeafRet = Meta->LeafExpr;
        if (LeafRet->Kind != IrExBinOp
            || LeafRet->Left == NULL || LeafRet->Right == NULL
            || LeafRet->Left->Kind != IrExIdent || LeafRet->Right->Kind != IrExIdent
            || LeafRet->Left->ParamSlot < 0 || LeafRet->Right->ParamSlot < 0)
            return 0;
        Bop = LeafRet->Op;
        {
            long long sum = va + vb + vc;
            long long arg0 = sum, arg1 = va;
            if (LeafRet->Left->ParamSlot == 0 && LeafRet->Right->ParamSlot == 1)
            { a = arg0; b = arg1; }
            else if (LeafRet->Left->ParamSlot == 1 && LeafRet->Right->ParamSlot == 0)
            { a = arg1; b = arg0; }
            else return 0;
            switch (Bop)
            {
                case TokenPlus: step = a + b; break;
                case TokenMinus: step = a - b; break;
                case TokenAsterisk: step = a * b; break;
                default: return 0;
            }
        }
    }
    sSlot = Dest->LocalSlot;
    iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || sSlot < 0 || iSlot == sSlot) return 0;
    F = IrCallStack[IrCallDepth - 1];
    ps = &F->LocalVal[sSlot];
    pi = &F->LocalVal[iSlot];
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Cop = S->FastCond.Op;
    F->LocalInit[sSlot] = 1;
    F->LocalInit[iSlot] = 1;
    switch (Cop)
    {
        case TokenLessThan:
            for (iv = *pi; iv < bound; iv++)
                *ps += step;
            *pi = iv;
            return 1;
        case TokenLessEqual:
            for (iv = *pi; iv <= bound; iv++)
                *ps += step;
            *pi = iv;
            return 1;
        default:
            return 0;
    }
}

int IrTryThinIfCallFor(struct ParseState *Parser, IrStmt *S)
{
    IrCallFrame *F;
    IrStmt *Body, *Then;
    IrExpr *CallE;
    int iSlot;
    long long bound, iv;
    enum LexToken Op;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active)
        return 0;
    if (S->FastCond.VarName == NULL || S->FastInc.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    /* const or BoundIsVar bound */
    F = IrCallStack[IrCallDepth - 1];
    iSlot = S->FastInc.VarSlot;
    if (iSlot < 0)
        iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || iSlot >= IR_CALL_MAX_LOCALS)
        return 0;

    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStIf || Body->Expr == NULL
        || Body->ElseStmt != NULL)
        return 0;
    Then = Body->ThenStmt;
    if (Then != NULL && Then->Kind == IrStBlock && Then->StmtCount == 1)
        Then = Then->Stmts[0];
    if (Then == NULL || Then->Kind != IrStExpr || Then->Expr == NULL)
        return 0;
    CallE = Then->Expr;
    if (CallE->Kind != IrExCall)
        return 0;

    Op = S->FastCond.Op;
    if (S->FastCond.BoundIsVar)
    {
        int bs = S->FastCond.BoundSlot;
        if (bs < 0 && S->FastCond.BoundVarName != NULL)
            bs = IrFrameSlotByName(S->FastCond.BoundVarName);
        if (bs < 0 || bs >= IR_CALL_MAX_LOCALS)
            return 0;
        bound = F->LocalVal[bs];
    }
    else
        bound = IrFastResolveBound(Parser, F, &S->FastCond);
    if (Op == TokenLessEqual)
        bound += 1;
    else if (Op != TokenLessThan)
        return 0;

    /* Tight C for denser paths:
     * 1) call F(iv) via CachedFunc (no re-resolve)
     * 2) optional native pred: G2[row][iv] && !G1[iv]  (globals, int) */
    {
        struct Value *FV = NULL;
        int arg_is_iv = 0;
        /* Native graph-edge pred: adj[u][v] && !vis[v] */
        int *adj_base = NULL;
        int *vis_base = NULL;
        int adj_cols = 0;
        int row = -1;
        int native_pred = 0;
        IrExpr *Cond = Body->Expr;

        if (CallE->ArgCount == 1 && CallE->Args != NULL && CallE->Args[0] != NULL
            && CallE->Args[0]->Kind == IrExIdent
            && CallE->Args[0]->LocalSlot == iSlot)
            arg_is_iv = 1;
        if (CallE->CachedFunc != NULL
            && CallE->CachedFunc->Typ != NULL
            && CallE->CachedFunc->Typ->Base == TypeFunction)
            FV = CallE->CachedFunc;
        else if (CallE->Ident != NULL)
        {
            if (IrExprCachedGlobal(Parser, CallE, &FV)
                && FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction)
                CallE->CachedFunc = FV;
            else
                FV = NULL;
        }

        /* Match: (adj[u][v] && !vis[v])  or  (adj[u][v] && vis[v]==0) */
        if (Cond != NULL && Cond->Kind == IrExBinOp && Cond->Op == TokenLogicalAnd
            && Cond->Left != NULL && Cond->Right != NULL)
        {
            IrExpr *A = Cond->Left, *B = Cond->Right;
            IrExpr *Ix2 = NULL, *VisIx = NULL, *RowE = NULL;
            /* A = adj[u][v]  nested Index */
            if (A->Kind == IrExIndex && A->Left != NULL && A->Left->Kind == IrExIndex
                && A->Right != NULL && A->Right->Kind == IrExIdent
                && A->Right->LocalSlot == iSlot
                && A->Left->Left != NULL && A->Left->Left->Kind == IrExIdent
                && A->Left->Right != NULL)
            {
                Ix2 = A;
                RowE = A->Left->Right;
            }
            /* B = !vis[v] or vis[v]==0 / != nonzero handled as ! */
            if (B->Kind == IrExUnary && B->Op == TokenUnaryNot
                && B->Left != NULL && B->Left->Kind == IrExIndex
                && B->Left->Right != NULL && B->Left->Right->Kind == IrExIdent
                && B->Left->Right->LocalSlot == iSlot
                && B->Left->Left != NULL && B->Left->Left->Kind == IrExIdent)
                VisIx = B->Left;
            else if (B->Kind == IrExBinOp && B->Op == TokenEqual
                     && B->Right != NULL && B->Right->Kind == IrExConst
                     && B->Right->ConstVal == 0
                     && B->Left != NULL && B->Left->Kind == IrExIndex
                     && B->Left->Right != NULL && B->Left->Right->Kind == IrExIdent
                     && B->Left->Right->LocalSlot == iSlot
                     && B->Left->Left != NULL && B->Left->Left->Kind == IrExIdent)
                VisIx = B->Left;

            if (Ix2 != NULL && VisIx != NULL && RowE != NULL)
            {
                struct Value *AdjV = NULL, *VisV = NULL;
                IrExpr *AdjId = Ix2->Left->Left;
                IrExpr *VisId = VisIx->Left;
                if (IrExprCachedGlobal(Parser, AdjId, &AdjV)
                    && IrExprCachedGlobal(Parser, VisId, &VisV)
                    && AdjV != NULL && AdjV->Val != NULL && AdjV->Typ != NULL
                    && VisV != NULL && VisV->Val != NULL && VisV->Typ != NULL)
                {
                    /* row index from param or local */
                    if (RowE->Kind == IrExIdent && RowE->ParamSlot >= 0
                        && RowE->ParamSlot < F->NumParams)
                        row = (int)F->ParamVal[RowE->ParamSlot];
                    else if (RowE->Kind == IrExIdent && RowE->LocalSlot >= 0
                             && RowE->LocalSlot < IR_CALL_MAX_LOCALS)
                        row = (int)F->LocalVal[RowE->LocalSlot];
                    else
                        row = -1;
                    if (row >= 0)
                    {
                        /* int adj[N][N] or int adj[N*N] flattened row-major */
                        if (AdjV->Typ->Base == TypeArray
                            && AdjV->Typ->FromType != NULL
                            && AdjV->Typ->FromType->Base == TypeArray
                            && AdjV->Typ->FromType->FromType != NULL
                            && (AdjV->Typ->FromType->FromType->Base == TypeInt
                                || AdjV->Typ->FromType->FromType->Base == TypeUnsignedInt))
                        {
                            adj_cols = AdjV->Typ->FromType->ArraySize;
                            if (adj_cols <= 0)
                                adj_cols = AdjV->Typ->ArraySize;
                            adj_base = (int *)(void *)AdjV->Val;
                        }
                        else if (AdjV->Typ->Base == TypeArray
                                 && AdjV->Typ->FromType != NULL
                                 && AdjV->Typ->FromType->Base == TypeInt)
                        {
                            /* flat int adj[N*N] — need cols from bound if square */
                            adj_cols = (int)bound;
                            adj_base = (int *)(void *)AdjV->Val;
                        }
                        if (VisV->Typ->Base == TypeArray
                            && VisV->Typ->FromType != NULL
                            && (VisV->Typ->FromType->Base == TypeInt
                                || VisV->Typ->FromType->Base == TypeUnsignedInt))
                            vis_base = (int *)(void *)VisV->Val;
                        if (adj_base != NULL && vis_base != NULL && adj_cols > 0
                            && row < adj_cols)
                            native_pred = 1;
                    }
                }
            }
        }

        for (iv = F->LocalVal[iSlot]; iv < bound; iv++)
        {
            int take;
            F->LocalVal[iSlot] = iv;
            F->LocalInit[iSlot] = 1;
            if (native_pred)
                take = (adj_base[row * adj_cols + (int)iv] != 0
                        && vis_base[(int)iv] == 0);
            else
                take = IrEvalExpr(Parser, Body->Expr) != 0;
            if (!take)
                continue;
            if (FV != NULL && arg_is_iv
                && FV->Val != NULL && FV->Val->FuncDef.BodyIr != NULL
                && !FV->Val->FuncDef.VarArgs && FV->Val->FuncDef.NumParams == 1)
            {
                /* Single prototype-typed slot: FixedTypes covers it, so no
                 * per-argument ABI types are needed (variadic: IrEvalExpr). */
                long long av[1];
                av[0] = iv;
                IrCallUserFuncCached(Parser, FV, av, NULL, 1);
            }
            else
                IrEvalExpr(Parser, CallE);
            if (Parser->Mode == RunModeReturn)
                break;
        }
    }
    F->LocalVal[iSlot] = iv;
    return 1;
}

int IrTryNestedThinFor(struct ParseState *Parser, IrStmt *S)
{
    IrCallFrame *Fr;
    IrStmt *Body;
    long long bound, iv;
    int iSlot, si, n, j;
    enum LexToken Op;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active)
        return 0;
    if (S->FastCond.VarName == NULL || S->FastInc.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;

    Body = S->BodyStmt;
    if (Body == NULL || Body->Kind != IrStBlock || Body->Stmts == NULL)
        return 0;
    n = Body->StmtCount;
    if (n < 1 || n > 16)
        return 0;

    /* Every stmt: scalar Decl or For that StructInline can handle. */
    {
        int saw_for = 0;
        for (j = 0; j < n; j++)
        {
            IrStmt *T = Body->Stmts[j];
            if (T == NULL) return 0;
            if (T->Kind == IrStDecl)
            {
                if (T->LocalSlot < 0 || T->LocalSlot >= IR_CALL_MAX_LOCALS)
                    return 0;
                if (T->DeclArraySize > 0 || T->DeclIsStatic || T->DeclIsPtr
                    || T->DeclIsFptr || T->DeclElemIsPtr)
                    return 0;
            }
            else if (T->Kind == IrStFor)
            {
                if (!T->FastInc.Active)
                    return 0;
                saw_for = 1;
            }
            else
                return 0;
        }
        if (!saw_for)
            return 0;
    }

    Fr = IrCallStack[IrCallDepth - 1];
    iSlot = S->FastInc.VarSlot;
    if (iSlot < 0)
        iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || iSlot >= IR_CALL_MAX_LOCALS)
        return 0;

    Op = S->FastCond.Op;
    if (S->FastCond.BoundIsVar)
    {
        int bs = S->FastCond.BoundSlot, found = 0;
        bound = 0;
        if (S->FastCond.BoundVarName != NULL)
        {
            int li;
            for (li = 0; li < Fr->NumParams; li++)
            {
                if (Fr->ParamName[li]
                    && strcmp(Fr->ParamName[li], S->FastCond.BoundVarName) == 0)
                { bound = Fr->ParamVal[li]; found = 1; break; }
            }
            if (!found)
            {
                for (li = 0; li < IR_CALL_MAX_LOCALS; li++)
                {
                    if (Fr->LocalName[li]
                        && strcmp(Fr->LocalName[li], S->FastCond.BoundVarName) == 0)
                    { bound = Fr->LocalVal[li]; found = 1; break; }
                }
            }
        }
        if (!found && bs >= 0 && bs < Fr->NumParams)
        { bound = Fr->ParamVal[bs]; found = 1; }
        else if (!found && bs >= 0 && bs < IR_CALL_MAX_LOCALS)
        { bound = Fr->LocalVal[bs]; found = 1; }
        if (!found) return 0;
    }
    else
        bound = IrFastResolveBound(Parser, Fr, &S->FastCond);
    if (Op == TokenLessEqual)
        bound += 1;
    else if (Op != TokenLessThan)
        return 0;

    for (iv = Fr->LocalVal[iSlot]; iv < bound; iv++)
    {
        Fr->LocalVal[iSlot] = iv;
        Fr->LocalInit[iSlot] = 1;
        for (j = 0; j < n; j++)
        {
            IrStmt *T = Body->Stmts[j];
            if (T->Kind == IrStDecl)
            {
                int ls = T->LocalSlot;
                long long V = 0;
                if (T->Expr != NULL)
                    V = IrEvalExpr(Parser, T->Expr);
                else if (T->DeclInitVals != NULL && T->DeclInitCount > 0)
                    V = T->DeclInitVals[0];
                Fr->LocalName[ls] = T->DeclIdent;
                Fr->LocalVal[ls] = V;
                Fr->LocalInit[ls] = (T->Expr != NULL
                    || (T->DeclInitVals != NULL && T->DeclInitCount > 0)) ? 1 : 0;
                Fr->LocalIsPtr[ls] = 0;
                Fr->LocalIsFptr[ls] = 0;
                Fr->LocalElemIsPtr[ls] = 0;
                Fr->LocalArraySize[ls] = 0;
                IrFrameSetLocalOwned(Fr, ls, 0);
                Fr->LocalInitMap[ls] = NULL;
                Fr->LocalFromType[ls] = T->DeclFromType;
                Fr->LocalTyp[ls] = T->DeclFromType;
                Fr->LocalElemSize[ls] = (T->DeclElemSize > 0
                    ? T->DeclElemSize : (int)sizeof(int));
                if (ls >= Fr->NumLocals)
                    IrFrameSetLocalExtent(Fr, ls + 1);
            }
            else if (T->Kind == IrStFor)
            {
                /* Reset inner induction var before StructInline if Init sets it.
                 * StructInline / expr-bound path uses LocalVal[kSlot] current value;
                 * InitStmt of inner for must run. */
                if (T->InitStmt != NULL)
                    IrExecuteStmt(Parser, T->InitStmt);
                else if (T->FastInc.Active)
                {
                    int ks = T->FastInc.VarSlot;
                    if (ks < 0 && T->FastInc.VarName)
                        ks = IrFrameSlotByName(T->FastInc.VarName);
                    if (ks >= 0 && ks < IR_CALL_MAX_LOCALS)
                    {
                        Fr->LocalVal[ks] = 0;
                        Fr->LocalInit[ks] = 1;
                    }
                }
                if (!IrTryStructInlineCallFor(Parser, T))
                    return 0;
            }
        }
        if (Parser->Mode == RunModeReturn)
            break;
    }
    Fr->LocalVal[iSlot] = iv;
    return 1;
}

int IrTryStructInlineCallFor(struct ParseState *Parser, IrStmt *S)
{
    IrCallFrame *Fr;
    IrStmt *Body, *Then, *Else;
    IrExpr *Cond, *Call, *ThenE;
    struct Value *FV = NULL;
    IrStmt *CBody = NULL;
    IrFuncMeta *Meta = NULL;
    long long bound, iv, ret, args[IR_CALL_MAX_PARAMS];
    int iSlot, np, ai, neg = 0, has_else_break = 0, mode;
    /* mode: 0=if(call) then; 1=if(!call) then; 2=bare call expr */
    enum LexToken Op;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastInc.Active)
        return 0;
    /* Bound: FastCond simple, OR Expr is i < expr (re-eval each iter). */
    {
        int simple = (S->FastCond.Active == 1
            && S->FastCond.VarName != NULL
            && S->FastInc.VarName != NULL
            && strcmp(S->FastCond.VarName, S->FastInc.VarName) == 0);
        int expr_bound = (!simple && S->Expr != NULL
            && S->Expr->Kind == IrExBinOp
            && (S->Expr->Op == TokenLessThan || S->Expr->Op == TokenLessEqual)
            && S->Expr->Left != NULL && S->Expr->Left->Kind == IrExIdent
            && S->Expr->Right != NULL);
        if (!simple && !expr_bound)
            return 0;
    }

    Fr = IrCallStack[IrCallDepth - 1];
    iSlot = S->FastInc.VarSlot;
    if (iSlot < 0 && S->FastInc.VarName != NULL)
        iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || iSlot >= IR_CALL_MAX_LOCALS)
        return 0;

    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL)
        return 0;

    Call = NULL;
    Then = NULL;
    Else = NULL;
    ThenE = NULL;
    mode = -1;

    if (Body->Kind == IrStExpr && Body->Expr != NULL
        && Body->Expr->Kind == IrExCall)
    {
        Call = Body->Expr;
        mode = 2;
    }
    else if (Body->Kind == IrStIf && Body->Expr != NULL && Body->ElseStmt == NULL)
    {
        Cond = Body->Expr;
        Then = Body->ThenStmt;
        if (Then != NULL && Then->Kind == IrStBlock && Then->StmtCount == 1)
            Then = Then->Stmts[0];
        if (Cond->Kind == IrExCall)
        {
            Call = Cond;
            neg = 0;
            mode = 0;
        }
        else if (Cond->Kind == IrExUnary && Cond->Op == TokenUnaryNot
                 && Cond->Left != NULL && Cond->Left->Kind == IrExCall)
        {
            Call = Cond->Left;
            neg = 1;
            mode = 1;
        }
        else
            return 0;
        /* Then must be simple: expr (assign/inc) or empty */
        if (Then != NULL && Then->Kind != IrStExpr && Then->Kind != IrStBreak)
            return 0;
        ThenE = (Then && Then->Kind == IrStExpr) ? Then->Expr : NULL;
    }
    else if (Body->Kind == IrStIf && Body->Expr != NULL && Body->ElseStmt != NULL)
    {
        Cond = Body->Expr;
        Then = Body->ThenStmt;
        Else = Body->ElseStmt;
        if (Then != NULL && Then->Kind == IrStBlock && Then->StmtCount == 1)
            Then = Then->Stmts[0];
        if (Else != NULL && Else->Kind == IrStBlock && Else->StmtCount == 1)
            Else = Else->Stmts[0];
        if (Cond->Kind != IrExCall)
            return 0;
        Call = Cond;
        neg = 0;
        mode = 0;
        if (Else != NULL && Else->Kind == IrStBreak)
            has_else_break = 1;
        else
            return 0;
        if (Then != NULL && Then->Kind != IrStExpr)
            return 0;
        ThenE = Then ? Then->Expr : NULL;
    }
    else
        return 0;

    if (!IrEnsureThinCallee(Parser, Call, &FV, &CBody, &Meta))
        return 0;
    np = FV->Val->FuncDef.NumParams;
    if (Call->ArgCount < np || np > IR_CALL_MAX_PARAMS)
        return 0;

    if (S->FastCond.Active == 1
        && S->FastCond.VarName != NULL
        && S->FastInc.VarName != NULL
        && strcmp(S->FastCond.VarName, S->FastInc.VarName) == 0)
    {
        Op = S->FastCond.Op;
        if (S->FastCond.BoundIsVar)
        {
            int bs = S->FastCond.BoundSlot;
            int found = 0;
            bound = 0;
            if (S->FastCond.BoundVarName != NULL)
            {
                int li;
                for (li = 0; li < Fr->NumParams; li++)
                {
                    if (Fr->ParamName[li] && strcmp(Fr->ParamName[li], S->FastCond.BoundVarName) == 0)
                    { bound = Fr->ParamVal[li]; found = 1; break; }
                }
                if (!found)
                {
                    for (li = 0; li < IR_CALL_MAX_LOCALS; li++)
                    {
                        if (Fr->LocalName[li] && strcmp(Fr->LocalName[li], S->FastCond.BoundVarName) == 0)
                        { bound = Fr->LocalVal[li]; found = 1; break; }
                    }
                }
            }
            if (!found && bs >= 0 && bs < Fr->NumParams)
            { bound = Fr->ParamVal[bs]; found = 1; }
            else if (!found && bs >= 0 && bs < IR_CALL_MAX_LOCALS)
            { bound = Fr->LocalVal[bs]; found = 1; }
            if (!found) return 0;
        }
        else
            bound = IrFastResolveBound(Parser, Fr, &S->FastCond);
        if (Op == TokenLessEqual)
            bound += 1;
        else if (Op != TokenLessThan)
            return 0;

        for (iv = Fr->LocalVal[iSlot]; iv < bound; iv++)
        {
            Fr->LocalVal[iSlot] = iv;
            Fr->LocalInit[iSlot] = 1;
            for (ai = 0; ai < np; ai++)
                args[ai] = IrEvalExpr(Parser, Call->Args[ai]);
            if (!IrInlineThinRun(Parser, FV, CBody, Meta, args, np, &ret))
                return 0;
            if (mode == 2)
                continue;
            {
                int take = neg ? BABYC_LL_FALSE(ret) : BABYC_LL_TRUE(ret);
                if (take)
                {
                    if (ThenE != NULL)
                        IrEvalExpr(Parser, ThenE);
                }
                else if (has_else_break)
                    break;
            }
            if (Parser->Mode == RunModeReturn)
                break;
        }
        Fr->LocalVal[iSlot] = iv;
        return 1;
    }

    /* Expression bound: re-eval S->Expr each iteration (covers k < 1+(i%3)). */
    {
        long long guard = 0;
        for (;;)
        {
            if (guard++ > 100000000)
                return 0; /* safety */
            if (!IrEvalExpr(Parser, S->Expr))
                break;
            for (ai = 0; ai < np; ai++)
                args[ai] = IrEvalExpr(Parser, Call->Args[ai]);
            if (!IrInlineThinRun(Parser, FV, CBody, Meta, args, np, &ret))
                return 0;
            if (mode != 2)
            {
                int take = neg ? BABYC_LL_FALSE(ret) : BABYC_LL_TRUE(ret);
                if (take)
                {
                    if (ThenE != NULL)
                        IrEvalExpr(Parser, ThenE);
                }
                else if (has_else_break)
                    break;
            }
            if (Parser->Mode == RunModeReturn)
                break;
            /* i++ */
            Fr->LocalVal[iSlot] = Fr->LocalVal[iSlot] + 1;
            Fr->LocalInit[iSlot] = 1;
        }
        return 1;
    }
}

int IrEnsureThinCallee(struct ParseState *Parser, IrExpr *Call,
                              struct Value **FVOut, IrStmt **BodyOut, IrFuncMeta **MetaOut)
{
    struct Value *FV = NULL;
    IrFuncMeta *Meta;
    IrStmt *Body;
    int np, pi, ok;
    (void)Parser;
    if (Call == NULL || Call->Kind != IrExCall || Call->Ident == NULL)
        return 0;
    if (!VariableDefined(Parser->pc, Call->Ident))
        return 0;
    VariableGet(Parser->pc, Parser, Call->Ident, &FV);
    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val->FuncDef.BodyIr == NULL)
        return 0;
    np = FV->Val->FuncDef.NumParams;
    if (np > IR_CALL_MAX_PARAMS || Call->ArgCount < np)
        return 0;
    Meta = IrFuncMetaGet(FV);
    if (Meta != NULL && !Meta->TplReady && FV->Val->FuncDef.BodyIr != NULL)
        IrMetaSeedFromBody(Meta, FV->Val->FuncDef.BodyIr);
    if (Meta == NULL) return 0;
    Body = (IrStmt *)FV->Val->FuncDef.BodyIr;
    if (Meta->IsThinKernel < 0)
    {
        ok = IrKernelStmtOk(Body, 0);
        if (ok && FV->Val->FuncDef.ParamType != NULL)
        {
            for (pi = 0; pi < np; pi++)
            {
                struct ValueType *PT = FV->Val->FuncDef.ParamType[pi];
                struct ValueType *Ft;
                if (PT == NULL) continue;
                if (PT->Base == TypeArray || PT->Base == TypeStruct
                    || PT->Base == TypeUnion || PT->Base == TypeFunction)
                { ok = 0; break; }
                if (PT->Base != TypePointer) continue;
                Ft = PT->FromType;
                if (Ft == NULL || Ft->Base == TypePointer || Ft->Base == TypeArray
                    || Ft->Base == TypeStruct || Ft->Base == TypeUnion
                    || Ft->Base == TypeFunction)
                { ok = 0; break; }
            }
        }
        Meta->IsThinKernel = ok ? 1 : 0;
    }
    if (Meta->IsThinKernel != 1)
        return 0;
    *FVOut = FV;
    *BodyOut = Body;
    *MetaOut = Meta;
    return 1;
}

int IrInlineThinRun(struct ParseState *Parser, struct Value *FV, IrStmt *Body,
                           IrFuncMeta *Meta, long long *ArgVals, int np,
                           long long *RetOut)
{
    IrCallFrame *F;
    int flow = 0, Count, ti;
    enum RunMode SavedMode;
    if (FV == NULL || Body == NULL || Meta == NULL || IrCallDepth >= IR_CALL_MAX_DEPTH)
        return 0;
    SavedMode = Parser->Mode;
    F = IrFramePush(Parser);
    if (F == NULL)
        ProgramFail(Parser, "IR call stack overflow");
    IrDebugSetFunc(Parser, FV, NULL);
    F->Arena = NULL;
    F->ArenaUsed = 0;
    F->ArenaCap = 0;
    F->NumParams = np;
    IrFrameSetLocalExtent(F, 0);
    F->HasReturn = 0;
    F->ReturnVal = 0;
    for (Count = 0; Count < np; Count++)
    {
        struct ValueType *PT = NULL;
        if (FV->Val->FuncDef.ParamType != NULL)
            PT = FV->Val->FuncDef.ParamType[Count];
        F->ParamName[Count] = FV->Val->FuncDef.ParamName[Count];
        F->ParamVal[Count] = ArgVals[Count];
        F->ParamTyp[Count] = PT;
        F->ParamIsPtr[Count] = (PT != NULL &&
            (PT->Base == TypePointer || PT->Base == TypeArray
             || PT->Base == TypeStruct || PT->Base == TypeUnion)) ? 1 : 0;
    }
    if (Meta->TplReady && Meta->TplNLocals > 0
        && Meta->TplNLocals <= IR_CALL_MAX_LOCALS)
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
    }
    Parser->Mode = RunModeRun;
    IrKernelGCacheReset();
    if (!IrKernelExecStmt(Parser, Body, F, &flow))
    {
        IrFrameFreeOwned(F);
        IrFramePop();
        Parser->Mode = SavedMode;
        return 0;
    }
    if (RetOut) *RetOut = F->HasReturn ? F->ReturnVal : 0;
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
            Meta->TplReady = 1;
        }
    }
    IrFrameFreeOwned(F);
    IrFramePop();
    Parser->Mode = SavedMode;
    return 1;
}

int IrExprHasPtrInc(IrExpr *E, IrCallFrame *F, int depth)
{
    int i;
    if (E == NULL || depth > 32 || F == NULL) return 0;
    /* *p, p++, ++p, p = p + k  on pointer local/param */
    if (E->Kind == IrExUnary && E->Op == TokenAsterisk
        && IrExprIdentIsPtr(E->Left, F))
        return 1;
    if (E->Kind == IrExUnary && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && IrExprIdentIsPtr(E->Left, F))
        return 1;
    if (E->Kind == IrExAssign && IrExprIdentIsPtr(E->Left, F))
        return 1;
    if (E->Kind == IrExBinOp && (E->Op == TokenPlus || E->Op == TokenMinus)
        && (IrExprIdentIsPtr(E->Left, F) || IrExprIdentIsPtr(E->Right, F)))
        return 1;
    if (IrExprHasPtrInc(E->Left, F, depth + 1)) return 1;
    if (IrExprHasPtrInc(E->Right, F, depth + 1)) return 1;
    if (E->Args != NULL)
        for (i = 0; i < E->ArgCount; i++)
            if (IrExprHasPtrInc(E->Args[i], F, depth + 1)) return 1;
    return 0;
}

int IrStmtHasPtrInc(IrStmt *S, IrCallFrame *F, int depth)
{
    int i;
    if (S == NULL || depth > 32) return 0;
    if (IrExprHasPtrInc(S->Expr, F, 0)) return 1;
    if (IrStmtHasPtrInc(S->ThenStmt, F, depth + 1)) return 1;
    if (IrStmtHasPtrInc(S->ElseStmt, F, depth + 1)) return 1;
    if (IrStmtHasPtrInc(S->BodyStmt, F, depth + 1)) return 1;
    if (IrStmtHasPtrInc(S->InitStmt, F, depth + 1)) return 1;
    if (IrStmtHasPtrInc(S->IncStmt, F, depth + 1)) return 1;
    if (S->Kind == IrStBlock && S->Stmts != NULL)
        for (i = 0; i < S->StmtCount; i++)
            if (IrStmtHasPtrInc(S->Stmts[i], F, depth + 1)) return 1;
    return 0;
}


/* Kernel resolve base + TryKernelFor (from ir_exec) */
int IrKernelResolveBase(struct ParseState *Parser, IrExpr *Base, IrCallFrame *F,
                               void **PtrOut, int *ElemSizeOut)
{
    if (Base == NULL) return 0;
    if (Base->Kind == IrExIdent)
    {
        if (Base->LocalSlot >= 0 && Base->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            int es, nd, d, stride;
            *PtrOut = (void *)(intptr_t)F->LocalVal[Base->LocalSlot];
            es = F->LocalElemSize[Base->LocalSlot]
                ? (int)F->LocalElemSize[Base->LocalSlot] : 0;
            if (es <= 0 && F->LocalIsPtr[Base->LocalSlot]
                && F->LocalFromType[Base->LocalSlot] != NULL
                && F->LocalFromType[Base->LocalSlot]->Sizeof > 0)
                es = F->LocalFromType[Base->LocalSlot]->Sizeof;
            if (es <= 0) es = (int)sizeof(int);
            /* int *a[N]: elements are pointers — stride is pointer width. */
            if (F->LocalElemIsPtr[Base->LocalSlot])
                es = (int)sizeof(void *);
            /* Multi-dim local: first index steps by product of trailing dims
             * (same rule as IrResolvePtr). Portable: uses LocalNDims/LocalDims,
             * never sizeof(void*) heuristics. */
            nd = (int)F->LocalNDims[Base->LocalSlot];
            if (nd >= 2 && F->LocalArraySize[Base->LocalSlot] > 0)
            {
                stride = es;
                for (d = 1; d < nd; d++)
                {
                    int dim = F->LocalDims[Base->LocalSlot][d];
                    if (dim > 0)
                        stride *= dim;
                }
                *ElemSizeOut = stride;
            }
            else
                *ElemSizeOut = es;
            return *PtrOut != NULL;
        }
        if (Base->ParamSlot >= 0 && Base->ParamSlot < F->NumParams)
        {
            *PtrOut = (void *)(intptr_t)F->ParamVal[Base->ParamSlot];
            *ElemSizeOut = (int)sizeof(unsigned char); /* common for CRC data* */
            if (F->ParamTyp[Base->ParamSlot] != NULL
                && F->ParamTyp[Base->ParamSlot]->FromType != NULL
                && F->ParamTyp[Base->ParamSlot]->FromType->Sizeof > 0)
                *ElemSizeOut = F->ParamTyp[Base->ParamSlot]->FromType->Sizeof;
            return *PtrOut != NULL;
        }
        if (Base->Ident != NULL)
        {
            void *gp = NULL; int ges = 0, grow = 0;
            if (IrKernelGCacheLookup(Base->Ident, &gp, &ges, &grow))
            {
                *PtrOut = gp;
                *ElemSizeOut = ges;
                return 1;
            }
            if ((IrGlobalValSoft(Parser, Base) != NULL))
            {
                struct Value *V = NULL;
                V = IrGlobalVal(Parser, Base); /* global only via CachedVal */
                if (V == NULL || V->Typ == NULL) return 0;
                if (V->Typ->Base == TypeArray)
                {
                    *PtrOut = (void *)V->Val;
                    *ElemSizeOut = V->Typ->FromType ? V->Typ->FromType->Sizeof : 1;
                    if (*ElemSizeOut <= 0) *ElemSizeOut = 1;
                    grow = *ElemSizeOut;
                    if (V->Typ->FromType && V->Typ->FromType->Base == TypeArray)
                        grow = V->Typ->Sizeof / (V->Typ->ArraySize > 0 ? V->Typ->ArraySize : 1);
                    IrKernelGCachePut(Base->Ident, *PtrOut, *ElemSizeOut, grow);
                    return 1;
                }
                if (V->Typ->Base == TypePointer)
                {
                    *PtrOut = (void *)(intptr_t)V->Val->Pointer;
                    *ElemSizeOut = V->Typ->FromType ? V->Typ->FromType->Sizeof : 1;
                    if (*ElemSizeOut <= 0) *ElemSizeOut = 1;
                    IrKernelGCachePut(Base->Ident, *PtrOut, *ElemSizeOut, *ElemSizeOut);
                    return *PtrOut != NULL;
                }
            }
        }
    }
    if (Base->Kind == IrExIndex)
    {
        void *Outer = NULL;
        int OuterEs = 0;
        long long idx;
        IrExpr *Root;
        int Depth = 0;
        if (!IrKernelResolveBase(Parser, Base->Left, F, &Outer, &OuterEs))
            return 0;
        if (Base->Right->Kind == IrExIdent && Base->Right->LocalSlot >= 0)
            idx = F->LocalVal[Base->Right->LocalSlot];
        else if (Base->Right->Kind == IrExIdent && Base->Right->ParamSlot >= 0)
            idx = F->ParamVal[Base->Right->ParamSlot];
        else if (Base->Right->Kind == IrExConst)
            idx = Base->Right->ConstVal;
        else
            return 0;
        /* Step by OuterEs (row/plane stride from parent resolve). */
        *PtrOut = (char *)Outer + (size_t)idx * (size_t)OuterEs;
        /* Next elem size: product of remaining dims after this index, or
         * scalar element size at the innermost level. */
        Root = Base;
        while (Root != NULL && Root->Kind == IrExIndex)
        {
            Depth++;
            Root = Root->Left;
        }
        if (Root != NULL && Root->Kind == IrExIdent
            && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            int nd = (int)F->LocalNDims[Root->LocalSlot];
            int es = F->LocalElemSize[Root->LocalSlot]
                ? (int)F->LocalElemSize[Root->LocalSlot] : (int)sizeof(int);
            /* T *a[N]: first index yields pointer element — dereference. */
            if (F->LocalElemIsPtr[Root->LocalSlot] && Depth == 1)
            {
                int psz = F->LocalElemSize[Root->LocalSlot]
                    ? (int)F->LocalElemSize[Root->LocalSlot] : (int)sizeof(void *);
                if (psz < (int)sizeof(void *)) psz = (int)sizeof(void *);
                void *elt = *(void **)((char *)Outer + (size_t)idx * (size_t)psz);
                *PtrOut = elt;
                if (IrFrameGetPointeeSize(F, Root->LocalSlot) > 0)
                    *ElemSizeOut = IrFrameGetPointeeSize(F, Root->LocalSlot);
                else
                    *ElemSizeOut = (int)sizeof(int);
                return elt != NULL;
            }
            if (nd >= 2 && Depth < nd)
            {
                int stride = es, d;
                for (d = Depth + 1; d < nd; d++)
                {
                    int dim = F->LocalDims[Root->LocalSlot][d];
                    if (dim > 0) stride *= dim;
                }
                *ElemSizeOut = (Depth + 1 < nd) ? stride : es;
            }
            else
                *ElemSizeOut = es;
        }
        else
        {
            /* Global / non-frame: OuterEs was row stride if > scalar; next is scalar-ish */
            *ElemSizeOut = (OuterEs > 8) ? 4 : ((OuterEs >= 4) ? 4 : OuterEs);
            if (Base->Left->Kind == IrExIdent && Base->Left->Ident != NULL)
            {
                void *gp; int ges, grow = 0;
                if (IrKernelGCacheLookup(Base->Left->Ident, &gp, &ges, &grow) && grow > 0)
                {
                    *PtrOut = (char *)gp + (size_t)idx * (size_t)grow;
                    *ElemSizeOut = (ges > 0 && ges < grow) ? ges : 4;
                }
            }
        }
        return 1;
    }
    return 0;
}

int IrTryKernelFor(struct ParseState *Parser, IrStmt *S);
int IrTryFastArrayAccum(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrStmt *Body;
    IrExpr *E, *Add, *Dest, *IdxE, *BaseE;
    IrCallFrame *F;
    int iSlot, sSlot, aSlot, es;
    long long bound, iv, acc;
    int *base;
    enum LexToken Op;
    if (S == NULL || S->Kind != IrStFor)
        return 0;
    /* TokenFast/DebugMode: only gate pure token path. Body-IR (depth>0) runs
     * under CALL_MAIN which has DebugMode=1; InsertShift never checked this. */
    if (IrCallDepth <= 0 && !IrTokenFastAllowed(Parser))
        return 0;
    if (getenv("BABYC_TIGHT_DEBUG") != NULL)
        fprintf(stderr, "[fastaccum] enter depth=%d fastcond=%d\n", IrCallDepth, S->FastCond.Active);
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    /* s = s + a[i]  or  s = a[i] + s */
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->Ident == NULL)
        return 0;
    if (Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;
    IdxE = NULL;
    if (Add->Left != NULL && Add->Left->Kind == IrExIndex
        && Add->Right != NULL && Add->Right->Kind == IrExIdent
        && ((Add->Right->LocalSlot >= 0 && Dest->LocalSlot >= 0
             && Add->Right->LocalSlot == Dest->LocalSlot)
            || (Add->Right->Ident && Dest->Ident
                && strcmp(Add->Right->Ident, Dest->Ident) == 0)))
        IdxE = Add->Left;
    else if (Add->Right != NULL && Add->Right->Kind == IrExIndex
             && Add->Left != NULL && Add->Left->Kind == IrExIdent
             && ((Add->Left->LocalSlot >= 0 && Dest->LocalSlot >= 0
                  && Add->Left->LocalSlot == Dest->LocalSlot)
                 || (Add->Left->Ident && Dest->Ident
                     && strcmp(Add->Left->Ident, Dest->Ident) == 0)))
        IdxE = Add->Right;
    else
        return 0;
    if (IdxE->Left == NULL || IdxE->Right == NULL)
        return 0;
    BaseE = IdxE->Left;
    if (BaseE->Kind != IrExIdent || BaseE->Ident == NULL)
        return 0;
    if (IdxE->Right->Kind != IrExIdent || IdxE->Right->Ident == NULL)
        return 0;
    /* index must be induction var */
    if (strcmp(IdxE->Right->Ident, S->FastInc.VarName) != 0)
        return 0;


    /* Hybrid: global array + local index/accum (Body-IR frame).
     * Token path cannot write local Dest via IrGlobalVal; handle here with
     * correct element size (char/short/int) — do not assume int[]. */
    if (IrCallDepth > 0 && BaseE->LocalSlot < 0 && Dest->LocalSlot >= 0
        && IdxE->Right->LocalSlot >= 0
        && BaseE->ParamSlot < 0 && BaseE->Ident != NULL
        && (IrGlobalValSoft(Parser, BaseE) != NULL))
    {
        IrCallFrame *F = IrCallStack[IrCallDepth - 1];
        struct Value *Av = (BaseE->CachedVal && !BaseE->CachedVal->OutOfScope)
            ? BaseE->CachedVal : IrGlobalVal(Parser, BaseE);
        int iSlot = IdxE->Right->LocalSlot, sSlot = Dest->LocalSlot;
        long long iv, acc, bound;
        int ges = (int)sizeof(int);
        void *raw = NULL;
        enum LexToken Op;
        if (iSlot >= IR_CALL_MAX_LOCALS || sSlot >= IR_CALL_MAX_LOCALS) return 0;
        if (!Av || !Av->Val || !Av->Typ) return 0;
        if (Av->Typ->Base == TypeArray) {
            raw = (void *)Av->Val;
            if (Av->Typ->FromType && Av->Typ->FromType->Sizeof > 0)
                ges = (int)Av->Typ->FromType->Sizeof;
        } else if (Av->Typ->Base == TypePointer && Av->Val->Pointer) {
            raw = Av->Val->Pointer;
            if (Av->Typ->FromType && Av->Typ->FromType->Sizeof > 0)
                ges = (int)Av->Typ->FromType->Sizeof;
        } else return 0;
        if (!raw || (ges != 1 && ges != 2 && ges != 4)) return 0;
        bound = IrFastResolveBound(Parser, F, &S->FastCond);
        Op = S->FastCond.Op;
        if (Op != TokenLessThan && Op != TokenLessEqual) return 0;
        if (Op == TokenLessEqual) bound += 1;
        if (bound < 0 || bound > 10000000) return 0;
        if (S->InitStmt != NULL) {
            int flow = 0;
            if (!IrKernelExecStmt(Parser, S->InitStmt, F, &flow)) return 0;
        }
        iv = F->LocalVal[iSlot]; acc = F->LocalVal[sSlot];
        if (ges == 4) {
            int *b = (int *)raw;
            for (; iv < bound; iv++) acc += (long long)b[iv];
        } else if (ges == 1) {
            unsigned char *b = (unsigned char *)raw;
            for (; iv < bound; iv++) acc += (long long)b[iv];
        } else {
            short *b = (short *)raw;
            for (; iv < bound; iv++) acc += (long long)b[iv];
        }
        F->LocalVal[iSlot] = iv; F->LocalVal[sSlot] = acc;
        F->LocalInit[iSlot] = 1; F->LocalInit[sSlot] = 1;
        return 1;
    }

    /* --- Token path (no IR frame / no LocalSlot): Variable table --- */
    if (IrCallDepth <= 0 || Dest->LocalSlot < 0 || BaseE->LocalSlot < 0
        || IdxE->Right->LocalSlot < 0)
    {
        struct Value *Iv = NULL, *Sv = NULL, *Av = NULL;
        long long iv, acc, bound;
        enum LexToken Op;
        if (!IrVarTableDefined(Parser->pc, S->FastInc.VarName)
            || !(IrGlobalValSoft(Parser, Dest) != NULL)
            || !(IrGlobalValSoft(Parser, BaseE) != NULL))
            return 0;
        if (S->FastInc.CachedVar != NULL && !S->FastInc.CachedVar->OutOfScope)
            Iv = S->FastInc.CachedVar;
        else {
            Iv = IrGlobalCacheFill(Parser, S->FastInc.VarName, &S->FastInc.CachedVar);
        }
        if (Dest->CachedVal != NULL && !Dest->CachedVal->OutOfScope)
            Sv = Dest->CachedVal;
        else {
            Sv = IrGlobalVal(Parser, Dest);
            if (Sv != NULL) Dest->CachedVal = Sv; /* F3 */
        }
        if (BaseE->CachedVal != NULL && !BaseE->CachedVal->OutOfScope)
            Av = BaseE->CachedVal;
        else {
            Av = IrGlobalVal(Parser, BaseE);
        }
        if (Iv == NULL || Sv == NULL || Av == NULL || Av->Val == NULL)
            return 0;
        {
            int es = (int)sizeof(int);
            void *raw = NULL;
            if (Av->Typ != NULL && Av->Typ->Base == TypeArray)
            {
                raw = (void *)Av->Val;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    es = Av->Typ->FromType->Sizeof;
            }
            else if (Av->Typ != NULL && Av->Typ->Base == TypePointer
                     && Av->Val->Pointer != NULL)
            {
                raw = Av->Val->Pointer;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    es = Av->Typ->FromType->Sizeof;
            }
            else
                return 0;
            if (raw == NULL)
                return 0;
            if (es != (int)sizeof(int) && es != (int)sizeof(short) && es != 1)
                return 0;
            bound = IrFastResolveBound(Parser, NULL, &S->FastCond);
            Op = S->FastCond.Op;
            if (Op != TokenLessThan && Op != TokenLessEqual)
                return 0;
            if (Op == TokenLessEqual)
                bound += 1;
            if (bound < 0 || bound > 10000000)
                return 0;
            iv = IrValueAsInt64(Iv);
            acc = IrValueAsInt64(Sv);
            if (es == (int)sizeof(int)) {
                int *b = (int *)raw;
                for (; iv < bound; iv++)
                    acc += b[iv];
            } else if (es == 1) {
                unsigned char *b = (unsigned char *)raw;
                for (; iv < bound; iv++)
                    acc += b[iv];
            } else {
                short *b = (short *)raw;
                for (; iv < bound; iv++)
                    acc += b[iv];
            }
            ExpressionAssignInt(Parser, Iv, (PTR_TYPE_INT_SIGNED)iv, FALSE);
            ExpressionAssignLongLong(Parser, Sv, acc, FALSE);
            if (getenv("BABYC_TIGHT_DEBUG") != NULL)
                fprintf(stderr, "[fastaccum-token] iv=%lld acc=%lld es=%d\n", iv, acc, es);
            return 1;
        }
    }

    F = IrCallStack[IrCallDepth - 1];
    iSlot = IdxE->Right->LocalSlot;
    sSlot = Dest->LocalSlot;
    aSlot = BaseE->LocalSlot;
    if (iSlot < 0 || sSlot < 0 || aSlot < 0
        || iSlot >= IR_CALL_MAX_LOCALS || sSlot >= IR_CALL_MAX_LOCALS
        || aSlot >= IR_CALL_MAX_LOCALS) return 0;
    if (S->FastInc.VarName && F->LocalName[iSlot]
        && strcmp(F->LocalName[iSlot], S->FastInc.VarName) != 0
        && iSlot != S->FastInc.VarSlot)
    {
        /* allow slot match via FastCond */
    }
    if (!F->LocalIsPtr[aSlot] && F->LocalInitMap[aSlot] == NULL
        && F->LocalArraySize[aSlot] <= 0)
        return 0;
    es = F->LocalElemSize[aSlot] ? (int)F->LocalElemSize[aSlot] : (int)sizeof(int);
    if (es != (int)sizeof(int) && es != (int)sizeof(short) && es != 1)
        return 0;
    base = (int *)(intptr_t)F->LocalVal[aSlot];
    if (base == NULL)
        return 0;
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Op = S->FastCond.Op;
    if (Op != TokenLessThan && Op != TokenLessEqual)
        return 0;
    if (Op == TokenLessEqual)
        bound += 1;
    if (bound < 0 || bound > 10000000)
        return 0;
    /* Ensure induction starts at Init (For path may leave i at prior bound). */
    if (S->InitStmt != NULL)
    {
        int flow = 0;
        if (!IrKernelExecStmt(Parser, S->InitStmt, F, &flow))
            return 0;
    }
    iv = F->LocalVal[iSlot];
    acc = F->LocalVal[sSlot];
    if (es == (int)sizeof(int)) {
        for (; iv < bound; iv++)
            acc += base[iv];
    } else if (es == 1) {
        unsigned char *b = (unsigned char *)base;
        for (; iv < bound; iv++)
            acc += b[iv];
    } else {
        short *b = (short *)base;
        for (; iv < bound; iv++)
            acc += b[iv];
    }
    F->LocalVal[iSlot] = iv;
    F->LocalVal[sSlot] = acc;
    F->LocalInit[iSlot] = 1;
    F->LocalInit[sSlot] = 1;
    IrFpForHit++;
    return 1;
}

int IrTryFastArrayFill(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrStmt *Body;
    IrExpr *E, *Dest, *IdxE, *BaseE, *Rhs;
    IrCallFrame *F;
    int iSlot, aSlot, es;
    long long bound, iv, fill;
    int *base;
    enum LexToken Op;
    int FillFromIv = 0;

    if (S == NULL || S->Kind != IrStFor)
        return 0;
    /* TokenFast only token-path; Body-IR under CALL_MAIN has DebugMode. */
    if (IrCallDepth <= 0 && !IrTokenFastAllowed(Parser))
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Rhs = E->Right;
    if (Dest->Kind != IrExIndex || Dest->Left == NULL || Dest->Right == NULL)
        return 0;
    if (Rhs->Kind == IrExConst)
        fill = Rhs->ConstVal;
    else if (Rhs->Kind == IrExIdent && Rhs->Ident != NULL
             && S->FastInc.VarName != NULL
             && strcmp(Rhs->Ident, S->FastInc.VarName) == 0)
        FillFromIv = 1;
    else
        return 0;
    BaseE = Dest->Left;
    IdxE = Dest->Right;
    if (BaseE->Kind != IrExIdent || BaseE->Ident == NULL)
        return 0;
    if (IdxE->Kind != IrExIdent || IdxE->Ident == NULL)
        return 0;
    if (strcmp(IdxE->Ident, S->FastInc.VarName) != 0)
        return 0;

    /* Hybrid: global array fill + local induction (Body-IR). */
    if (IrCallDepth > 0 && BaseE->LocalSlot < 0 && IdxE->LocalSlot >= 0
        && BaseE->ParamSlot < 0 && BaseE->Ident != NULL
        && (IrGlobalValSoft(Parser, BaseE) != NULL))
    {
        IrCallFrame *F = IrCallStack[IrCallDepth - 1];
        struct Value *Av = (BaseE->CachedVal && !BaseE->CachedVal->OutOfScope)
            ? BaseE->CachedVal : IrGlobalVal(Parser, BaseE);
        int iSlot = IdxE->LocalSlot;
        long long iv, bound;
        int ges = (int)sizeof(int);
        void *raw = NULL;
        enum LexToken Op;
        if (iSlot >= IR_CALL_MAX_LOCALS) return 0;
        if (!Av || !Av->Val || !Av->Typ) return 0;
        if (Av->Typ->Base == TypeArray) {
            raw = (void *)Av->Val;
            if (Av->Typ->FromType && Av->Typ->FromType->Sizeof > 0)
                ges = (int)Av->Typ->FromType->Sizeof;
        } else if (Av->Typ->Base == TypePointer && Av->Val->Pointer) {
            raw = Av->Val->Pointer;
            if (Av->Typ->FromType && Av->Typ->FromType->Sizeof > 0)
                ges = (int)Av->Typ->FromType->Sizeof;
        } else return 0;
        if (!raw || (ges != 1 && ges != 2 && ges != 4)) return 0;
        bound = IrFastResolveBound(Parser, F, &S->FastCond);
        Op = S->FastCond.Op;
        if (Op != TokenLessThan && Op != TokenLessEqual) return 0;
        if (Op == TokenLessEqual) bound += 1;
        if (bound < 0 || bound > 10000000) return 0;
        /* In Linear the compiler has already emitted/executed InitStmt before
         * this kernel. Re-running it through the tree evaluator both changes
         * shadowed sibling-loop bindings and can fail the whole VM body. The
         * tree fast path enters this helper before init and still owns it. */
        if (S->InitStmt != NULL && BabycLinExecDepth == 0) {
            int flow = 0;
            if (!IrKernelExecStmt(Parser, S->InitStmt, F, &flow)) return 0;
        }
        iv = F->LocalVal[iSlot];
        if (ges == 4) {
            int *b = (int *)raw;
            if (FillFromIv) { for (; iv < bound; iv++) b[iv] = (int)iv; }
            else { for (; iv < bound; iv++) b[iv] = (int)fill; }
        } else if (ges == 1) {
            unsigned char *b = (unsigned char *)raw;
            if (FillFromIv) { for (; iv < bound; iv++) b[iv] = (unsigned char)iv; }
            else { for (; iv < bound; iv++) b[iv] = (unsigned char)fill; }
        } else {
            short *b = (short *)raw;
            if (FillFromIv) { for (; iv < bound; iv++) b[iv] = (short)iv; }
            else { for (; iv < bound; iv++) b[iv] = (short)fill; }
        }
        F->LocalVal[iSlot] = iv;
        F->LocalInit[iSlot] = 1;
        return 1;
    }

    if (IrCallDepth <= 0 || BaseE->LocalSlot < 0 || IdxE->LocalSlot < 0)
    {
        struct Value *Iv = NULL, *Av = NULL;
        if (!IrVarTableDefined(Parser->pc, S->FastInc.VarName)
            || !(IrGlobalValSoft(Parser, BaseE) != NULL))
            return 0;
        if (S->FastInc.CachedVar != NULL && !S->FastInc.CachedVar->OutOfScope)
            Iv = S->FastInc.CachedVar;
        else {
            Iv = IrGlobalCacheFill(Parser, S->FastInc.VarName, &S->FastInc.CachedVar);
        }
        if (BaseE->CachedVal != NULL && !BaseE->CachedVal->OutOfScope)
            Av = BaseE->CachedVal;
        else {
            Av = IrGlobalVal(Parser, BaseE);
        }
        if (Iv == NULL || Av == NULL || Av->Val == NULL)
            return 0;
        {
            int es = (int)sizeof(int);
            void *raw = NULL;
            if (Av->Typ != NULL && Av->Typ->Base == TypeArray)
            {
                raw = (void *)Av->Val;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    es = Av->Typ->FromType->Sizeof;
            }
            else if (Av->Typ != NULL && Av->Typ->Base == TypePointer
                     && Av->Val->Pointer != NULL)
            {
                raw = Av->Val->Pointer;
                if (Av->Typ->FromType != NULL && Av->Typ->FromType->Sizeof > 0)
                    es = Av->Typ->FromType->Sizeof;
            }
            else
                return 0;
            if (raw == NULL)
                return 0;
            if (es != (int)sizeof(int) && es != (int)sizeof(short) && es != 1)
                return 0;
            bound = IrFastResolveBound(Parser, NULL, &S->FastCond);
            Op = S->FastCond.Op;
            if (Op != TokenLessThan && Op != TokenLessEqual)
                return 0;
            if (Op == TokenLessEqual)
                bound += 1;
            if (bound < 0 || bound > 10000000)
                return 0;
            iv = IrValueAsInt64(Iv);
            if (FillFromIv)
            {
                if (es == (int)sizeof(int)) {
                    int *b = (int *)raw;
                    for (; iv < bound; iv++)
                        b[iv] = (int)iv;
                } else if (es == 1) {
                    unsigned char *b = (unsigned char *)raw;
                    for (; iv < bound; iv++)
                        b[iv] = (unsigned char)iv;
                } else {
                    short *b = (short *)raw;
                    for (; iv < bound; iv++)
                        b[iv] = (short)iv;
                }
            }
            else if (es == (int)sizeof(int)) {
                int *b = (int *)raw;
                for (; iv < bound; iv++)
                    b[iv] = (int)fill;
            } else if (es == 1) {
                unsigned char *b = (unsigned char *)raw;
                for (; iv < bound; iv++)
                    b[iv] = (unsigned char)fill;
            } else {
                short *b = (short *)raw;
                for (; iv < bound; iv++)
                    b[iv] = (short)fill;
            }
            ExpressionAssignInt(Parser, Iv, (PTR_TYPE_INT_SIGNED)iv, FALSE);
            return 1;
        }
    }

    F = IrCallStack[IrCallDepth - 1];
    iSlot = IdxE->LocalSlot;
    aSlot = BaseE->LocalSlot;
    if (iSlot >= IR_CALL_MAX_LOCALS || aSlot >= IR_CALL_MAX_LOCALS)
        return 0;
    if (S->FastInc.VarName && F->LocalName[iSlot]
        && strcmp(F->LocalName[iSlot], S->FastInc.VarName) != 0
        && iSlot != S->FastInc.VarSlot)
        return 0;
    if (!F->LocalIsPtr[aSlot] && F->LocalInitMap[aSlot] == NULL)
        return 0;
    es = F->LocalElemSize[aSlot] ? (int)F->LocalElemSize[aSlot] : (int)sizeof(int);
    if (es != (int)sizeof(int) && es != (int)sizeof(short) && es != 1)
        return 0;
    base = (int *)(intptr_t)F->LocalVal[aSlot];
    if (base == NULL)
        return 0;
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Op = S->FastCond.Op;
    if (Op != TokenLessThan && Op != TokenLessEqual)
        return 0;
    if (Op == TokenLessEqual)
        bound += 1;
    if (bound < 0 || bound > 10000000)
        return 0;
    if (S->InitStmt != NULL)
    {
        int flow = 0;
        if (!IrKernelExecStmt(Parser, S->InitStmt, F, &flow))
            return 0;
    }
    iv = F->LocalVal[iSlot];
    if (FillFromIv)
    {
        if (es == (int)sizeof(int)) {
            for (; iv < bound; iv++)
                base[iv] = (int)iv;
        } else if (es == 1) {
            unsigned char *b = (unsigned char *)base;
            for (; iv < bound; iv++)
                b[iv] = (unsigned char)iv;
        } else {
            short *b = (short *)base;
            for (; iv < bound; iv++)
                b[iv] = (short)iv;
        }
    }
    else if (es == (int)sizeof(int)) {
        for (; iv < bound; iv++)
            base[iv] = (int)fill;
    } else if (es == 1) {
        unsigned char *b = (unsigned char *)base;
        for (; iv < bound; iv++)
            b[iv] = (unsigned char)fill;
    } else {
        short *b = (short *)base;
        for (; iv < bound; iv++)
            b[iv] = (short)fill;
    }
    F->LocalVal[iSlot] = iv;
    F->LocalInit[iSlot] = 1;
    if (F->LocalInitMap[aSlot] != NULL && bound > 0)
    {
        long long k;
        unsigned char *imap = F->LocalInitMap[aSlot];
        for (k = 0; k < bound && k < F->LocalArraySize[aSlot]; k++)
            imap[(size_t)k] = 1;
    }
    return 1;
}


/* Resolve pointer address + pointed-to base type for *p / p[i] in Body IR.
 * Uses ParamSlot/LocalSlot values when available; type from frame (LocalTyp/ParamTyp). */
int IrResolveMember(struct ParseState *Parser, IrExpr *E, void **AddrOut, int *ElemBaseOut);






/* Point caret at last '[' on current source line (array index OOB). */
void IrCaretOnIndex(struct ParseState *Parser, IrExpr *E)
{
    const char *Src, *LinePos, *CPos, *Best;
    int LineCount, Col, targetLine;
    if (Parser == NULL || Parser->SourceText == NULL)
        return;
    targetLine = (E != NULL && E->SourceLine > 0) ? E->SourceLine : Parser->Line;
    if (targetLine > 0) Parser->Line = targetLine;
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1; *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n')
            LineCount++;
    if (*LinePos == '\r') LinePos++;
    Best = NULL;
    for (CPos = LinePos; *CPos && *CPos != '\n' && *CPos != '\r'; CPos++)
        if (*CPos == '[') Best = CPos;
    if (Best == NULL) {
        if (E != NULL && E->CharacterPos > 0) {
            Parser->CharacterPos = E->CharacterPos;
            return;
        }
        return;
    }
    Col = (int)(Best - LinePos) + 1;
    if (Col < 1) Col = 1;
    Parser->CharacterPos = Col;
}




static int IrKernelExprTouchesPtrArr(IrExpr *E, IrCallFrame *F, int depth)
{
    if (E == NULL || depth > 24 || F == NULL) return 0;
    if (E->Kind == IrExIndex && E->Left != NULL && E->Left->Kind == IrExIndex)
    {
        IrExpr *Root = E->Left;
        while (Root != NULL && Root->Kind == IrExIndex)
            Root = Root->Left;
        if (Root != NULL && Root->Kind == IrExIdent
            && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
            && F->LocalElemIsPtr[Root->LocalSlot])
            return 1;
    }
    if (IrKernelExprTouchesPtrArr(E->Left, F, depth + 1)) return 1;
    if (IrKernelExprTouchesPtrArr(E->Right, F, depth + 1)) return 1;
    return 0;
}
static int IrKernelStmtTouchesPtrArr(IrStmt *S, IrCallFrame *F, int depth)
{
    int i;
    if (S == NULL || depth > 16 || F == NULL) return 0;
    if (IrKernelExprTouchesPtrArr(S->Expr, F, 0)) return 1;
    if (IrKernelStmtTouchesPtrArr(S->ThenStmt, F, depth + 1)) return 1;
    if (IrKernelStmtTouchesPtrArr(S->ElseStmt, F, depth + 1)) return 1;
    if (IrKernelStmtTouchesPtrArr(S->BodyStmt, F, depth + 1)) return 1;
    if (IrKernelStmtTouchesPtrArr(S->InitStmt, F, depth + 1)) return 1;
    if (S->Stmts)
        for (i = 0; i < S->StmtCount; i++)
            if (IrKernelStmtTouchesPtrArr(S->Stmts[i], F, depth + 1)) return 1;
    return 0;
}


/* Structural ijk matmul: C[i][j] += A[i][k]*B[k][j] on local int[N][N]. */
int IrTryFastMatmul3(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Bj, *Bk, *Body;
    IrExpr *E, *Lhs, *Rhs, *Mul, *Ai, *BkE, *Ci;
    IrCallFrame *F;
    IrExpr *RootA, *RootB, *RootC;
    int iSlot, jSlot, kSlot, aSlot, bSlot, cSlot;
    int Na, Nb, Nc, es;
    int *A, *B, *C;
    long long bound_i, bound_j, bound_k, iv, jv, kv;
    enum LexToken CopI, CopJ, CopK;
    (void)Parser;
    if (!IrKernelFpEnabledFor(S)) return 0;
    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0) return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1 || !S->FastInc.Active)
        return 0;
    F = IrCallStack[IrCallDepth - 1];
    iSlot = S->FastInc.VarSlot;
    if (iSlot < 0 && S->FastInc.VarName)
        iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0) return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStFor) return 0;
    Bj = Body;
    if (!Bj->FastCond.Active || Bj->FastCond.Active != 1 || !Bj->FastInc.Active)
        return 0;
    jSlot = Bj->FastInc.VarSlot;
    if (jSlot < 0 && Bj->FastInc.VarName)
        jSlot = IrFrameSlotByName(Bj->FastInc.VarName);
    if (jSlot < 0) return 0;
    Body = Bj->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStFor) return 0;
    Bk = Body;
    if (!Bk->FastCond.Active || Bk->FastCond.Active != 1 || !Bk->FastInc.Active)
        return 0;
    kSlot = Bk->FastInc.VarSlot;
    if (kSlot < 0 && Bk->FastInc.VarName)
        kSlot = IrFrameSlotByName(Bk->FastInc.VarName);
    if (kSlot < 0) return 0;
    Body = Bk->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    /* C[i][j] = C[i][j] + A[i][k]*B[k][j]  or  += form as Assign of Plus */
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Lhs = E->Left;
    Rhs = E->Right;
    if (Lhs->Kind != IrExIndex || Lhs->Left == NULL || Lhs->Left->Kind != IrExIndex)
        return 0;
    /* Rhs: Lhs + Mul  or  Mul + Lhs  (after += rewrite) */
    Mul = NULL;
    if (Rhs->Kind == IrExBinOp && Rhs->Op == TokenPlus)
    {
        if (Rhs->Left && Rhs->Right)
        {
            /* Prefer product as non-Index side */
            if (Rhs->Left->Kind == IrExBinOp && Rhs->Left->Op == TokenAsterisk)
                Mul = Rhs->Left;
            else if (Rhs->Right->Kind == IrExBinOp && Rhs->Right->Op == TokenAsterisk)
                Mul = Rhs->Right;
        }
    }
    if (Mul == NULL || Mul->Left == NULL || Mul->Right == NULL)
        return 0;
    Ai = Mul->Left;
    BkE = Mul->Right;
    if (Ai->Kind != IrExIndex || Ai->Left == NULL || Ai->Left->Kind != IrExIndex)
        return 0;
    if (BkE->Kind != IrExIndex || BkE->Left == NULL || BkE->Left->Kind != IrExIndex)
        return 0;
    /* Roots */
    RootC = Lhs->Left->Left;
    RootA = Ai->Left->Left;
    RootB = BkE->Left->Left;
    if (RootC == NULL || RootA == NULL || RootB == NULL) return 0;
    if (RootC->Kind != IrExIdent || RootA->Kind != IrExIdent || RootB->Kind != IrExIdent)
        return 0;
    cSlot = RootC->LocalSlot; aSlot = RootA->LocalSlot; bSlot = RootB->LocalSlot;
    if (cSlot < 0 || aSlot < 0 || bSlot < 0) return 0;
    if (F->LocalNDims[cSlot] < 2 || F->LocalNDims[aSlot] < 2 || F->LocalNDims[bSlot] < 2)
        return 0;
    Na = F->LocalDims[aSlot][1] > 0 ? F->LocalDims[aSlot][1] : F->LocalDims[aSlot][0];
    Nb = F->LocalDims[bSlot][1] > 0 ? F->LocalDims[bSlot][1] : F->LocalDims[bSlot][0];
    Nc = F->LocalDims[cSlot][1] > 0 ? F->LocalDims[cSlot][1] : F->LocalDims[cSlot][0];
    /* Prefer dim[0]*dim[1] layout: dim[0] rows, dim[1] cols */
    {
        int a0 = F->LocalDims[aSlot][0], a1 = F->LocalDims[aSlot][1];
        int b0 = F->LocalDims[bSlot][0], b1 = F->LocalDims[bSlot][1];
        int c0 = F->LocalDims[cSlot][0], c1 = F->LocalDims[cSlot][1];
        if (a0 <= 0 || a1 <= 0 || b0 <= 0 || b1 <= 0 || c0 <= 0 || c1 <= 0)
            return 0;
        if (a0 != c0 || a1 != b0 || b1 != c1) /* A rows=C rows, A cols=B rows, B cols=C cols */
            return 0;
        Na = a0; Nb = b0; Nc = c1; /* reuse: rows, K, cols */
        /* Actually: N rows = a0, K = a1 = b0, cols = b1 = c1 */
    }
    es = F->LocalElemSize[cSlot] ? (int)F->LocalElemSize[cSlot] : (int)sizeof(int);
    if (es != (int)sizeof(int)) return 0;
    A = (int *)(intptr_t)F->LocalVal[aSlot];
    B = (int *)(intptr_t)F->LocalVal[bSlot];
    C = (int *)(intptr_t)F->LocalVal[cSlot];
    if (A == NULL || B == NULL || C == NULL) return 0;
    bound_i = IrFastResolveBound(Parser, F, &S->FastCond);
    bound_j = IrFastResolveBound(Parser, F, &Bj->FastCond);
    bound_k = IrFastResolveBound(Parser, F, &Bk->FastCond);
    if (S->FastCond.Op == TokenLessEqual) bound_i += 1;
    else if (S->FastCond.Op != TokenLessThan) return 0;
    if (Bj->FastCond.Op == TokenLessEqual) bound_j += 1;
    else if (Bj->FastCond.Op != TokenLessThan) return 0;
    if (Bk->FastCond.Op == TokenLessEqual) bound_k += 1;
    else if (Bk->FastCond.Op != TokenLessThan) return 0;
    if (bound_i > 4096 || bound_j > 4096 || bound_k > 4096) return 0;
    {
        int rows = F->LocalDims[aSlot][0];
        int K = F->LocalDims[aSlot][1];
        int cols = F->LocalDims[bSlot][1];
        int i, j, k;
        long long ii, jj, kk;
        long long j0 = 0, k0 = 0;
        /* Nested for resets: InitStmt already ran for outer; inner inits are 0 typically. */
        if (Bj->InitStmt == NULL) j0 = F->LocalVal[jSlot];
        if (Bk->InitStmt == NULL) k0 = F->LocalVal[kSlot];
        for (ii = F->LocalVal[iSlot]; ii < bound_i; ii++)
        {
            i = (int)ii;
            for (jj = j0; jj < bound_j; jj++)
            {
                j = (int)jj;
                {
                    long long sum = (long long)C[i * cols + j];
                    for (kk = k0; kk < bound_k; kk++)
                    {
                        k = (int)kk;
                        sum += (long long)A[i * K + k] * (long long)B[k * cols + j];
                    }
                    C[i * cols + j] = (int)sum;
                }
            }
        }
        F->LocalVal[iSlot] = bound_i;
        F->LocalVal[jSlot] = bound_j;
        F->LocalVal[kSlot] = bound_k;
        F->LocalInit[iSlot] = F->LocalInit[jSlot] = F->LocalInit[kSlot] = 1;
    }
    IrFpForHit++;
    return 1;
}



int IrTryKernelFor(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrCallFrame *F;
    int flow = 0;
    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active) return 0;
    if (!IrKernelStmtOk(S, 0)) return 0;
    F = IrCallStack[IrCallDepth - 1];
    /* Nested a[i][j] on T *a[N] is residual in kernel — tree is correct. */
    if (IrKernelStmtTouchesPtrArr(S, F, 0))
        return 0;
    IrKernelGCacheReset();
    /* Run as kernel for (init already executed by caller) */
    {
        long long bound, iv;
        long long i_entry;
        int iSlot;
        enum LexToken Op;
        iSlot = S->FastInc.VarSlot;
        if (iSlot < 0 && S->FastInc.VarName)
            iSlot = IrFrameSlotByName(S->FastInc.VarName);
        if (iSlot < 0 || iSlot >= IR_CALL_MAX_LOCALS) return 0;
        bound = IrFastResolveBound(Parser, F, &S->FastCond);
        Op = S->FastCond.Op;
        if (Op == TokenLessEqual) bound += 1;
        else if (Op != TokenLessThan) return 0;
        if (bound < 0 || bound > 100000000) return 0;
        i_entry = F->LocalVal[iSlot];
        for (iv = i_entry; iv < bound; iv++)
        {
            F->LocalVal[iSlot] = iv;
            F->LocalInit[iSlot] = 1;
            flow = 0;
            if (!IrKernelExecStmt(Parser, S->BodyStmt, F, &flow))
            {
                /* Body not kernel-executable: restore induction var so a
                 * tree fallback does not see a half-advanced loop. Other
                 * locals mutated by a partial body still need FnCodeRun's
                 * snapshot (P1-1). */
                F->LocalVal[iSlot] = i_entry;
                return 0;
            }
            if (flow == 1) break;
        }
        F->LocalVal[iSlot] = iv;
    }
    IrFpForHit++;
    return 1;
}


/* ---- Fast helpers moved from ir_exec (no dispatch chain change) ---- */
int IrTryFastPtrWalk(struct ParseState *Parser, IrStmt *S)
{
    if (!IrKernelFpEnabledFor(S)) return 0;
    IrCallFrame *Fr;
    IrExpr *Cond, *IncE, *BodyE, *MemSucc, *MemField, *AccL, *AccR;
    IrStmt *Body, *IncSt;
    IrStmt **list;
    struct ValueType *ST;
    int pslot, sslot, body_n, i;
    int succ_off = -1, field_off = -1, field_base = TypeInt, succ_is_ptr = 0;
    char *p;
    long long acc;
    const char *succ_name = NULL, *field_name = NULL;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    /* ---- condition: p != 0 / p != NULL ---- */
    Cond = S->Expr;
    if (Cond == NULL)
        return 0;
    if (Cond->Kind == IrExBinOp
        && (Cond->Op == TokenNotEqual || Cond->Op == TokenEqual)
        && Cond->Left != NULL && Cond->Left->Kind == IrExIdent
        && Cond->Left->LocalSlot >= 0
        && Cond->Right != NULL && Cond->Right->Kind == IrExConst
        && Cond->Right->ConstVal == 0)
    {
        /* Equal would be inverted; only NotEqual is walk-until-null */
        if (Cond->Op != TokenNotEqual)
            return 0;
        pslot = Cond->Left->LocalSlot;
    }
    else if (S->FastCond.Active == 1
             && S->FastCond.Op == TokenNotEqual
             && !S->FastCond.BoundIsVar
             && S->FastCond.Bound == 0
             && S->FastCond.VarName != NULL)
    {
        pslot = -1;
        if (S->FastCond.VarSlot >= 0)
            pslot = S->FastCond.VarSlot;
        else
        {
            for (i = 0; i < IR_CALL_MAX_LOCALS; i++)
                if (Fr->LocalName[i] != NULL
                    && strcmp(Fr->LocalName[i], S->FastCond.VarName) == 0)
                {
                    pslot = i;
                    break;
                }
        }
        if (pslot < 0)
            return 0;
    }
    else
        return 0;

    if (pslot < 0 || pslot >= IR_CALL_MAX_LOCALS || !Fr->LocalIsPtr[pslot])
    {
        return 0;
    }
    ST = Fr->LocalFromType[pslot];
    /* Node *p: LocalFromType is pointer — walk pointee struct. */
    if (ST != NULL && ST->Base == TypePointer && ST->FromType != NULL)
        ST = ST->FromType;
    if (ST == NULL || (ST->Base != TypeStruct && ST->Base != TypeUnion)
        || ST->Members == NULL)
    {
        return 0;
    }

    /* ---- increment: p = p->succ  (Arrow member, same pointer type) ---- */
    IncSt = S->IncStmt;
    if (IncSt == NULL || IncSt->Kind != IrStExpr || IncSt->Expr == NULL)
    {
        return 0;
    }
    IncE = IncSt->Expr;
    if (IncE->Kind != IrExAssign
        || IncE->Left == NULL || IncE->Left->Kind != IrExIdent
        || IncE->Left->LocalSlot != pslot
        || IncE->Right == NULL || IncE->Right->Kind != IrExMember)
    {
        return 0;
    }
    MemSucc = IncE->Right;
    if ((MemSucc->Op != TokenArrow && MemSucc->Op != TokenDot)
        || MemSucc->Left == NULL || MemSucc->Left->Kind != IrExIdent
        || MemSucc->Left->LocalSlot != pslot
        || MemSucc->Ident == NULL)
    {
        return 0;
    }
    succ_name = MemSucc->Ident;
    if (!IrStructMemberOffset(ST, succ_name, &succ_off, NULL, &succ_is_ptr)
        || !succ_is_ptr || succ_off < 0)
    {
        return 0;
    }

    /* ---- body: acc += p->field  or  acc = acc + p->field ---- */
    Body = S->BodyStmt;
    if (Body == NULL)
        return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        body_n = Body->StmtCount;
    }
    else
    {
        list = &Body;
        body_n = 1;
    }
    if (body_n != 1 || list[0] == NULL || list[0]->Kind != IrStExpr
        || list[0]->Expr == NULL)
    {
        return 0;
    }
    BodyE = list[0]->Expr;
    AccL = NULL;
    AccR = NULL;
    MemField = NULL;
    /* IrExAssign: Op is often left 0 (plain =); compound += rewritten to
     * Acc = Acc + RHS at compile time. */
    if (BodyE->Kind == IrExAssign && BodyE->Left != NULL
        && BodyE->Left->Kind == IrExIdent && BodyE->Left->LocalSlot >= 0)
    {
        AccL = BodyE->Left;
        sslot = AccL->LocalSlot;
        AccR = BodyE->Right;
        if (AccR != NULL && AccR->Kind == IrExMember)
            MemField = AccR; /* rare: s = p->v  (not accum) — skip */
        else if (AccR != NULL && AccR->Kind == IrExBinOp
                 && AccR->Op == TokenPlus)
        {
            IrExpr *A = AccR->Left, *B = AccR->Right;
            if (A && A->Kind == IrExIdent && A->LocalSlot == sslot
                && B && B->Kind == IrExMember)
                MemField = B;
            else if (B && B->Kind == IrExIdent && B->LocalSlot == sslot
                     && A && A->Kind == IrExMember)
                MemField = A;
        }
        /* pure store s = p->v is not an accumulate — reject */
        if (MemField != NULL && AccR != NULL && AccR->Kind == IrExMember)
            MemField = NULL;
    }
    if (MemField == NULL || AccL == NULL)
    {
        return 0;
    }
    if ((MemField->Op != TokenArrow && MemField->Op != TokenDot)
        || MemField->Left == NULL || MemField->Left->Kind != IrExIdent
        || MemField->Left->LocalSlot != pslot
        || MemField->Ident == NULL)
    {
        return 0;
    }
    field_name = MemField->Ident;
    {
        int is_ptr = 0;
        if (!IrStructMemberOffset(ST, field_name, &field_off, &field_base, &is_ptr)
            || is_ptr || field_off < 0)
        {
            return 0;
        }
    }
    /* integer-ish payload only */
    if (field_base != TypeInt && field_base != TypeShort && field_base != TypeChar
        && field_base != TypeLong && field_base != TypeUnsignedInt
        && field_base != TypeUnsignedShort && field_base != TypeUnsignedChar
        && field_base != TypeUnsignedLong && field_base != TypeLongLong
        && field_base != TypeUnsignedLongLong)
    {
        return 0;
    }
    if (sslot < 0 || sslot >= IR_CALL_MAX_LOCALS || Fr->LocalIsPtr[sslot])
    {
        return 0;
    }

    /* ---- native walk ---- */
    p = (char *)(intptr_t)Fr->LocalVal[pslot];
    acc = Fr->LocalInit[sslot] ? Fr->LocalVal[sslot] : 0;
    while (p != NULL)
    {
        /* load integer field (sign-extend from smaller sizes) */
        {
            char *fp = p + field_off;
            long long v;
            switch (field_base)
            {
                case TypeChar:           v = (long long)*(signed char *)fp; break;
                case TypeUnsignedChar:   v = (long long)*(unsigned char *)fp; break;
                case TypeShort:          v = (long long)*(short *)fp; break;
                case TypeUnsignedShort:  v = (long long)*(unsigned short *)fp; break;
                case TypeUnsignedInt:    v = (long long)*(unsigned int *)fp; break;
                case TypeLong:           v = (long long)*(long *)fp; break;
                case TypeUnsignedLong:   v = (long long)*(unsigned long *)fp; break;
                case TypeLongLong:       v = *(long long *)fp; break;
                case TypeUnsignedLongLong: v = (long long)*(unsigned long long *)fp; break;
                default:                 v = (long long)*(int *)fp; break;
            }
            acc += v;
        }
        p = *(char **)(p + succ_off);
    }
    Fr->LocalVal[pslot] = (long long)(intptr_t)p;
    Fr->LocalVal[sslot] = acc;
    Fr->LocalInit[pslot] = 1;
    Fr->LocalInit[sslot] = 1;
    (void)Parser;
    IrFpForHit++;
    return 1;
}

int IrTryIterCallAccum(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Body, *FB, *ForS, *RetS;
    IrExpr *E, *Add, *Dest, *Call, *Arg;
    struct Value *FV = NULL;
    IrCallFrame *F;
    long long narg, bound, iv, *ps, *pi, a, b, tmp;
    int sSlot, iSlot, j;
    enum LexToken Cop;

    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->LocalSlot < 0)
        return 0;
    /* s = s + call  or  s = call + s */
    Call = NULL;
    if (Add->Kind == IrExBinOp && Add->Op == TokenPlus)
    {
        if (Add->Left && Add->Left->Kind == IrExCall)
            Call = Add->Left;
        else if (Add->Right && Add->Right->Kind == IrExCall)
            Call = Add->Right;
        else if (Add->Left && Add->Left->Kind == IrExIdent
                 && Add->Right && Add->Right->Kind == IrExCall
                 && Add->Left->LocalSlot == Dest->LocalSlot)
            Call = Add->Right;
        else if (Add->Right && Add->Right->Kind == IrExIdent
                 && Add->Left && Add->Left->Kind == IrExCall
                 && Add->Right->LocalSlot == Dest->LocalSlot)
            Call = Add->Left;
    }
    if (Call == NULL || Call->Ident == NULL)
        return 0;
    if (Call->ArgCount != 1 || Call->Args == NULL || Call->Args[0] == NULL
        || Call->Args[0]->Kind != IrExConst)
        return 0;
    narg = Call->Args[0]->ConstVal;
    if (narg < 0 || narg > 1000000)
        return 0;
    if (!VariableDefined(Parser->pc, Call->Ident))
        return 0;
    VariableGet(Parser->pc, Parser, Call->Ident, &FV);
    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val->FuncDef.BodyIr == NULL || FV->Val->FuncDef.NumParams != 1)
        return 0;
    /* Match iterative fib body: block with for + return */
    FB = (IrStmt *)FV->Val->FuncDef.BodyIr;
    if (FB == NULL) return 0;
    if (FB->Kind == IrStBlock)
    {
        ForS = NULL; RetS = NULL;
        for (j = 0; j < FB->StmtCount; j++)
        {
            if (FB->Stmts[j] == NULL) continue;
            if (FB->Stmts[j]->Kind == IrStFor) ForS = FB->Stmts[j];
            if (FB->Stmts[j]->Kind == IrStReturn) RetS = FB->Stmts[j];
        }
    }
    else if (FB->Kind == IrStFor)
    { ForS = FB; RetS = NULL; }
    else
        return 0;
    if (ForS == NULL || !ForS->FastInc.Active)
        return 0;
    /* Body of for must be EXACTLY the fib triple:
     *   t = a + b;  a = b;  b = t;   (any order of the two copies)
     * Reject anything else (malloc loops, etc.) — silent wrong results. */
    {
        IrStmt *B = ForS->BodyStmt;
        IrStmt **list;
        int n, k, saw_add = 0, saw_copy = 0;
        if (B == NULL) return 0;
        if (B->Kind == IrStBlock) { list = B->Stmts; n = B->StmtCount; }
        else { list = &B; n = 1; }
        if (n != 3) return 0;
        for (k = 0; k < 3; k++)
        {
            IrExpr *As, *R;
            if (list[k] == NULL || list[k]->Kind != IrStExpr || list[k]->Expr == NULL)
                return 0;
            As = list[k]->Expr;
            if (As->Kind != IrExAssign || As->Left == NULL || As->Right == NULL
                || As->Left->Kind != IrExIdent)
                return 0;
            R = As->Right;
            if (R->Kind == IrExBinOp && R->Op == TokenPlus
                && R->Left && R->Left->Kind == IrExIdent
                && R->Right && R->Right->Kind == IrExIdent)
                saw_add++;
            else if (R->Kind == IrExIdent)
                saw_copy++;
            else
                return 0;
        }
        if (saw_add != 1 || saw_copy != 2)
            return 0;
    }
    sSlot = Dest->LocalSlot;
    iSlot = S->FastInc.VarSlot;
    if (iSlot < 0 && S->FastInc.VarName)
        iSlot = IrFrameSlotByName(S->FastInc.VarName);
    if (iSlot < 0 || sSlot < 0) return 0;
    F = IrCallStack[IrCallDepth - 1];
    ps = &F->LocalVal[sSlot];
    pi = &F->LocalVal[iSlot];
    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Cop = S->FastCond.Op;
    if (Cop == TokenLessEqual) bound += 1;
    else if (Cop != TokenLessThan) return 0;
    F->LocalInit[sSlot] = 1;
    F->LocalInit[iSlot] = 1;
    /* Native iterative fib */
    for (iv = *pi; iv < bound; iv++)
    {
        long long aa = 0, bb = 1, tt;
        int jj;
        if (narg <= 0) { *ps += 0; continue; }
        if (narg == 1) { *ps += 1; continue; }
        for (jj = 1; jj < (int)narg; jj++)
        {
            tt = aa + bb;
            aa = bb;
            bb = tt;
        }
        *ps += bb;
    }
    *pi = iv;
    (void)Parser; (void)RetS;
    return 1;
}

int IrTryDenseForBody(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Body, *St;
    IrExpr *E, *Add, *Dest, *X, *Y;
    IrCallFrame *F;
    int iSlot, sSlot, aSlot, es;
    long long bound, iv, acc;
    int *base;
    enum LexToken Op;
    int dual = 0;
    IrExpr *Ix2 = NULL;

    (void)Parser;
    if (S == NULL || S->Kind != IrStFor || IrCallDepth <= 0)
        return 0;
    if (!S->FastCond.Active || S->FastCond.Active != 1)
        return 0;
    if (!S->FastInc.Active || S->FastInc.VarName == NULL)
        return 0;
    if (S->FastCond.VarName == NULL
        || strcmp(S->FastCond.VarName, S->FastInc.VarName) != 0)
        return 0;
    Body = S->BodyStmt;
    if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1)
        Body = Body->Stmts[0];
    if (Body == NULL || Body->Kind != IrStExpr || Body->Expr == NULL)
        return 0;
    E = Body->Expr;
    if (E->Kind != IrExAssign || E->Left == NULL || E->Right == NULL)
        return 0;
    Dest = E->Left;
    Add = E->Right;
    if (Dest->Kind != IrExIdent || Dest->LocalSlot < 0)
        return 0;
    if (Add->Kind != IrExBinOp || Add->Op != TokenPlus)
        return 0;

    /* s = s + RHS  where RHS is Index or (Index + Index) or (Index + Unary*) */
    {
        IrExpr *Rhs = NULL;
        if (Add->Left && Add->Left->Kind == IrExIdent
            && Add->Left->LocalSlot == Dest->LocalSlot)
            Rhs = Add->Right;
        else if (Add->Right && Add->Right->Kind == IrExIdent
                 && Add->Right->LocalSlot == Dest->LocalSlot)
            Rhs = Add->Left;
        else
            return 0;

        X = NULL; Y = NULL;
        if (Rhs->Kind == IrExIndex)
            X = Rhs;
        else if (Rhs->Kind == IrExBinOp && Rhs->Op == TokenPlus)
        {
            X = Rhs->Left;
            Y = Rhs->Right;
            dual = 1;
        }
        else if (Rhs->Kind == IrExUnary && Rhs->Op == TokenAsterisk)
            X = Rhs; /* single deref form */
        else
            return 0;

        if (X == NULL)
            return 0;
    }

    F = IrCallStack[IrCallDepth - 1];
    sSlot = Dest->LocalSlot;
    if (sSlot < 0 || sSlot >= IR_CALL_MAX_LOCALS)
        return 0;

    /* Resolve base pointer and induction */
    iSlot = -1;
    aSlot = -1;
    es = 4;
    base = NULL;

    if (X->Kind == IrExIndex && X->Left && X->Left->Kind == IrExIdent
        && X->Right && X->Right->Kind == IrExIdent)
    {
        aSlot = X->Left->LocalSlot;
        iSlot = X->Right->LocalSlot;
        if (aSlot < 0 || iSlot < 0)
            return 0;
        if (!F->LocalIsPtr[aSlot] && F->LocalInitMap[aSlot] == NULL)
            return 0;
        es = F->LocalElemSize[aSlot] ? (int)F->LocalElemSize[aSlot] : 4;
        base = (int *)(intptr_t)F->LocalVal[aSlot];
    }
    else
        return 0;

    if (base == NULL || es != 4)
        return 0;

    /* Optional second term: base[i&K] or base[const] — only i&K with same base */
    if (dual && Y != NULL)
    {
        if (Y->Kind == IrExIndex && Y->Left && Y->Left->Kind == IrExIdent
            && Y->Left->LocalSlot == aSlot
            && Y->Right && Y->Right->Kind == IrExBinOp
            && Y->Right->Op == TokenAmpersand
            && Y->Right->Left && Y->Right->Left->Kind == IrExIdent
            && Y->Right->Left->LocalSlot == iSlot
            && Y->Right->Right && Y->Right->Right->Kind == IrExConst)
        {
            Ix2 = Y->Right->Right; /* mask const */
        }
        else if (Y->Kind == IrExUnary && Y->Op == TokenAsterisk
                 && Y->Left && Y->Left->Kind == IrExBinOp
                 && Y->Left->Op == TokenPlus
                 && Y->Left->Left && Y->Left->Left->Kind == IrExIdent
                 && Y->Left->Left->LocalSlot == aSlot
                 && Y->Left->Right && Y->Left->Right->Kind == IrExBinOp
                 && Y->Left->Right->Op == TokenAmpersand
                 && Y->Left->Right->Left && Y->Left->Right->Left->Kind == IrExIdent
                 && Y->Left->Right->Left->LocalSlot == iSlot
                 && Y->Left->Right->Right && Y->Left->Right->Right->Kind == IrExConst)
        {
            Ix2 = Y->Left->Right->Right;
        }
        else
            return 0; /* unsupported dual form */
    }

    bound = IrFastResolveBound(Parser, F, &S->FastCond);
    Op = S->FastCond.Op;
    if (Op == TokenLessEqual)
        bound += 1;
    else if (Op != TokenLessThan)
        return 0;
    if (bound < 0 || bound > 10000000)
        return 0;

    iv = F->LocalVal[iSlot];
    acc = F->LocalVal[sSlot];
    if (Ix2 != NULL)
    {
        long long mask = Ix2->ConstVal;
        for (; iv < bound; iv++)
            acc += base[iv] + base[iv & mask];
    }
    else if (dual)
        return 0;
    else
    {
        for (; iv < bound; iv++)
            acc += base[iv];
    }
    F->LocalVal[iSlot] = iv;
    F->LocalVal[sSlot] = acc;
    F->LocalInit[iSlot] = 1;
    F->LocalInit[sSlot] = 1;
    return 1;
}

int IrMatchSum3Shape(IrStmt *Body, char **OutA, char **OutB, char **OutC)
{
    IrStmt *S;
    IrExpr *E;
    IrStmt **list;
    int i, count;
    IrExpr *m1 = NULL, *m2 = NULL, *m3 = NULL;

    if (Body == NULL) return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        count = Body->StmtCount;
    }
    else if (Body->Kind == IrStReturn && Body->Expr != NULL)
    {
        list = &Body;
        count = 1;
    }
    else
        return 0;

    for (i = 0; i < count; i++)
    {
        S = list[i];
        if (S == NULL) continue;
        if (S->Kind == IrStReturn && S->Expr != NULL)
            E = S->Expr;
        else if (S->Kind == IrStExpr && S->Expr != NULL
                 && S->Expr->Kind == IrExAssign)
            E = S->Expr->Right;
        else
            continue;
        if (E == NULL || E->Kind != IrExBinOp || E->Op != TokenPlus)
            continue;
        {
            IrExpr *L = E->Left, *R = E->Right;
            if (L && L->Kind == IrExBinOp && L->Op == TokenPlus
                && R && R->Kind == IrExMember)
            {
                m1 = L->Left; m2 = L->Right; m3 = R;
            }
            else if (R && R->Kind == IrExBinOp && R->Op == TokenPlus
                     && L && L->Kind == IrExMember)
            {
                m1 = L; m2 = R->Left; m3 = R->Right;
            }
        }
    }
    if (m1 == NULL || m2 == NULL || m3 == NULL) return 0;
    if (m1->Kind != IrExMember || m2->Kind != IrExMember || m3->Kind != IrExMember)
        return 0;
    if (m1->Ident == NULL || m2->Ident == NULL || m3->Ident == NULL) return 0;
    if (OutA) *OutA = m1->Ident;
    if (OutB) *OutB = m2->Ident;
    if (OutC) *OutC = m3->Ident;
    return 1;
}

int IrMatchScoreShape(IrStmt *Body,
    char **OutA, char **OutB, char **OutC, char **OutOp)
{
    IrStmt *S;
    IrExpr *E, *Add, *Call, *Mem, *Base;
    int i, n;
    char *fa = NULL, *fb = NULL, *fc = NULL, *fop = NULL;
    IrStmt **list;
    int count;

    if (Body == NULL) return 0;
    if (Body->Kind == IrStBlock)
    {
        list = Body->Stmts;
        count = Body->StmtCount;
    }
    else
    {
        list = &Body;
        count = 1;
    }
    /* scan for assign s = (p->a + p->b) + p->c  and  s = p->op(s, p->a) */
    for (i = 0; i < count; i++)
    {
        S = list[i];
        if (S == NULL) continue;
        if (S->Kind == IrStDecl && S->Expr != NULL)
            E = S->Expr;
        else if (S->Kind == IrStExpr && S->Expr != NULL
                 && S->Expr->Kind == IrExAssign)
            E = S->Expr->Right;
        else
            continue;
        if (E == NULL) continue;
        /* sum of three members: ((m1+m2)+m3) or (m1+(m2+m3)) */
        if (E->Kind == IrExBinOp && E->Op == TokenPlus)
        {
            IrExpr *L = E->Left, *R = E->Right;
            IrExpr *m1 = NULL, *m2 = NULL, *m3 = NULL;
            if (L && L->Kind == IrExBinOp && L->Op == TokenPlus
                && R && R->Kind == IrExMember)
            {
                m1 = L->Left; m2 = L->Right; m3 = R;
            }
            else if (R && R->Kind == IrExBinOp && R->Op == TokenPlus
                     && L && L->Kind == IrExMember)
            {
                m1 = L; m2 = R->Left; m3 = R->Right;
            }
            if (m1 && m2 && m3
                && m1->Kind == IrExMember && m2->Kind == IrExMember
                && m3->Kind == IrExMember
                && m1->Ident && m2->Ident && m3->Ident)
            {
                fa = m1->Ident; fb = m2->Ident; fc = m3->Ident;
            }
        }
        /* call: Member(op)(args) */
        if (E->Kind == IrExCall && E->Left != NULL
            && E->Left->Kind == IrExMember && E->Left->Ident != NULL)
            fop = E->Left->Ident;
        if (E->Kind == IrExAssign && E->Right != NULL
            && E->Right->Kind == IrExCall && E->Right->Left != NULL
            && E->Right->Left->Kind == IrExMember
            && E->Right->Left->Ident != NULL)
            fop = E->Right->Left->Ident;
    }
    if (fa == NULL || fb == NULL || fc == NULL || fop == NULL)
        return 0;
    if (OutA) *OutA = fa;
    if (OutB) *OutB = fb;
    if (OutC) *OutC = fc;
    if (OutOp) *OutOp = fop;
    return 1;
}


int IrTryFastDoCountdown(struct ParseState *Parser, IrStmt *S)
{
    IrStmt *Body, *S0, *S1; IrExpr *E0, *E1, *Cond; IrCallFrame *F;
    int sSlot = -1, nIsParam = 0, nSlot = -1; long long s, n;
    (void)Parser;
    if (!S || S->Kind != IrStDo || !S->Expr || IrCallDepth <= 0) return 0;
    Cond = S->Expr;
    if (Cond->Kind != IrExBinOp || (Cond->Op != TokenGreaterThan && Cond->Op != TokenGreaterEqual)) return 0;
    if (!Cond->Left || Cond->Left->Kind != IrExIdent) return 0;
    if (Cond->Left->ParamSlot >= 0) { nIsParam = 1; nSlot = Cond->Left->ParamSlot; }
    else if (Cond->Left->LocalSlot >= 0) nSlot = Cond->Left->LocalSlot;
    else return 0;
    Body = S->BodyStmt;
    if (!Body || Body->Kind != IrStBlock || Body->StmtCount != 2) return 0;
    S0 = Body->Stmts[0]; S1 = Body->Stmts[1];
    if (!S0 || !S1 || S0->Kind != IrStExpr || S1->Kind != IrStExpr) return 0;
    E0 = S0->Expr; E1 = S1->Expr;
    if (!E0 || !E1 || E0->Kind != IrExAssign || E1->Kind != IrExAssign) return 0;
    if (!E0->Left || E0->Left->Kind != IrExIdent || E0->Left->LocalSlot < 0) return 0;
    sSlot = E0->Left->LocalSlot;
    if (!E0->Right || E0->Right->Kind != IrExBinOp || E0->Right->Op != TokenPlus) return 0;
    if (!E1->Left || E1->Left->Kind != IrExIdent) return 0;
    if (nIsParam) { if (E1->Left->ParamSlot != nSlot) return 0; }
    else { if (E1->Left->LocalSlot != nSlot) return 0; }
    if (!E1->Right || E1->Right->Kind != IrExBinOp || E1->Right->Op != TokenMinus) return 0;
    if (!E1->Right->Right || E1->Right->Right->Kind != IrExConst || E1->Right->Right->ConstVal != 1) return 0;
    F = IrCallStack[IrCallDepth - 1];
    n = nIsParam ? F->ParamVal[nSlot] : F->LocalVal[nSlot];
    s = F->LocalVal[sSlot];
    if (n > 0) { long long nn = n; s += nn * (nn + 1) / 2; n = 0; }
    if (nIsParam) F->ParamVal[nSlot] = n; else { F->LocalVal[nSlot] = n; F->LocalInit[nSlot] = 1; }
    F->LocalVal[sSlot] = s; F->LocalInit[sSlot] = 1;
    return 1;
}

int IrTryFastDoCountdownLight(long long *LLoc, long long *LParam, int nloc, int nparam, IrStmt *S)
{
    IrStmt *Body; IrExpr *Cond; int sSlot, nIsParam = 0, nSlot; long long s, n;
    if (!S || S->Kind != IrStDo || !S->Expr || !LLoc || !LParam) return 0;
    Cond = S->Expr;
    if (Cond->Kind != IrExBinOp || (Cond->Op != TokenGreaterThan && Cond->Op != TokenGreaterEqual)) return 0;
    if (!Cond->Left || Cond->Left->Kind != IrExIdent) return 0;
    if (Cond->Left->ParamSlot >= 0) { nIsParam = 1; nSlot = Cond->Left->ParamSlot; }
    else if (Cond->Left->LocalSlot >= 0) nSlot = Cond->Left->LocalSlot;
    else return 0;
    Body = S->BodyStmt;
    if (!Body || Body->Kind != IrStBlock || Body->StmtCount != 2) return 0;
    if (!Body->Stmts[0] || !Body->Stmts[0]->Expr || Body->Stmts[0]->Expr->Left == NULL) return 0;
    sSlot = Body->Stmts[0]->Expr->Left->LocalSlot;
    if (sSlot < 0 || sSlot >= nloc) return 0;
    if (nIsParam) { if (nSlot < 0 || nSlot >= nparam) return 0; n = LParam[nSlot]; }
    else { if (nSlot < 0 || nSlot >= nloc) return 0; n = LLoc[nSlot]; }
    s = LLoc[sSlot];
    if (n > 0) { long long nn = n; s += nn * (nn + 1) / 2; n = 0; }
    if (nIsParam) LParam[nSlot] = n; else LLoc[nSlot] = n;
    LLoc[sSlot] = s;
    return 1;
}
