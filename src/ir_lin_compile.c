/* ir_lin_compile.c — extract compile-side lower from ir_lower.c */
#include "picoc.h"
#include "ir_lin_compile.h"
#include "ir_lin_emit.h"
#include "ir_lin_tabs.h"
#include "ir_lin_ptr.h"
#include "ir_lin_light.h"
#include "ir_runtime.h"
#include "ir_lin.h"
int LinCseN = 0;
/* Preserve cache/registers across a loop condition and its body. */
static int LinCseLoopRegion = 0;
int LinCseSlot[IR_CALL_MAX_LOCALS];
int LinCseReg[IR_CALL_MAX_LOCALS];
unsigned char LinCseSlotPinned[IR_CALL_MAX_LOCALS];
int LinAddrTaken[IR_CALL_MAX_LOCALS];
/* Expression CSE is deliberately separate from local-slot CSE: the legacy
 * slot table is keyed by small integers, while indexed loads need a full
 * structural key to avoid collisions between unrelated base/index pairs. */
static int LinCseExprN = 0;
static unsigned short LinCseRegPins[IR_LIN_REGS];
void LinCsePinReg(int reg)
{
    if (reg >= 0 && reg < IR_LIN_REGS && LinCseRegPins[reg] < 65535)
        LinCseRegPins[reg]++;
}
void LinCseUnpinReg(int reg)
{
    if (reg >= 0 && reg < IR_LIN_REGS && LinCseRegPins[reg] > 0)
        LinCseRegPins[reg]--;
}
int LinCseRegIsPinned(int reg)
{
    return reg >= 0 && reg < IR_LIN_REGS && LinCseRegPins[reg] != 0;
}
static unsigned long long LinCseExprKey[IR_CALL_MAX_LOCALS];
static int LinCseExprReg[IR_CALL_MAX_LOCALS];
unsigned char LinCseExprPinned[IR_CALL_MAX_LOCALS];
int LinCseExprFind(unsigned long long key)
{
    int i;
    if (key == 0) return -1;
    for (i = 0; i < LinCseExprN; i++)
        if (LinCseExprKey[i] == key && LinCseExprReg[i] >= 0)
            return LinCseExprReg[i];
    return -1;
}
void LinCseExprStore(unsigned long long key, int reg)
{
    int i;
    if (key == 0 || reg < 0) return;
    for (i = 0; i < LinCseExprN; i++) {
        if (LinCseExprKey[i] == key) {
            if (LinCseExprPinned[i]) LinCseUnpinReg(LinCseExprReg[i]);
            LinCseExprReg[i] = reg;
            LinCsePinReg(reg);
            LinCseExprPinned[i] = 1;
            return;
        }
    }
    if (LinCseExprN < IR_CALL_MAX_LOCALS) {
        LinCseExprKey[LinCseExprN] = key;
        LinCseExprReg[LinCseExprN] = reg;
        LinCsePinReg(reg);
        LinCseExprPinned[LinCseExprN] = 1;
        LinCseExprN++;
    }
}
static void LinCseDropExprCache(void)
{
    int i;
    for (i = 0; i < LinCseExprN; ++i) {
        if (LinCseExprPinned[i]) LinCseUnpinReg(LinCseExprReg[i]);
        LinCseExprPinned[i] = 0;
    }
    LinCseExprN = 0;
}
void LinCseReset(void)
{
    int i;
    for (i = 0; i < LinCseN; ++i) {
        if (LinCseSlotPinned[i]) LinCseUnpinReg(LinCseReg[i]);
        LinCseSlotPinned[i] = 0;
    }
    LinCseN = 0;
    LinCseDropExprCache();
}
void LinCseInvalAll(void) { LinCseReset(); }
static void LinCseInvalidateLoopImpl(const unsigned char *written, int memory_write,
                                     int has_call, int preserve_expr_for_local_writes)
{
    int i, w = 0, any = 0;
    for (i = 0; i < IR_CALL_MAX_LOCALS; ++i) if (written[i]) { any = 1; break; }
    for (i = 0; i < LinCseN; ++i) {
        int slot = LinCseSlot[i];
        if (slot >= 0 && slot < IR_CALL_MAX_LOCALS && !written[slot]) {
            LinCseSlot[w] = LinCseSlot[i]; LinCseReg[w] = LinCseReg[i];
            LinCseSlotPinned[w] = LinCseSlotPinned[i]; ++w;
        } else {
            if (LinCseSlotPinned[i]) LinCseUnpinReg(LinCseReg[i]);
            LinCseSlotPinned[i] = 0;
        }
    }
    LinCseN = w;
    /* Expr cache currently lacks dependency vectors. Drop it if any possible
       dependency changed; unrelated scalar-local cache entries survive. */
    if (memory_write || has_call || (any && !preserve_expr_for_local_writes))
        LinCseDropExprCache();
}
void LinCseInvalidateLoop(const unsigned char *written, int memory_write, int has_call)
{
    LinCseInvalidateLoopImpl(written, memory_write, has_call, 0);
}
static void LinCseInvalidateLoopPreserveExpr(const unsigned char *written,
                                             int memory_write, int has_call)
{
    LinCseInvalidateLoopImpl(written, memory_write, has_call, 1);
}
static void LinCsePinLiveEntries(void)
{
    int i;
    for (i = 0; i < LinCseN; ++i) if (!LinCseSlotPinned[i]) {
        LinCsePinReg(LinCseReg[i]); LinCseSlotPinned[i] = 1;
    }
    for (i = 0; i < LinCseExprN; ++i) if (!LinCseExprPinned[i]) {
        LinCsePinReg(LinCseExprReg[i]); LinCseExprPinned[i] = 1;
    }
}
void LinMarkAddrTaken(int slot) {
    if (slot >= 0 && slot < IR_CALL_MAX_LOCALS) LinAddrTaken[slot] = 1;
}
#include "ir_storage.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern struct ParseState *LinParser;
extern int LinCompileActive;
extern int LowerDepth;
extern int LinExecDepth;
extern int LinSkippedCall;
extern long LinSkipCallCount;
extern int LinRetSeen;
extern int LinStepAbort;
extern int LinDiv0;
extern long long LinRetVal;
extern unsigned char CallSkip[128];
/* CompLocal* declared in ir_storage.h */
extern long long LinWideImm[IR_LIN_STMT_MAX];
extern unsigned char LinWideOn[IR_LIN_STMT_MAX];
extern int LinWideMax;
extern int GlobSaved;
extern unsigned GlobDirty;

int AllocReg(int *Next);
int Emit(IrLinInst *Code, int Cap, int *N, IrLinInst In);
int EmitLine(IrLinInst *Code, int Cap, int *N, int Line);
int LowerRec(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next, int *Dest);
void CompileStampName(int slot, const char *name);
void CompileStampPtrSlot(int slot, struct ValueType *FT, int is_fptr);
void IdxMetaPush(IrExpr *E);
void IrLiveMarkInit(IrCallFrame *Fr, int slot);
int IrKernelStmtScalarOnly(IrStmt *S, int depth);
int IrKernelFpEnabledFor(IrStmt *S);
int IrShapeFpEnabled(void);
int IrExecIsVmProd(void);
int IrVmStrictOn(void);
int IrVmLinearFeaturesOn(void);
int IrFnBodyIntegerEligible(IrStmt *Body);
int IrLinPeephole(IrLinInst *Code, int N);
void IrVmProdNotice(void);
int IrVmProfileOn(void);
struct Value *IrGlobalIdentSoft(struct ParseState *Parser, const char *name);

typedef struct LinLoop {
    int CanCont;
    int Brk[12];
    int NBrk;
    int ContPc;
    int Conts[12];
    int NCont;
} LinLoop;
static LinLoop Loops[8];
int LoopSp;

typedef struct LinLab {
    const char *Name;
    int Pc;
    int Patch[8];
    int NPatch;
} LinLab;
LinLab Labs[16];
int NLab;

/* Compiler frame implementation (nested FnCodeCompile / IrLowerStmt). */
typedef struct CompileFrame {
    FnCodeSlot Tabs;
    long long WideImm[IR_LIN_STMT_MAX];
    unsigned char WideOn[IR_LIN_STMT_MAX];
    int WideMaxSav;
    LinLoop LoopsSav[8];
    int LoopSpSav;
    LinLab LabsSav[16];
    int NLabSav;
    unsigned char CallSkipSav[128];
    int LinSkippedCallSav;
    int LinRetSeenSav;
    long long LinRetValSav;
    struct ParseState *LinParserSav;
    /* Nested FnCodeCompile zeros CompLocal*; save/restore parent dims/types. */
    unsigned char CompNDims[IR_CALL_MAX_LOCALS];
    int CompDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
    int CompLeafEs[IR_CALL_MAX_LOCALS];
    unsigned char CompElemIsPtr[IR_CALL_MAX_LOCALS];
    int CompPointeeSize[IR_CALL_MAX_LOCALS];
    struct ValueType *CompFromType[IR_CALL_MAX_LOCALS];
    int Valid;
} CompileFrame;

#define COMPILE_FRAME_MAX 8
static CompileFrame CompileFrames[COMPILE_FRAME_MAX];
static int CompileFrameSp;

int CompileFramePush(void)
{
    CompileFrame *F;
    int wm;
    if (CompileFrameSp >= COMPILE_FRAME_MAX)
        return 0;
    F = &CompileFrames[CompileFrameSp++];
    FnCodeSnapshot(&F->Tabs);
    wm = LinWideMax;
    if (wm > IR_LIN_STMT_MAX) wm = IR_LIN_STMT_MAX;
    F->WideMaxSav = wm;
    if (wm > 0)
    {
        memcpy(F->WideImm, LinWideImm, (size_t)wm * sizeof(long long));
        memcpy(F->WideOn, LinWideOn, (size_t)wm);
    }
    memcpy(F->LoopsSav, Loops, sizeof(Loops));
    F->LoopSpSav = LoopSp;
    memcpy(F->LabsSav, Labs, sizeof(Labs));
    F->NLabSav = NLab;
    memcpy(F->CallSkipSav, CallSkip, sizeof(CallSkip));
    F->LinSkippedCallSav = LinSkippedCall;
    F->LinRetSeenSav = LinRetSeen;
    F->LinRetValSav = LinRetVal;
    F->LinParserSav = LinParser;
    memcpy(F->CompNDims, CompLocalNDims, sizeof(CompLocalNDims));
    memcpy(F->CompDims, CompLocalDims, sizeof(CompLocalDims));
    memcpy(F->CompLeafEs, CompLocalLeafEs, sizeof(CompLocalLeafEs));
    memcpy(F->CompElemIsPtr, CompLocalElemIsPtr, sizeof(CompLocalElemIsPtr));
    memcpy(F->CompPointeeSize, CompLocalPointeeSize, sizeof(CompLocalPointeeSize));
    memcpy(F->CompFromType, CompLocalFromType, sizeof(CompLocalFromType));
    F->Valid = 1;
    return 1;
}

void CompileFramePop(void)
{
    CompileFrame *F;
    int wm;
    if (CompileFrameSp <= 0)
        return;
    F = &CompileFrames[--CompileFrameSp];
    if (!F->Valid)
        return;
    LinTabsRestore(&F->Tabs);
    wm = F->WideMaxSav;
    if (wm > IR_LIN_STMT_MAX) wm = IR_LIN_STMT_MAX;
    if (wm > 0)
    {
        memcpy(LinWideImm, F->WideImm, (size_t)wm * sizeof(long long));
        memcpy(LinWideOn, F->WideOn, (size_t)wm);
    }
    LinWideMax = wm;
    memcpy(Loops, F->LoopsSav, sizeof(Loops));
    LoopSp = F->LoopSpSav;
    memcpy(Labs, F->LabsSav, sizeof(Labs));
    NLab = F->NLabSav;
    memcpy(CallSkip, F->CallSkipSav, sizeof(CallSkip));
    LinSkippedCall = F->LinSkippedCallSav;
    LinRetSeen = F->LinRetSeenSav;
    LinRetVal = F->LinRetValSav;
    LinParser = F->LinParserSav;
    memcpy(CompLocalNDims, F->CompNDims, sizeof(CompLocalNDims));
    memcpy(CompLocalDims, F->CompDims, sizeof(CompLocalDims));
    memcpy(CompLocalLeafEs, F->CompLeafEs, sizeof(CompLocalLeafEs));
    memcpy(CompLocalElemIsPtr, F->CompElemIsPtr, sizeof(CompLocalElemIsPtr));
    memcpy(CompLocalPointeeSize, F->CompPointeeSize, sizeof(CompLocalPointeeSize));
    memcpy(CompLocalFromType, F->CompFromType, sizeof(CompLocalFromType));
    F->Valid = 0;
}


/* KERNEL_FOR gate: scalar locals/params only — no Index/global/Call.
 * IrKernelStmtOk allows Index (isort/adj); those stay on ARRAY_ACCUM/INSERT. */
static int IrKernelExprScalarOnly(IrExpr *E, int depth)
{
    if (E == NULL || depth > 24) return 0;
    switch (E->Kind)
    {
        case IrExConst: return 1;
        case IrExIdent:
            return (E->LocalSlot >= 0 || E->ParamSlot >= 0);
        case IrExBinOp:
            /* Shifts need unsigned/wide flags; kernel tree can miss param ushort. */
            if (E->Op == TokenShiftLeft || E->Op == TokenShiftRight)
                return 0;
            return IrKernelExprScalarOnly(E->Left, depth + 1)
                && IrKernelExprScalarOnly(E->Right, depth + 1);
        case IrExAssign:
            if (E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
                || E->Op == TokenArithmeticExorAssign
                || E->Op == TokenArithmeticAndAssign
                || E->Op == TokenArithmeticOrAssign)
                return 0;
            return IrKernelExprScalarOnly(E->Left, depth + 1)
                && IrKernelExprScalarOnly(E->Right, depth + 1);
        case IrExUnary:
            if (E->Op == TokenAmpersand || E->Op == TokenAsterisk)
                return 0;
            if (E->Op == TokenIncrement || E->Op == TokenDecrement)
                return 0;
            return IrKernelExprScalarOnly(E->Left, depth + 1);
        case IrExTernary:
            if (E->Args == NULL || E->ArgCount < 1) return 0;
            return IrKernelExprScalarOnly(E->Left, depth + 1)
                && IrKernelExprScalarOnly(E->Right, depth + 1)
                && IrKernelExprScalarOnly(E->Args[0], depth + 1);
        default:
            return 0; /* Index/Call/Member/Cast refused */
    }
}

int IrKernelStmtScalarOnly(IrStmt *S, int depth)
{
    int i;
    if (S == NULL || depth > 16) return 0;
    switch (S->Kind)
    {
        case IrStExpr:
            return S->Expr != NULL && IrKernelExprScalarOnly(S->Expr, 0);
        case IrStBlock:
            for (i = 0; i < S->StmtCount; i++)
                if (!IrKernelStmtScalarOnly(S->Stmts[i], depth + 1))
                    return 0;
            return 1;
        case IrStIf:
            if (S->Expr == NULL || !IrKernelExprScalarOnly(S->Expr, 0))
                return 0;
            if (S->ThenStmt && !IrKernelStmtScalarOnly(S->ThenStmt, depth + 1))
                return 0;
            if (S->ElseStmt && !IrKernelStmtScalarOnly(S->ElseStmt, depth + 1))
                return 0;
            return 1;
        case IrStBreak:
        case IrStContinue:
            return 1;
        default:
            return 0;
    }
}

/* Const-trip unrolling lowers a body repeatedly from the same AST. Keep that
 * optimization to side effects on scalar locals/parameters: repeated lowering
 * of Index/Member/Call/pointer expressions can mutate cached address metadata
 * and make otherwise-valid multidimensional accesses fail. This is separate
 * from IrKernelExprScalarOnly: scalar ++/-- are safe here and preserve source
 * diagnostics in loops such as a const-local increment. */
static int IrConstUnrollExprSafe(IrExpr *E, int depth)
{
    if (E == NULL || depth > 24) return 0;
    switch (E->Kind)
    {
        case IrExConst:
            return E->Op != TokenStringConstant;
        case IrExIdent:
            return E->LocalSlot >= 0 || E->ParamSlot >= 0;
        case IrExBinOp:
            if (E->Op == TokenComma || E->Op == TokenLogicalAnd
                || E->Op == TokenLogicalOr)
                return 0;
            return IrConstUnrollExprSafe(E->Left, depth + 1)
                && IrConstUnrollExprSafe(E->Right, depth + 1);
        case IrExAssign:
            if (E->Left == NULL || E->Left->Kind != IrExIdent
                || (E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0))
                return 0;
            if (E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
                || E->Op == TokenArithmeticExorAssign
                || E->Op == TokenArithmeticAndAssign
                || E->Op == TokenArithmeticOrAssign)
                return 0;
            return IrConstUnrollExprSafe(E->Left, depth + 1)
                && IrConstUnrollExprSafe(E->Right, depth + 1);
        case IrExUnary:
            if (E->Op == TokenIncrement || E->Op == TokenDecrement)
                return E->Left != NULL && E->Left->Kind == IrExIdent
                    && (E->Left->LocalSlot >= 0 || E->Left->ParamSlot >= 0);
            if (E->Op == TokenAmpersand || E->Op == TokenAsterisk
                || E->Op == TokenCast)
                return 0;
            return IrConstUnrollExprSafe(E->Left, depth + 1);
        case IrExTernary:
            if (E->Args == NULL || E->ArgCount < 1) return 0;
            return IrConstUnrollExprSafe(E->Left, depth + 1)
                && IrConstUnrollExprSafe(E->Right, depth + 1)
                && IrConstUnrollExprSafe(E->Args[0], depth + 1);
        default:
            /* Index, Member, Call and any future expression kinds are unsafe
             * to re-lower from the same AST until their cache lifecycle is explicit. */
            return 0;
    }
}

static int IrConstUnrollStmtSafe(IrStmt *S, int depth)
{
    int i;
    if (S == NULL || depth > 16) return 0;
    switch (S->Kind)
    {
        case IrStExpr:
            return S->Expr != NULL && IrConstUnrollExprSafe(S->Expr, 0);
        case IrStBlock:
            for (i = 0; i < S->StmtCount; i++)
                if (!IrConstUnrollStmtSafe(S->Stmts[i], depth + 1))
                    return 0;
            return 1;
        case IrStIf:
            if (S->Expr == NULL || !IrConstUnrollExprSafe(S->Expr, 0))
                return 0;
            if (S->ThenStmt && !IrConstUnrollStmtSafe(S->ThenStmt, depth + 1))
                return 0;
            if (S->ElseStmt && !IrConstUnrollStmtSafe(S->ElseStmt, depth + 1))
                return 0;
            return 1;
        case IrStBreak:
        case IrStContinue:
            return 1;
        default:
            /* Calls, declarations, nested loops, switch, labels and gotos stay
             * on the normal loop lowering path. */
            return 0;
    }
}

static LinLab *LabGet(const char *Name)
{
    int i;
    if (Name == NULL) return NULL;
    for (i = 0; i < NLab; i++)
        if (Labs[i].Name == Name || (Labs[i].Name && strcmp(Labs[i].Name, Name) == 0))
            return &Labs[i];
    if (NLab >= 16) return NULL;
    Labs[NLab].Name = Name;
    Labs[NLab].Pc = -1;
    Labs[NLab].NPatch = 0;
    return &Labs[NLab++];
}

static void LinPromSyncAll(IrLinInst *Code, int Cap, int *N);
static void LinPromFinish(const unsigned char *selected, const unsigned char *owned);
static int EmitBreak(IrLinInst *Code, int Cap, int *N)
{
    IrLinInst In;
    if (LoopSp <= 0) return 0;
    if (Loops[LoopSp - 1].NBrk >= 12) return 0;
    Loops[LoopSp - 1].Brk[Loops[LoopSp - 1].NBrk++] = *N;
    In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
    return Emit(Code, Cap, N, In);
}
static int EmitCont(IrLinInst *Code, int Cap, int *N)
{
    IrLinInst In;
    int sp;
    for (sp = LoopSp - 1; sp >= 0; sp--)
        if (Loops[sp].CanCont) break;
    if (sp < 0) return 0;
    In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
    if (Loops[sp].ContPc >= 0)
        In.Imm = Loops[sp].ContPc;
    else
    {
        if (Loops[sp].NCont >= 12) return 0;
        Loops[sp].Conts[Loops[sp].NCont++] = *N;
    }
    return Emit(Code, Cap, N, In);
}
static void Patch(IrLinInst *Code, int *List, int N, int Pc)
{
    int i;
    for (i = 0; i < N; i++)
        Code[List[i]].Imm = Pc;
}
static void PushLoop(int CanCont)
{
    LinLoop *L = &Loops[LoopSp++];
    L->CanCont = CanCont;
    L->NBrk = 0;
    L->NCont = 0;
    L->ContPc = -1;
}

/* Before a generic loop: try the shared native loop chain (the same one the
 * tree runs, IrTryFastWhileChain / IrTryFastForAfterInit) and skip the
 * lowered loop when it ran the whole loop. Without this, programs that used
 * to fall back to tree (and so got its kernels) became much slower once
 * linear could lower them. Returns the BrZ pc to patch with the loop end,
 * -1 when not emitted, -2 on emission failure. */
static int LinPromAnyActive(void);
static int LinPromStmtTouchesActive(IrStmt *S);
static int EmitTreeLoopTry(IrStmt *S, int tok, IrLinInst *Code, int Cap,
                           int *N, int *Next)
{
    IrLinInst K;
    int r, br;
    /* A native tree/kernel loop mutates real local slots directly. Never put
     * one inside an enclosing region with pinned local registers: that path
     * would bypass the register preheader/sync and leave the outer registers
     * stale. The outer loop can still be promoted; only this nested kernel
     * attempt is declined and its generic lowered body remains available. */
    if (!IrKernelFpEnabledFor(S) || !IrShapeFpEnabled() || KernelN >= 16
        || IrCallDepth <= 0 || LinPromStmtTouchesActive(S))
        return -1;
    r = AllocReg(Next);
    if (r < 0) return -1;
    KernelStmtTab[KernelN] = S;
    K.Op = IrLinKernel; K.Dest = (unsigned char)r; K.A = K.B = 0;
    K.Tok = (unsigned char)tok; K.Imm = KernelN;
    KernelN++;
    if (!Emit(Code, Cap, N, K)) return -2;
    br = *N;
    K.Op = IrLinBrZ; K.Dest = 0; K.A = (unsigned char)r; K.B = 0;
    K.Tok = 0; K.Imm = 0;
    if (!Emit(Code, Cap, N, K)) return -2;
    return br;
}
static void SealCont(IrLinInst *Code, int Pc)
{
    LinLoop *L = &Loops[LoopSp - 1];
    L->ContPc = Pc;
    Patch(Code, L->Conts, L->NCont, Pc);
}
static void SealBrk(IrLinInst *Code, int Pc)
{
    Patch(Code, Loops[LoopSp - 1].Brk, Loops[LoopSp - 1].NBrk, Pc);
    LoopSp--;
}

/* Conservative source-IR write-set scan for loop condition/body. */
typedef struct LinLoopWrites { unsigned char slot[IR_CALL_MAX_LOCALS]; unsigned char unsupported[IR_CALL_MAX_LOCALS]; int memory; int call; } LinLoopWrites;
static void LinScanExprWrites(IrExpr *E, LinLoopWrites *W);
static void LinScanStmtWrites(IrStmt *S, LinLoopWrites *W);
static void LinScanStmtWritesForPromotion(IrStmt *S, LinLoopWrites *W);
static void LinPromSyncAll(IrLinInst *Code, int Cap, int *N);
static int LinPromReg[IR_CALL_MAX_LOCALS];
static int LinPromRefs[IR_CALL_MAX_LOCALS];
static int LinPromType[IR_CALL_MAX_LOCALS];
static int LinPromAnyActive(void)
{
    int s;
    for (s = 0; s < IR_CALL_MAX_LOCALS; ++s)
        if (LinPromReg[s] >= 0) return 1;
    return 0;
}
static int LinPromExprTouchesActive(IrExpr *E, int depth)
{
    int i;
    if (E == NULL || depth > 64) return 0;
    if (E->Kind == IrExIdent && E->LocalSlot >= 0
        && E->LocalSlot < IR_CALL_MAX_LOCALS
        && LinPromReg[E->LocalSlot] >= 0)
        return 1;
    if (LinPromExprTouchesActive(E->Left, depth + 1)
        || LinPromExprTouchesActive(E->Right, depth + 1)) return 1;
    for (i = 0; E->Args != NULL && i < E->ArgCount; ++i)
        if (LinPromExprTouchesActive(E->Args[i], depth + 1)) return 1;
    return 0;
}
static int LinPromStmtTouchesActive(IrStmt *S)
{
    int i;
    if (S == NULL || !LinPromAnyActive()) return 0;
    if (LinPromExprTouchesActive(S->Expr, 0)
        || LinPromStmtTouchesActive(S->ThenStmt)
        || LinPromStmtTouchesActive(S->ElseStmt)
        || LinPromStmtTouchesActive(S->BodyStmt)
        || LinPromStmtTouchesActive(S->InitStmt)
        || LinPromStmtTouchesActive(S->IncStmt)) return 1;
    for (i = 0; S->Stmts != NULL && i < S->StmtCount; ++i)
        if (LinPromStmtTouchesActive(S->Stmts[i])) return 1;
    return 0;
}
static struct ValueType *LinPromSlotType(IrCallFrame *Fr, int slot)
{
    if (Fr == NULL || slot < 0 || slot >= Fr->NumLocals
        || slot >= IR_CALL_MAX_LOCALS) return NULL;
    /* The lowering-time declaration map is scoped to the function being
     * compiled. Frame LocalTyp/LocalFromType can be stale when caller/callee
     * frames reuse a slot, and frame LocalFromType can be only the pointee for
     * pointer locals. Scalar narrowing uses this map's declared width; pointer
     * slots are admitted only when it independently confirms TypePointer. */
    if (CompLocalFromType[slot] != NULL) return CompLocalFromType[slot];
    if (Fr->LocalTyp[slot] != NULL) return Fr->LocalTyp[slot];
    return Fr->LocalFromType[slot];
}
static int LinPromAddrTakenExpr(IrExpr *E)
{
    int i;
    if (!E) return 0;
    if (E->Kind == IrExUnary && E->Op == TokenAmpersand && E->Left
        && E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0
        && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
        LinAddrTaken[E->Left->LocalSlot] = 1;
    LinPromAddrTakenExpr(E->Left); LinPromAddrTakenExpr(E->Right);
    for (i = 0; E->Args && i < E->ArgCount; ++i) LinPromAddrTakenExpr(E->Args[i]);
    return 1;
}
static void LinPromAddrTakenStmt(IrStmt *S)
{
    int i; if (!S) return;
    LinPromAddrTakenExpr(S->Expr);
    LinPromAddrTakenStmt(S->ThenStmt); LinPromAddrTakenStmt(S->ElseStmt);
    LinPromAddrTakenStmt(S->BodyStmt); LinPromAddrTakenStmt(S->InitStmt); LinPromAddrTakenStmt(S->IncStmt);
    for (i = 0; S->Stmts && i < S->StmtCount; ++i) LinPromAddrTakenStmt(S->Stmts[i]);
}
static int LinPromHasLabel(IrStmt *S)
{
    int i; if (!S) return 0;
    if (S->Kind == IrStLabel) return 1;
    if (LinPromHasLabel(S->ThenStmt) || LinPromHasLabel(S->ElseStmt)
        || LinPromHasLabel(S->BodyStmt) || LinPromHasLabel(S->InitStmt)
        || LinPromHasLabel(S->IncStmt)) return 1;
    for (i = 0; S->Stmts && i < S->StmtCount; ++i)
        if (LinPromHasLabel(S->Stmts[i])) return 1;
    return 0;
}
static int LinPromTypeOk(int slot, struct ValueType **type_out)
{
    IrCallFrame *Fr = IrCallDepth > 0 ? IrCallStack[IrCallDepth - 1] : NULL;
    struct ValueType *T;
    int b;
    if (!Fr || slot < 0 || slot >= Fr->NumLocals || slot >= IR_CALL_MAX_LOCALS) return 0;
    T = LinPromSlotType(Fr, slot);
    if (!T || Fr->LocalArraySize[slot] != 0 || Fr->LocalOwned[slot]
        || Fr->LocalIsFptr[slot]) return 0;
    /* A non-array pointer variable is a C scalar. The old blanket exclusion
     * made pointer-walk loops spill/reload the pointer every iteration even
     * when its address is not taken and the loop has no calls. Only promote
     * when compile-time declaration metadata confirms the slot is a pointer;
     * LocalFromType alone can be the pointee type and is not sufficient. */
    if (Fr->LocalIsPtr[slot] && T->Base != TypePointer) return 0;
    b = T->Base;
    if (!(b == TypeChar || b == TypeUnsignedChar || b == TypeShort || b == TypeUnsignedShort
       || b == TypeInt || b == TypeUnsignedInt || b == TypeLong || b == TypeUnsignedLong
       || b == TypeLongLong || b == TypeUnsignedLongLong || b == TypePointer || b == TypeEnum)) return 0;
    if (type_out) *type_out = T;
    return 1;
}
static void LinPromEmitSync(IrLinInst *Code, int Cap, int *N, int slot)
{
    IrLinInst In;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS || LinPromReg[slot] < 0) return;
    memset(&In, 0, sizeof(In)); In.Op = IrLinStoreLocal; In.Dest = 255;
    In.A = (unsigned char)LinPromReg[slot]; In.Tok = 0; In.Imm = slot;
    (void)Emit(Code, Cap, N, In);
}
static void LinPromSyncAll(IrLinInst *Code, int Cap, int *N)
{
    int s; for (s = 0; s < IR_CALL_MAX_LOCALS; ++s) if (LinPromReg[s] >= 0) LinPromEmitSync(Code, Cap, N, s);
}
/* Select and pin scalar slots before the loop header. Calls or address-taking
 * disable promotion for the entire loop. Existing outer promotion is reused. */
static int LinPromPrepare(IrStmt *S, IrExpr *Cond, IrStmt *Body, IrStmt *Inc,
                          IrLinInst *Code, int Cap, int *N, int *Next,
                          unsigned char *selected, unsigned char *owned)
{
    LinLoopWrites W; int s; IrCallFrame *Fr = IrCallDepth > 0 ? IrCallStack[IrCallDepth - 1] : NULL;
    memset(selected, 0, IR_CALL_MAX_LOCALS); memset(owned, 0, IR_CALL_MAX_LOCALS);
    if (getenv("BABYC_NO_LOOP_LOCAL_PROMOTION") != NULL || !Fr) return 1;
    memset(&W, 0, sizeof(W)); LinScanExprWrites(Cond, &W);
    LinScanStmtWritesForPromotion(Body, &W); LinScanStmtWrites(Inc, &W);
    if (W.call || LinPromHasLabel(Body)) return 1;
    LinPromAddrTakenExpr(Cond); LinPromAddrTakenStmt(Body); LinPromAddrTakenStmt(Inc);
    for (s = 0; s < IR_CALL_MAX_LOCALS; ++s) if (W.slot[s] && !W.unsupported[s] && !LinAddrTaken[s]
        && LinPromTypeOk(s, NULL))
    {
        int r;
        selected[s] = 1;
        if (LinPromReg[s] >= 0) { LinPromRefs[s]++; continue; }
        r = AllocReg(Next); if (r < 0) { LinPromFinish(selected, owned); return 0; }
        {
            IrLinInst In; memset(&In, 0, sizeof(In)); In.Op = IrLinLoadLocal;
            In.Dest = (unsigned char)r; In.Imm = s;
            if (!Emit(Code, Cap, N, In)) { LinPromFinish(selected, owned); return 0; }
        }
        LinPromReg[s] = r; LinPromRefs[s] = 1;
        LinPromType[s] = (int)LinPromSlotType(Fr, s)->Base;
        LinCsePinReg(r); owned[s] = 1;
    }
    (void)S;
    return 1;
}
static void LinPromRewriteRegion(IrLinInst *Code, int begin, int end,
                                 const unsigned char *selected)
{
    int pc;
    for (pc = begin; pc < end; ++pc)
    {
        IrLinInst *In = &Code[pc]; int s = -1;
        if (In->Op == IrLinLoadLocal && In->Imm >= 0 && In->Imm < IR_CALL_MAX_LOCALS) s = (int)In->Imm;
        else if (In->Op == IrLinStoreLocal && In->Dest != 255 && In->Imm >= 0 && In->Imm < IR_CALL_MAX_LOCALS) s = (int)In->Imm;
        else if (In->Op == IrLinAddLocal && In->A < IR_CALL_MAX_LOCALS) s = (int)In->A;
        if (s < 0 || !selected[s] || LinPromReg[s] < 0) continue;
        if (In->Op == IrLinLoadLocal)
        { In->Op = IrLinAddI; In->A = (unsigned char)LinPromReg[s]; In->Imm = 0; In->B = 0; In->Tok = 0; }
        else if (In->Op == IrLinStoreLocal)
        { In->Op = IrLinAddI; In->Dest = (unsigned char)LinPromReg[s]; In->A = In->A; In->Imm = 0; In->B = 0; In->Tok = 0; }
        else if (In->Op == IrLinAddLocal)
        { In->Op = IrLinAddRegLocal; In->A = (unsigned char)LinPromReg[s]; In->B = (unsigned char)s; In->Tok = (enum LexToken)LinPromType[s]; }
    }
}
static void LinPromEmitSelectedSync(IrLinInst *Code, int Cap, int *N, const unsigned char *selected)
{
    int s; for (s = 0; s < IR_CALL_MAX_LOCALS; ++s)
        if (selected[s]) LinPromEmitSync(Code, Cap, N, s);
}
static void LinPromFinish(const unsigned char *selected, const unsigned char *owned)
{
    int s;
    for (s = 0; s < IR_CALL_MAX_LOCALS; ++s) if (selected[s]) {
        if (LinPromRefs[s] > 0) LinPromRefs[s]--;
        if (owned[s] && LinPromRefs[s] == 0) {
            LinCseUnpinReg(LinPromReg[s]); LinPromReg[s] = -1; LinPromType[s] = 0;
        }
    }
}
static void LinScanExprWrites(IrExpr *E, LinLoopWrites *W)
{
    int i;
    if (!E) return;
    if (E->Kind == IrExAssign || (E->Kind == IrExUnary &&
        (E->Op == TokenIncrement || E->Op == TokenDecrement))) {
        IrExpr *L = E->Left;
        if (L && L->Kind == IrExIdent && L->LocalSlot >= 0 && L->LocalSlot < IR_CALL_MAX_LOCALS) {
            W->slot[L->LocalSlot] = 1;
            if (E->Kind == IrExAssign && E->Op != TokenAssign) W->unsupported[L->LocalSlot] = 1;
        }
        else if (L && (L->Kind == IrExIdent || L->Kind == IrExIndex || L->Kind == IrExUnary || L->Kind == IrExMember))
            W->memory = 1;
    }
    if (E->Kind == IrExCall) { W->call = 1; W->memory = 1; }
    LinScanExprWrites(E->Left, W); LinScanExprWrites(E->Right, W);
    for (i = 0; E->Args && i < E->ArgCount; ++i) LinScanExprWrites(E->Args[i], W);
}
static void LinScanExprEffects(IrExpr *E, LinLoopWrites *W)
{
    int i;
    if (!E) return;
    if (E->Kind == IrExCall) { W->call = 1; W->memory = 1; }
    if ((E->Kind == IrExAssign || (E->Kind == IrExUnary
            && (E->Op == TokenIncrement || E->Op == TokenDecrement)))
        && E->Left != NULL && E->Left->Kind != IrExIdent)
        W->memory = 1;
    LinScanExprEffects(E->Left, W); LinScanExprEffects(E->Right, W);
    for (i = 0; E->Args && i < E->ArgCount; ++i) LinScanExprEffects(E->Args[i], W);
}
static void LinScanStmtEffects(IrStmt *S, LinLoopWrites *W)
{
    int i; if (!S) return;
    LinScanExprEffects(S->Expr, W);
    LinScanStmtEffects(S->ThenStmt, W); LinScanStmtEffects(S->ElseStmt, W);
    LinScanStmtEffects(S->BodyStmt, W); LinScanStmtEffects(S->InitStmt, W); LinScanStmtEffects(S->IncStmt, W);
    for (i = 0; S->Stmts && i < S->StmtCount; ++i) LinScanStmtEffects(S->Stmts[i], W);
}
static void LinScanStmtWritesForPromotion(IrStmt *S, LinLoopWrites *W)
{
    int i; if (!S) return;
    /* A nested loop has its own promotion region and may be replaced by a
     * native kernel before its body is lowered. Do not pin its local writes
     * in the parent region; retain call/indirect-memory effects conservatively. */
    if (S->Kind == IrStFor || S->Kind == IrStWhile || S->Kind == IrStDo) {
        LinScanStmtEffects(S, W);
        return;
    }
    LinScanExprWrites(S->Expr, W);
    LinScanStmtWritesForPromotion(S->ThenStmt, W);
    LinScanStmtWritesForPromotion(S->ElseStmt, W);
    LinScanStmtWritesForPromotion(S->BodyStmt, W);
    LinScanStmtWritesForPromotion(S->InitStmt, W);
    LinScanStmtWritesForPromotion(S->IncStmt, W);
    for (i = 0; S->Stmts && i < S->StmtCount; ++i)
        LinScanStmtWritesForPromotion(S->Stmts[i], W);
}
static void LinScanStmtWrites(IrStmt *S, LinLoopWrites *W)
{
    int i; if (!S) return;
    LinScanExprWrites(S->Expr, W);
    LinScanStmtWrites(S->ThenStmt, W); LinScanStmtWrites(S->ElseStmt, W);
    LinScanStmtWrites(S->BodyStmt, W); LinScanStmtWrites(S->InitStmt, W); LinScanStmtWrites(S->IncStmt, W);
    for (i = 0; S->Stmts && i < S->StmtCount; ++i) LinScanStmtWrites(S->Stmts[i], W);
}
static int LowerStmtRec1(IrStmt *S, IrLinInst *Code, int Cap, int *N, int *Next);

/* Condition lowering emits false edges only. The true path falls through, so
 * hot loop/if conditions avoid both boolean temporaries and extra jumps. */
static void CondPatch(IrLinInst *Code, int head, int target)
{
    while (head >= 0) {
        int next = (int)Code[head].Imm;
        Code[head].Imm = target;
        head = next;
    }
}
static int CondEmitPatch(IrLinInst *Code, int Cap, int *N, IrLinInst In, int *head)
{
    int pc = *N;
    In.Imm = *head;
    if (!Emit(Code, Cap, N, In)) return 0;
    *head = pc;
    return 1;
}
static int CondIsCompare(enum LexToken op)
{
    return op == TokenEqual || op == TokenNotEqual || op == TokenLessThan
        || op == TokenGreaterThan || op == TokenLessEqual || op == TokenGreaterEqual;
}
static enum LexToken CondInvertCompare(enum LexToken op)
{
    switch (op) {
    case TokenEqual: return TokenNotEqual;
    case TokenNotEqual: return TokenEqual;
    case TokenLessThan: return TokenGreaterEqual;
    case TokenGreaterThan: return TokenLessEqual;
    case TokenLessEqual: return TokenGreaterThan;
    case TokenGreaterEqual: return TokenLessThan;
    default: return op;
    }
}
static int LowerCondFalse(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next,
                          int *false_head, int depth)
{
    IrLinInst In;
    int a, local, skip;
    if (E == NULL || depth > 64) return 0;
    if (E->Kind == IrExBinOp && E->Op == TokenLogicalAnd)
        return LowerCondFalse(E->Left, Code, Cap, N, Next, false_head, depth+1)
            && LowerCondFalse(E->Right, Code, Cap, N, Next, false_head, depth+1);
    if (E->Kind == IrExBinOp && E->Op == TokenLogicalOr) {
        local = -1;
        if (!LowerCondFalse(E->Left, Code, Cap, N, Next, &local, depth+1)) return 0;
        /* Left true falls through this jump to the end; left false enters RHS. */
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
        skip = *N;
        if (!Emit(Code, Cap, N, In)) return 0;
        CondPatch(Code, local, *N);
        if (!LowerCondFalse(E->Right, Code, Cap, N, Next, false_head, depth+1)) return 0;
        Code[skip].Imm = *N;
        return 1;
    }
    if (E->Kind == IrExTernary && E->Left && E->Right && E->Args
        && E->ArgCount > 0 && E->Args[0]) {
        local = -1;
        if (!LowerCondFalse(E->Left, Code, Cap, N, Next, &local, depth+1)) return 0;
        if (!LowerCondFalse(E->Right, Code, Cap, N, Next, false_head, depth+1)) return 0;
        /* True selector skips the false arm after evaluating the true arm. */
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
        skip = *N;
        if (!Emit(Code, Cap, N, In)) return 0;
        CondPatch(Code, local, *N);
        if (!LowerCondFalse(E->Args[0], Code, Cap, N, Next, false_head, depth+1)) return 0;
        Code[skip].Imm = *N;
        return 1;
    }
    if (E->Kind == IrExBinOp && CondIsCompare(E->Op) && E->Left && E->Right
        && !(E->Left->TypeFlags & IR_TF_FP) && !(E->Right->TypeFlags & IR_TF_FP)) {
        int b;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &a)
            || !LowerRec(E->Right, Code, Cap, N, Next, &b)) return 0;
        In.Op = IrLinBrCmp;
        In.Dest = ((E->Left->TypeFlags | E->Right->TypeFlags) & IR_TF_UNSIGNED) ? 1 : 0;
        In.A = (unsigned char)a; In.B = (unsigned char)b;
        In.Tok = CondInvertCompare(E->Op); In.Imm = -1;
        return CondEmitPatch(Code, Cap, N, In, false_head);
    }
    if (!LowerRec(E, Code, Cap, N, Next, &a)) return 0;
    In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)a; In.B = 0;
    In.Tok = 0; In.Imm = -1;
    return CondEmitPatch(Code, Cap, N, In, false_head);
}
/* Lowering refusals are silent by design (the body stays on the residual
 * path). Under BABYC_VM_PROFILE report the innermost refused statement once
 * per refusal chain, so linear STRICT fallbacks can be triaged by line. */
static int LinRejectReported;
static int LowerStmtRec(IrStmt *S, IrLinInst *Code, int Cap, int *N, int *Next)
{
    extern void LinCseReset(void);
    if (!LinCseLoopRegion) LinCseReset();
    int ok = LowerStmtRec1(S, Code, Cap, N, Next);
    if (ok && LinCseLoopRegion && S != NULL) {
        LinLoopWrites writes;
        memset(&writes, 0, sizeof(writes));
        LinScanStmtWrites(S, &writes);
        LinCseInvalidateLoop(writes.slot, writes.memory, writes.call);
        if (ok) LinCsePinLiveEntries();
    }
    if (!ok && S != NULL && IrVmProfileOn() && !LinRejectReported)
    {
        LinRejectReported = 1;
        fprintf(stderr, "[babyc-exec-profile] lower_reject stmt_kind=%d line=%d expr_kind=%d expr_op=%d\n",
                (int)S->Kind, S->SourceLine,
                S->Expr ? (int)S->Expr->Kind : -1, S->Expr ? (int)S->Expr->Op : -1);
    }
    if (ok)
        LinRejectReported = 0;
    return ok;
}
static int LowerStmtRec1(IrStmt *S, IrLinInst *Code, int Cap, int *N, int *Next)
{
    IrLinInst In;
    int dest, i, br, jmp, head, end;
    if (S == NULL)
        return 1;
    /* Temporaries are statement-local; cached values are protected by register pins. */
    *Next = 0;
    if (S->Kind == IrStCase || S->Kind == IrStDefault)
        return 1;
    /* Per-statement LINE for F7 debug (only when BABYC_DEBUG). Skip pure meta. */
    if (S->Kind != IrStLabel && S->Kind != IrStBlock
        && S->SourceLine > 0
        && !EmitLine(Code, Cap, N, S->SourceLine))
        return 0;
    if (S->Kind == IrStLabel)
    {
        LinLab *L = LabGet(S->DeclIdent);
        if (L == NULL || L->Pc >= 0) return 0;
        L->Pc = *N;
        Patch(Code, L->Patch, L->NPatch, *N);
        return 1;
    }
    if (S->Kind == IrStGoto)
    {
        LinLab *L = LabGet(S->DeclIdent);
        LinPromSyncAll(Code, Cap, N);
        if (L == NULL) return 0;
        if (L->Pc >= 0)
        {
            In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = L->Pc;
            return Emit(Code, Cap, N, In);
        }
        if (L->NPatch >= 8) return 0;
        L->Patch[L->NPatch++] = *N;
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
        return Emit(Code, Cap, N, In);
    }
    if (S->Kind == IrStDecl)
    {
        /* Runtime-sized arrays need the tree executor's per-frame allocation
         * and dynamic sizeof metadata. Until compact bytecode has a matching
         * VLA opcode, reject this body rather than treating the bound as a
         * pointer value and passing an invalid address to host intrinsics. */
        if (S->DeclArraySize == -2)
            return 0;
        /* Name for uninit diagnostics: survive compile→run frame switch. */
        if (S->DeclIdent != NULL && S->LocalSlot >= 0 && S->LocalSlot < IR_CALL_MAX_LOCALS)
            CompileStampName(S->LocalSlot, S->DeclIdent);
        /* Stamp Decl meta (CompLocal + FnSlot incl. SlotIsFptr) before any
         * branch so Assign/Call later in this body see fptr flags
         * (fp = tab[i]; fp(10) needs Tok=5 store + indirect call). */
        if (S->LocalSlot >= 0 && S->LocalSlot < IR_CALL_MAX_LOCALS
            && (S->DeclFromType != NULL || S->DeclIsFptr || S->DeclIsPtr))
            IrStorageDescStampDecl(S->LocalSlot, S);
        /* Static struct/union BSS: IR_K_DECL_STATIC_STU. */
        if (S->DeclIsStatic && !S->DeclIsFptr && !S->DeclIsPtr
            && S->DeclArraySize <= 1
            && S->DeclFromType != NULL
            && (S->DeclFromType->Base == TypeStruct
                || S->DeclFromType->Base == TypeUnion)
            && S->DeclInitCount <= 0 && S->DeclInitVals == NULL
            && S->LocalSlot >= 0 && KernelN < 16
            && S->DeclIdent != NULL)
        {
            IrLinInst Kin;
            size_t nbytes = S->DeclFromType->Sizeof > 0
                ? (size_t)S->DeclFromType->Sizeof : 0;
            if (nbytes == 0 || nbytes > 4096)
                return 0;
            Kin.Op = IrLinKernel;
            Kin.Dest = (unsigned char)S->LocalSlot;
            Kin.A = 0; Kin.B = 0;
            Kin.Tok = IR_K_DECL_STATIC_STU;
            Kin.Imm = KernelN;
            KernelStmtTab[KernelN] = S;
            KernelN++;
            if (!Emit(Code, Cap, N, Kin)) return 0;
            return 1;
        }
        /* Static scalar-element array: IR_K_DECL_STATIC_ARR (BSS persist blob).
         * DeclIsPtr may be set (LocalVal = blob addr) — still an array Decl. */
        if (S->DeclIsStatic && !S->DeclIsFptr
            && S->DeclArraySize > 0 && !S->DeclElemIsPtr
            && S->LocalSlot >= 0 && KernelN < 16
            && S->DeclIdent != NULL)
        {
            IrLinInst Kin;
            /* GUARD: resolve storage width through the shared metadata helper. */
            int es = IrResolveDeclElemSize(S->DeclElemSize, S->DeclFromType);
            if (es != 1 && es != 2 && es != 4 && es != 8)
                es = (int)sizeof(int);
            Kin.Op = IrLinKernel;
            Kin.Dest = (unsigned char)S->LocalSlot;
            Kin.A = (unsigned char)es;
            Kin.B = 0;
            Kin.Tok = IR_K_DECL_STATIC_ARR;
            Kin.Imm = KernelN;
            KernelStmtTab[KernelN] = S;
            KernelN++;
            /* Stamp dims for Index lower (P0 descriptor). */
            if (S->LocalSlot < IR_CALL_MAX_LOCALS)
                IrStorageDescStampDeclEs(S->LocalSlot, S, es);
            if (!Emit(Code, Cap, N, Kin)) return 0;
            return 1;
        }
        /* Scalar static local: IR_K_DECL_STATIC loads/creates persist entry. */
        if (S->DeclIsStatic && !S->DeclIsPtr && !S->DeclIsFptr
            && S->DeclArraySize <= 0 && S->LocalSlot >= 0 && KernelN < 16
            && S->DeclIdent != NULL)
        {
            IrLinInst Kin;
            Kin.Op = IrLinKernel;
            Kin.Dest = (unsigned char)S->LocalSlot;
            Kin.A = 0; Kin.B = 0;
            Kin.Tok = IR_K_DECL_STATIC;
            Kin.Imm = KernelN;
            KernelStmtTab[KernelN] = S;
            KernelN++;
            if (!Emit(Code, Cap, N, Kin)) return 0;
            return 1;
        }
        /* Array Decl: under EXEC=vm prod, run tree Decl once (alloc blob +
         * brace-init) then continue lowering the rest of the body so Index
         * / loops stay on bytecode. Under both/shadow keep skip so tree
         * owns the Decl (no double-alloc). Ptr/fptr Decl still skip. */
        /* Value array T a[N] (DeclArraySize>0, !DeclElemIsPtr, !DeclIsFptr):
         * under EXEC=vm run tree Decl once (alloc+init), then continue.
         * DeclIsPtr is often also set (LocalVal holds blob addr) — ignore it. */
        /* Array of struct: DeclFromType is the element struct type with
         * DeclArraySize>1 (ir_compile convention). Scalar struct uses
         * DeclArraySize<=1 → DECL_STRUCT. */
        /* int *a[N] or int (*tab[N])(...): array of pointer / fptr words. */
        if (S->DeclArraySize > 0 && (S->DeclElemIsPtr || S->DeclIsFptr)
            && IrExecIsVmProd() && IrVmLinearFeaturesOn() && LinParser != NULL
            && S->LocalSlot >= 0 && KernelN < 16)
        {
            IrLinInst Kin;
            int es = (int)sizeof(void *);
            long long nbytes = (long long)S->DeclArraySize * (long long)es;
            if (S->DeclArraySize > 1024 || nbytes > 8192)
                return 0;
            /* Brace-init: calloc supplies omitted/zero elements.  Non-constant
             * pointer initializers must be lowered into Linear stores below;
             * they must not be evaluated by the tree expression evaluator in
             * IR_K_DECL_ARRAY. */
            if (S->DeclInitCount > 0 && S->DeclInitVals == NULL
                && S->DeclInitExprs == NULL)
                return 0;
            if (S->DeclInitCount > S->DeclArraySize)
                return 0; /* preserve the parser's excess-initializer diagnostic */
            /* P0: array-of-ptr / fptr-array layout via descriptor (incl. FnSlot). */
            if (S->LocalSlot >= 0 && S->LocalSlot < IR_CALL_MAX_LOCALS)
                IrStorageDescStampDeclEs(S->LocalSlot, S, (int)sizeof(void *));
            KernelStmtTab[KernelN] = S;
            Kin.Op = IrLinKernel;
            Kin.Dest = 0;
            Kin.A = (unsigned char)es;
            Kin.B = 0;
            Kin.Tok = IR_K_DECL_ARRAY;
            Kin.Imm = KernelN;
            KernelN++;
            if (!Emit(Code, Cap, N, Kin))
            {
                KernelN--;
                return 0;
            }
            /* Evaluate brace initializer expressions in the selected Linear
             * engine and write pointer-sized elements through the same checked
             * store used for ordinary *p = value.  The allocation kernel has
             * zero-filled the whole object, so omitted entries stay null. */
            if (S->DeclInitCount > 0)
            {
                int ii;
                for (ii = 0; ii < S->DeclInitCount; ii++)
                {
                    int vr = -1, base, addr;
                    int next_mark = *Next;
                    long long cv = (S->DeclInitVals != NULL)
                        ? S->DeclInitVals[ii] : 0;
                    IrLinInst St;
                    /* Each initializer is evaluated and stored before the next
                     * one begins. Reuse its temporary registers instead of
                     * consuming three registers per element for the whole
                     * declaration (large pointer arrays otherwise exceed the
                     * 64-register budget despite having no live cross-element
                     * values). Drop compile-time CSE pins before reusing them. */
                    LinCseReset();
                    *Next = next_mark;
                    if (S->DeclInitExprs != NULL && S->DeclInitExprs[ii] != NULL)
                    {
                        if (!LowerRec(S->DeclInitExprs[ii], Code, Cap, N, Next, &vr)
                            || vr < 0)
                            return 0;
                    }
                    else
                    {
                        vr = AllocReg(Next);
                        if (vr < 0) return 0;
                        St.Op = IrLinImm; St.Dest = (unsigned char)vr;
                        St.A = St.B = 0; St.Tok = 0; St.Imm = cv;
                        if (!Emit(Code, Cap, N, St)) return 0;
                    }
                    base = AllocReg(Next);
                    if (base < 0) return 0;
                    St.Op = IrLinImm; St.Dest = (unsigned char)base;
                    St.A = St.B = 0; St.Tok = 4; St.Imm = S->LocalSlot;
                    if (!Emit(Code, Cap, N, St)) return 0;
                    addr = base;
                    if (ii != 0)
                    {
                        addr = AllocReg(Next);
                        if (addr < 0) return 0;
                        St.Op = IrLinAddI; St.Dest = (unsigned char)addr;
                        St.A = (unsigned char)base; St.B = 0;
                        St.Tok = 0; St.Imm = (long long)ii * (long long)es;
                        if (!Emit(Code, Cap, N, St)) return 0;
                    }
                    St.Op = IrLinStoreIndex; St.Dest = 0;
                    St.A = (unsigned char)vr; St.B = (unsigned char)addr;
                    St.Tok = 4; /* explicitly typed pointer-element store */
                    St.Imm = es;
                    if (!Emit(Code, Cap, N, St)) return 0;
                    LinCseReset();
                    *Next = next_mark;
                }
            }
            return 1;
        }
        if (S->DeclArraySize > 0 && !S->DeclElemIsPtr && !S->DeclIsFptr
            /* DeclArraySize==1 is also used for scalar struct temporaries.
             * An uninitialized DeclIsPtr with no initializer owns array storage;
             * initialized aggregate temporaries must retain their copy path. */
            && (S->DeclArraySize > 1
                || (S->DeclArraySize == 1 && S->DeclIsPtr && S->Expr == NULL
                    && S->DeclInitCount <= 0 && S->DeclInitVals == NULL
                    && S->DeclInitExprs == NULL))
            && IrExecIsVmProd() && LinParser != NULL)
        {
            /* Linear-safe Decl for int-scale arrays (ARRAY_ACCUM/isort).
             * Scalar struct/union (ArraySize==1 convention) → DECL_STRUCT below.
             * Resolve elem size: DeclElemSize, else array FromType, else int.
             * char[] refused → tree. */
            IrLinInst Kin;
            /* GUARD: resolve storage width once; don't bypass the metadata
             * helper with a local FromType->Sizeof cascade. */
            int es = IrResolveDeclElemSize(S->DeclElemSize, S->DeclFromType);
            if (S->LocalSlot < 0 || KernelN >= 16)
                return 0;
            if (es <= 0 || es > 65535)
                return 0;
            /* Artificial size caps removed: IrChargeLocalArrayOrFail at
             * runtime applies the same virtual stack-budget check as the
             * tree path. Arrays that fit run; truly huge ones fail with
             * stack overflow instead of silent tree fallback. */
            if (S->DeclArraySize <= 0 || S->DeclArraySize > 16 * 1024 * 1024)
                return 0;
            /* DECL_ARRAY has a 16-bit stride encoded in A:B. Preserve large
             * struct element sizes instead of narrowing them to one byte. */
            KernelStmtTab[KernelN] = S;
            Kin.Op = IrLinKernel;
            Kin.Dest = 0;
            Kin.A = (unsigned char)(es & 0xff);
            Kin.B = (unsigned char)((es >> 8) & 0xff);
            Kin.Tok = IR_K_DECL_ARRAY;
            Kin.Imm = KernelN;
            KernelN++;
            /* Stamp compile-time dims for multidim Index lower (P0 descriptor). */
            if (S->LocalSlot >= 0 && S->LocalSlot < IR_CALL_MAX_LOCALS)
                IrStorageDescStampDeclEs(S->LocalSlot, S, es);
            if (!Emit(Code, Cap, N, Kin))
            {
                KernelN--;
                return 0;
            }
            return 1;
        }
        /* EXEC=vm: scalar pointer Decl with lowerable init (malloc/cast/ident).
         * Store bits to LocalVal + mark LocalIsPtr so p->m Load/Store work.
         * No LinSkippedCall — body can stay on bytecode. */
        /* EXEC=vm: any scalar pointer Decl (int, char, Node, ...). */
        /* Local struct/union value: owned blob for s.m under EXEC=vm. */
        /* Local struct/union value: owned blob for s.m under EXEC=vm.
         * Brace-init OK when DeclInitVals is flat int list (no nested exprs). */
        /* `struct S t = expr;` compiles as a one-element owned blob with
         * DeclIsPtr set (ir_compile): it is still a single aggregate object
         * whose initializer is copied by value, never aliased. */
        if (S->DeclFromType != NULL
            && (S->DeclFromType->Base == TypeStruct || S->DeclFromType->Base == TypeUnion)
            && (!S->DeclIsPtr
                || (S->DeclArraySize == 1 && S->Expr != NULL && !S->DeclElemIsPtr
                    && S->DeclInitCount <= 0 && S->DeclInitVals == NULL
                    && S->DeclInitExprs == NULL))
            && S->DeclArraySize <= 1 && !S->DeclIsFptr
            && S->LocalSlot >= 0 && KernelN < 16)
        {
            IrLinInst Kin;
            size_t nbytes = S->DeclFromType->Sizeof > 0
                ? (size_t)S->DeclFromType->Sizeof : 1;
            /* Bitfield members: blob is still Sizeof bytes. Load/StoreIndex Tok=1
             * stamps BitWidth and uses IrBitExtract/Insert in Step — same as
             * array-of-bitfield-struct path. Do not refuse the Decl. */
            if (nbytes > 16 * 1024 * 1024)
                return 0;
            /* Brace-init is a Linear feature; STRICT only governs fallback policy. */
            if (S->DeclInitCount > 0 || S->DeclInitVals != NULL)
            {
                if (!IrVmLinearFeaturesOn())
                    return 0;
                if (nbytes % sizeof(int) != 0)
                    return 0;
                if (S->DeclInitCount > (int)(nbytes / sizeof(int)) + 8)
                    return 0;
            }
            Kin.Op = IrLinKernel;
            Kin.Dest = (unsigned char)S->LocalSlot;
            Kin.A = 0; Kin.B = 0;
            Kin.Tok = IR_K_DECL_STRUCT;
            Kin.Imm = KernelN;
            KernelStmtTab[KernelN] = S;
            KernelN++;
            /* P0: struct/union FromType stamp via descriptor. */
            if (S->LocalSlot >= 0 && S->LocalSlot < IR_CALL_MAX_LOCALS)
                IrStorageDescStampDecl(S->LocalSlot, S);
            if (!Emit(Code, Cap, N, Kin)) return 0;
            /* Non-constant brace-init ({ x + 1, y - 5 }, { .b = x * 2 }):
             * IR_K_DECL_STRUCT packs the folded constants (DeclInitVals,
             * 0 for non-const slots); each DeclInitExprs[vi] is then lowered
             * here and stored at the target given by IrBraceInitPlan, the
             * same layout walk used by the tree pack (nested aggregates,
             * array members, designators). Targets that need a conversion
             * the raw store cannot express (bitfield, float, FP<->int) keep
             * the body on the residual path instead of storing wrong bits. */
            if (S->DeclInitExprs != NULL && S->DeclInitCount > 0
                && S->DeclFromType->Members != NULL)
            {
                IrBracePlace PlanLocal[64];
                int used, vi;
                if (S->DeclInitCount > 64)
                    return 0;
                used = IrBraceInitPlan(S->DeclFromType, S->DeclInitNames,
                                       S->DeclInitCount, 1, 0, PlanLocal);
                for (vi = 0; vi < S->DeclInitCount; vi++)
                {
                    IrExpr *IE = S->DeclInitExprs[vi];
                    IrBracePlace *P = &PlanLocal[vi];
                    int rd = -1, base, addr, es, ie_fp, m_fp;
                    IrLinInst St;
                    if (IE == NULL)
                        continue;
                    if (vi >= used)
                        return 0; /* excess initializer: keep tree diagnostics */
                    ie_fp = (IE->TypeFlags & IR_TF_FP) ? 1 : 0;
                    if (!LowerRec(IE, Code, Cap, N, Next, &rd) || rd < 0)
                        return 0;
                    if (P->Off < 0)
                        continue; /* evaluated for side effects only */
                    if (P->MV != NULL && P->MV->BitWidth > 0)
                        return 0;
                    if (P->MV != NULL && P->MV->Typ != NULL)
                        es = P->MV->Typ->Sizeof;
                    else
                        es = 0;
                    if (P->MV == NULL)
                    {
                        enum BaseType EB = P->EB;
                        es = (EB == TypeChar || EB == TypeUnsignedChar) ? 1
                           : (EB == TypeShort || EB == TypeUnsignedShort) ? 2
                           : (EB == TypeLong || EB == TypeUnsignedLong
                              || EB == TypeLongLong || EB == TypeUnsignedLongLong
                              || EB == TypePointer || EB == TypeFP) ? 8 : 4;
                        if ((EB == TypeLong || EB == TypeUnsignedLong) && sizeof(long) == 4)
                            es = 4;
                    }
                    m_fp = (P->EB == TypeFP);
                    if (m_fp != ie_fp)
                        return 0; /* int<->FP conversion residual */
                    if (m_fp && es != 8)
                        return 0; /* float member residual */
                    if (es != 1 && es != 2 && es != 4 && es != 8)
                        return 0;
                    base = AllocReg(Next);
                    if (base < 0) return 0;
                    St.Op = IrLinImm; St.Dest = (unsigned char)base;
                    St.A = St.B = 0; St.Tok = 4; St.Imm = S->LocalSlot;
                    if (!Emit(Code, Cap, N, St)) return 0;
                    addr = base;
                    if (P->Off != 0)
                    {
                        addr = AllocReg(Next);
                        if (addr < 0) return 0;
                        St.Op = IrLinAddI; St.Dest = (unsigned char)addr;
                        St.A = (unsigned char)base; St.B = 0;
                        St.Tok = 0; St.Imm = P->Off;
                        if (!Emit(Code, Cap, N, St)) return 0;
                    }
                    St.Op = IrLinStoreIndex; St.Dest = 0;
                    St.A = (unsigned char)rd;
                    St.B = (unsigned char)addr;
                    St.Tok = 2; St.Imm = es;
                    if (!Emit(Code, Cap, N, St)) return 0;
                }
            }
            /* struct S t = make(...): DECL then by-value copy from Call RetBuf.
             * Without this, DECL zeros the blob and the Call init is dropped (514/647).
             * Sizeof>4 excluded 4-byte bitfield structs (514) — use Sizeof>0. */
            /* Any other aggregate initializer (s.m, a[i], *p, global, local,
             * va_arg, ?:) lowers to its source object's address: same copy. */
            if (S->Expr != NULL
                && (S->Expr->Kind == IrExCall
                    || (S->Expr->CanonicalType != NULL
                        && IrTypeIsCopyableAggregate(S->Expr->CanonicalType)))
                && S->DeclFromType != NULL
                && S->DeclFromType->Sizeof > 0
                && S->DeclFromType->Sizeof <= (S->Expr->Kind == IrExCall ? 512 : 4096))
            {
                int src_reg = -1, dst_reg = -1;
                int nbytes = (int)S->DeclFromType->Sizeof;
                if (!LowerRec(S->Expr, Code, Cap, N, Next, &src_reg))
                    return 0;
                dst_reg = AllocReg(Next);
                if (dst_reg < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)dst_reg;
                In.A = In.B = 0; In.Tok = 4; In.Imm = S->LocalSlot;
                if (!Emit(Code, Cap, N, In)) return 0;
                In.Op = IrLinStoreIndex; In.Dest = 0;
                In.A = (unsigned char)src_reg;
                In.B = (unsigned char)dst_reg;
                In.Tok = 3;
                In.Imm = nbytes;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else if (S->Expr != NULL)
                return 0; /* non-Call init residual */
            return 1;
        }
        if (S->DeclFromType != NULL
            && (S->DeclFromType->Base == TypeStruct || S->DeclFromType->Base == TypeUnion)
            && !S->DeclIsPtr && S->DeclArraySize <= 1 && !S->DeclIsFptr)
            return 0; /* complex brace residual */
        if (S->DeclIsPtr && !S->DeclIsFptr && S->DeclArraySize <= 0
            && IrExecIsVmProd() && S->LocalSlot >= 0)
        {
            int rd = -1;
            IrCallFrame *Fr;
            int ok_init = (S->Expr == NULL);
            if (S->Expr != NULL)
                ok_init = LowerRec(S->Expr, Code, Cap, N, Next, &rd);
            else
            {
                /* bare Node *p; → null */
                rd = AllocReg(Next);
                if (rd < 0) ok_init = 0;
                else
                {
                    In.Op = IrLinImm; In.Dest = (unsigned char)rd;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
                    ok_init = Emit(Code, Cap, N, In);
                }
            }
            if (ok_init && rd >= 0)
            {
                /* P0: CompLocal + FnSlot + Frame (ptr, init, elem size, skip owned) */
                Fr = (IrCallDepth > 0) ? IrCallStack[IrCallDepth - 1] : NULL;
                IrStorageDescStampDeclFull(S->LocalSlot, S, Fr,
                    IR_STAMP_FRAME_PTR | IR_STAMP_FRAME_INIT
                    | IR_STAMP_FRAME_SKIP_OWNED | IR_STAMP_FRAME_ELEMSIZE
                    | IR_STAMP_FRAME_NAME | IR_STAMP_FRAME_TYPE);
                In.Op = IrLinStoreLocal; In.Dest = 0;
                In.A = (unsigned char)rd; In.B = 0; In.Tok = 0;
                In.Imm = S->LocalSlot;
                return Emit(Code, Cap, N, In);
            }
            /* init not lowerable: fall through to skip */
        }
        if (S->DeclIsFptr && S->LocalSlot >= 0 && S->DeclArraySize <= 0)
        {
            /* Scalar int (*fp)(...) = add; NOT int (*tab[N])(...). */
            int rd = -1;
            {
                IrCallFrame *FrFp = (IrCallDepth > 0) ? IrCallStack[IrCallDepth - 1] : NULL;
                IrStorageDescStampDeclFull(S->LocalSlot, S, FrFp,
                    IR_STAMP_FRAME_FPTR | IR_STAMP_FRAME_NAME
                    | IR_STAMP_FRAME_TYPE);
            }
            if (S->Expr != NULL)
            {
                IrExpr *RX = S->Expr;
                if (RX->Kind == IrExUnary && RX->Op == TokenAmpersand && RX->Left != NULL)
                    RX = RX->Left; /* &fred */
                if (RX->Kind == IrExIdent && RX->Ident != NULL && LinParser != NULL)
                {
                    struct Value *TV = NULL;
                    TV = IrGlobalIdentSoft(LinParser, RX->Ident);
                    if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
                    {
                        rd = AllocReg(Next);
                        if (rd >= 0)
                        {
                            IrLinInst Im;
                            Im.Op = IrLinImm; Im.Dest = (unsigned char)rd;
                            Im.A = Im.B = 0; Im.Tok = 5;
                            Im.Imm = (long long)(intptr_t)TV;
                            if (!Emit(Code, Cap, N, Im))
                                return 0;
                        }
                    }
                }
                if (rd < 0 && !LowerRec(S->Expr, Code, Cap, N, Next, &rd))
                    rd = -1;
            }
            else
            {
                rd = AllocReg(Next);
                if (rd >= 0)
                {
                    IrLinInst Im;
                    Im.Op = IrLinImm; Im.Dest = (unsigned char)rd;
                    Im.A = Im.B = 0; Im.Tok = 0; Im.Imm = 0;
                    if (!Emit(Code, Cap, N, Im)) return 0;
                }
            }
            if (rd >= 0)
            {
                IrLinInst St;
                St.Op = IrLinStoreLocal; St.Dest = 0;
                St.A = (unsigned char)rd; St.B = 0; St.Tok = 5; /* fptr slot */
                St.Imm = S->LocalSlot;
                return Emit(Code, Cap, N, St);
            }
            /* Fptr Decl not fully lowerable: keep body eligible, skip this Decl. */
            return 1;
        }
        if (S->DeclIsPtr || S->DeclArraySize > 0)
        {
            /* Pointer/array decl with a lowerable call init (malloc, ...):
             * lower the call and store the pointer bits. Only skip if the
             * call cannot be lowered. */
            if (S->Expr != NULL && S->Expr->Kind == IrExCall
                && S->LocalSlot >= 0)
            {
                int dest = 0;
                if (LowerRec(S->Expr, Code, Cap, N, Next, &dest))
                {
                    /* P0: ptr/array Decl with Call init — unified frame stamp */
                    {
                        IrCallFrame *FrCall = (IrCallDepth > 0)
                            ? IrCallStack[IrCallDepth - 1] : NULL;
                        int ff = IR_STAMP_FRAME_SKIP_OWNED | IR_STAMP_FRAME_INIT
                            | IR_STAMP_FRAME_NAME | IR_STAMP_FRAME_TYPE;
                        if (S->DeclIsFptr)
                            ff |= IR_STAMP_FRAME_FPTR;
                        else
                            ff |= IR_STAMP_FRAME_PTR;
                        IrStorageDescStampDeclFull(S->LocalSlot, S, FrCall, ff);
                    }
                    In.Op = IrLinStoreLocal; In.Dest = 0;
                    In.A = (unsigned char)dest; In.B = 0; In.Tok = 0;
                    In.Imm = S->LocalSlot;
                    return Emit(Code, Cap, N, In);
                }
            }
            if (S->LocalSlot >= 0 && S->LocalSlot < 128)
                CallSkip[S->LocalSlot] = 1;
            LinSkippedCall = 1; LinSkipCallCount++;
            return 1;
        }
        /* Enum constants compile as Decl with DeclInitVals, no Expr. */
        if (S->Expr == NULL && S->LocalSlot >= 0
            && S->DeclInitVals != NULL && S->DeclInitCount > 0)
        {
            int rd = AllocReg(Next);
            int ff;
            if (rd < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)rd;
            In.A = In.B = 0; In.Tok = 0;
            In.Imm = S->DeclInitVals[0];
            if (!Emit(Code, Cap, N, In)) return 0;
            /* Nested compile must not poison live FromType (790 fused uninit). */
            ff = IR_STAMP_FRAME_INIT | IR_STAMP_FRAME_NAME;
            if (!LinCompileActive || CompileFrameSp == 0)
                ff |= IR_STAMP_FRAME_TYPE;
            if (IrCallDepth > 0 && S->LocalSlot < IR_CALL_MAX_LOCALS)
                IrStorageDescStampDeclFull(S->LocalSlot, S,
                    IrCallStack[IrCallDepth - 1], ff);
            In.Op = IrLinStoreLocal; In.Dest = 0;
            In.A = (unsigned char)rd; In.B = 0; In.Tok = 0;
            In.Imm = S->LocalSlot;
            return Emit(Code, Cap, N, In);
        }
        if (S->Expr == NULL || S->LocalSlot < 0)
        {
            /* Bare int n; — stamp name/type so BoundIsVar / uninit work.
             * No INIT: slot stays uninit until first store. */
            if (S->LocalSlot >= 0 && IrCallDepth > 0
                && S->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                int ff = IR_STAMP_FRAME_NAME;
                if (!LinCompileActive || CompileFrameSp == 0)
                    ff |= IR_STAMP_FRAME_TYPE;
                if (S->DeclIdent != NULL)
                    CompileStampName(S->LocalSlot, S->DeclIdent);
                IrStorageDescStampDeclFull(S->LocalSlot, S,
                    IrCallStack[IrCallDepth - 1], ff);
            }
            return 1;
        }
        /* Decl with Call init: try lower first (FnCode path). Only skip if
         * the call cannot be lowered. */
        if (S->Expr->Kind == IrExCall)
        {
            if (LowerRec(S->Expr, Code, Cap, N, Next, &dest))
            {
                In.Op = IrLinStoreLocal; In.Dest = 0;
                In.A = (unsigned char)dest; In.B = 0; In.Tok = 0;
                In.Imm = S->LocalSlot;
                return Emit(Code, Cap, N, In);
            }
            if (S->LocalSlot >= 0 && S->LocalSlot < 128) CallSkip[S->LocalSlot] = 1;
            LinSkippedCall = 1; LinSkipCallCount++;
            return 1;
        }
        if (!LowerRec(S->Expr, Code, Cap, N, Next, &dest))
            return 0;
        /* C scalar initializers apply the same implicit conversion as scalar
         * assignments. Keep pointer/array/aggregate initialization untouched. */
        if (S->DeclFromType != NULL
            && !IrLinEmitScalarConversion(S->DeclFromType->Base, S->Expr,
                                          &dest, Code, Cap, N, Next))
            return 0;
        /* Stamp DeclFromType so IrNarrowToLocalType works under pure VM.
         * INIT is runtime StoreLocal; do not mark init at compile time
         * (790 fused uninit). Nested compile skips TYPE to avoid poison. */
        if (IrCallDepth > 0 && S->LocalSlot >= 0 && S->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            int ff = IR_STAMP_FRAME_NAME | IR_STAMP_FRAME_INIT;
            if (!LinCompileActive || CompileFrameSp == 0)
                ff |= IR_STAMP_FRAME_TYPE;
            IrStorageDescStampDeclFull(S->LocalSlot, S,
                IrCallStack[IrCallDepth - 1], ff);
        }
        In.Op = IrLinStoreLocal; In.Dest = 0;
        In.A = (unsigned char)dest; In.B = 0;
        In.Tok = S->DeclIsConst ? 1 : 0;
        In.Imm = S->LocalSlot;
        return Emit(Code, Cap, N, In);
    }
    if (S->Kind == IrStExpr)
    {
        if (S->Expr == NULL)
            return 1;
        /* Dead identity x = x: no side effects, drop (general DCE). */
        if (S->Expr->Kind == IrExAssign
            && (S->Expr->Op == 0 || S->Expr->Op == TokenAssign)
            && S->Expr->Left != NULL && S->Expr->Left->Kind == IrExIdent
            && S->Expr->Right != NULL && S->Expr->Right->Kind == IrExIdent
            && S->Expr->Left->LocalSlot >= 0
            && S->Expr->Left->LocalSlot == S->Expr->Right->LocalSlot)
            return 1;
        /* User call: lower if possible; host/printf → explicit skip. */
        if (S->Expr->Kind == IrExCall)
        {
            if (LowerRec(S->Expr, Code, Cap, N, Next, &dest))
                return 1;
            if (getenv("BABYC_SKIP_TRACE"))
                fprintf(stderr, "[skip-call-stmt] line=%d %s args=%d\n", S->SourceLine,
                    S->Expr->Ident ? S->Expr->Ident :
                    (S->Expr->Left && S->Expr->Left->Ident ? S->Expr->Left->Ident : "?"),
                    S->Expr->ArgCount);
            LinSkippedCall = 1; LinSkipCallCount++;
            return 1;
        }
        if (S->Expr->Kind == IrExAssign && S->Expr->Right != NULL
            && S->Expr->Right->Kind == IrExCall
            && S->Expr->Left != NULL && S->Expr->Left->Kind == IrExIdent
            && S->Expr->Left->LocalSlot >= 0 && S->Expr->Left->LocalSlot < 128)
        {
            if (LowerRec(S->Expr, Code, Cap, N, Next, &dest))
            {
                In.Op = IrLinStoreLocal; In.Dest = 0;
                In.A = (unsigned char)dest; In.B = 0; In.Tok = 0;
                In.Imm = S->Expr->Left->LocalSlot;
                return Emit(Code, Cap, N, In);
            }
            CallSkip[S->Expr->Left->LocalSlot] = 1;
            LinSkippedCall = 1; LinSkipCallCount++;
            return 1;
        }
        if (!LowerRec(S->Expr, Code, Cap, N, Next, &dest))
        {
            return 0;
        }
        if (S->Expr->Kind == IrExAssign && S->Expr->Left != NULL
            && S->Expr->Left->Kind == IrExIdent && S->Expr->Left->LocalSlot >= 0)
        {
            int ls = S->Expr->Left->LocalSlot;
            In.Op = IrLinStoreLocal; In.Dest = 0;
            In.A = (unsigned char)dest; In.B = 0; In.Tok = 0;
            In.Imm = ls;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (IrCallDepth > 0 && ls < IR_CALL_MAX_LOCALS
                && (S->Expr->Left->TypeFlags & IR_TF_PTR))
            {
                IrStorageDesc Dp;
                IrStorageDescFromType(NULL, 1, 0, &Dp);
                IrStorageDescApplyFrame(IrCallStack[IrCallDepth - 1], ls, &Dp,
                    NULL, IR_STAMP_FRAME_PTR | IR_STAMP_FRAME_INIT);
            }
            return 1;
        }
        return 1;
    }
    if (S->Kind == IrStBlock)
    {
        for (i = 0; i < S->StmtCount; i++)
        {
            *Next = 0;
            if (!LowerStmtRec(S->Stmts[i], Code, Cap, N, Next))
                return 0;
        }
        return 1;
    }
    if (S->Kind == IrStIf && S->Expr != NULL)
    {
        int fh = -1;
        if (!LowerCondFalse(S->Expr, Code, Cap, N, Next, &fh, 0)) return 0;
        LinCsePinLiveEntries();
        if (!LowerStmtRec(S->ThenStmt, Code, Cap, N, Next)) return 0;
        if (S->ElseStmt != NULL) {
            jmp = *N;
            In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            CondPatch(Code, fh, *N);
            if (!LowerStmtRec(S->ElseStmt, Code, Cap, N, Next)) return 0;
            Code[jmp].Imm = *N;
        } else CondPatch(Code, fh, *N);
        return 1;
    }
    if (S->Kind == IrStWhile && S->Expr != NULL)
    {
        /* Insert-shift shape: kernel (if enabled) or tight expanded (CSE a[j]). */
        if (IrExecIsVmProd() && IrCallDepth > 0 && IrShapeFpEnabled())
        {
            IrExpr *Cond = S->Expr, *L, *R, *Bound = NULL, *Cmp = NULL;
            IrStmt *Body = S->BodyStmt;
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            int jSlot = -1, aSlot = -1, keySlot = -1, body_n = 0, ok_shape = 0;
            int strict_gt = 1;
            if (Cond->Kind == IrExBinOp && Cond->Op == TokenLogicalAnd
                && Cond->Left && Cond->Right)
            {
                L = Cond->Left; R = Cond->Right;
                {
                    IrExpr *sides[2] = { L, R };
                    int si;
                    for (si = 0; si < 2; si++)
                    {
                        IrExpr *E = sides[si];
                        if (E->Kind == IrExBinOp && E->Op == TokenGreaterEqual
                            && E->Left && E->Left->Kind == IrExIdent
                            && E->Right && E->Right->Kind == IrExConst
                            && E->Right->ConstVal == 0)
                        { Bound = E; jSlot = E->Left->LocalSlot; break; }
                    }
                }
                Cmp = (Bound == L) ? R : (Bound == R) ? L : NULL;
                if (Bound && Cmp && Cmp->Kind == IrExBinOp
                    && (Cmp->Op == TokenGreaterThan || Cmp->Op == TokenGreaterEqual)
                    && Cmp->Left && Cmp->Left->Kind == IrExIndex
                    && Cmp->Right && Cmp->Right->Kind == IrExIdent)
                {
                    IrExpr *Ix = Cmp->Left;
                    strict_gt = (Cmp->Op == TokenGreaterThan);
                    keySlot = Cmp->Right->LocalSlot;
                    if (Ix->Left && Ix->Left->Kind == IrExIdent
                        && Ix->Right && Ix->Right->Kind == IrExIdent
                        && Ix->Right->LocalSlot == jSlot)
                        aSlot = Ix->Left->LocalSlot;
                }
                if (Body && Body->Kind == IrStBlock)
                    body_n = Body->StmtCount;
                else if (Body)
                    body_n = 1;
                if (jSlot >= 0 && aSlot >= 0 && keySlot >= 0
                    && body_n >= 2 && body_n <= 4
                    && aSlot < IR_CALL_MAX_LOCALS
                    && (Fr->LocalArraySize[aSlot] > 0 || IrExecIsVmProd()))
                    ok_shape = 1;
            }
            /* INSERT_SHIFT kernel only for pure 2-stmt body
             * (a[j+1]=a[j]; j--). Extra stmts (moves++) are not in the
             * kernel; emitting it caused runtime abort → tree fallback →
             * vm_strict fail. Tight expanded handles body_n 2..4. */
            if (ok_shape && body_n == 2 && IrKernelFpEnabledFor(S) && KernelN < 16
                && !LinPromStmtTouchesActive(S)
                && (Fr->LocalVal[aSlot] != 0 || IrExecIsVmProd()))
            {
                IrLinInst Kin;
                KernelStmtTab[KernelN] = S;
                Kin.Op = IrLinKernel; Kin.Dest = 0; Kin.A = 0; Kin.B = 0;
                Kin.Tok = IR_K_INSERT_SHIFT; Kin.Imm = KernelN;
                KernelN++;
                return Emit(Code, Cap, N, Kin);
            }
            /* Tight expanded: only pure 2-stmt shift body. body_n>2 (moves++)
             * falls through to generic while so every stmt is lowered. */
            if (ok_shape && body_n == 2 && LoopSp < 8 && IdxN < 30)
            {
                int rj, ra, rk, rc, rj1, br0, br1, head_pc, end_pc;
                IrExpr *IxLoad = NULL;
                /* Build synthetic Index expr for a[j] meta from Cmp */
                if (Cmp && Cmp->Left && Cmp->Left->Kind == IrExIndex)
                    IxLoad = Cmp->Left;
                PushLoop(1);
                head_pc = *N;
                /* rj = j */
                rj = AllocReg(Next);
                if (rj < 0) { LoopSp--; return 0; }
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)rj;
                In.A = In.B = 0; In.Tok = 0; In.Imm = jSlot;
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                /* if (!(j >= 0)) goto end  == BrLT j, 0 is wrong; use Imm 0 + Bin >= */
                {
                    int z = AllocReg(Next);
                    if (z < 0) { LoopSp--; return 0; }
                    In.Op = IrLinImm; In.Dest = (unsigned char)z;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                    rc = AllocReg(Next);
                    if (rc < 0) { LoopSp--; return 0; }
                    In.Op = IrLinBin; In.Dest = (unsigned char)rc;
                    In.A = (unsigned char)rj; In.B = (unsigned char)z;
                    In.Tok = TokenGreaterEqual; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                    br0 = *N;
                    In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)rc;
                    In.B = 0; In.Tok = 0; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                }
                /* ra = a[j]  (single load — CSE) */
                if (IxLoad == NULL || IdxN >= 32) { LoopSp--; return 0; }
                ra = AllocReg(Next);
                if (ra < 0) { LoopSp--; return 0; }
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)ra;
                In.A = (unsigned char)rj; In.B = 0; In.Tok = 0; In.Imm = IdxN;
                IdxMetaPush(IxLoad);
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                /* rk = key */
                rk = AllocReg(Next);
                if (rk < 0) { LoopSp--; return 0; }
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)rk;
                In.A = In.B = 0; In.Tok = 0; In.Imm = keySlot;
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                /* rc = (a[j] > key) or >= */
                rc = AllocReg(Next);
                if (rc < 0) { LoopSp--; return 0; }
                In.Op = IrLinBin; In.Dest = (unsigned char)rc;
                In.A = (unsigned char)ra; In.B = (unsigned char)rk;
                In.Tok = strict_gt ? TokenGreaterThan : TokenGreaterEqual;
                In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                br1 = *N;
                In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)rc;
                In.B = 0; In.Tok = 0; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                /* a[j+1] = ra */
                rj1 = AllocReg(Next);
                if (rj1 < 0) { LoopSp--; return 0; }
                In.Op = IrLinAddI; In.Dest = (unsigned char)rj1;
                In.A = (unsigned char)rj; In.B = 0; In.Tok = 0; In.Imm = 1;
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                if (IdxN >= 32) { LoopSp--; return 0; }
                In.Op = IrLinStoreIndex; In.Dest = 0;
                In.A = (unsigned char)ra; In.B = (unsigned char)rj1;
                In.Tok = 0; In.Imm = IdxN;
                IdxMetaPush(IxLoad); /* same array base meta */
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                /* j-- */
                {
                    int tmp = AllocReg(Next);
                    if (tmp < 0) { LoopSp--; return 0; }
                    In.Op = IrLinAddLocal; In.Dest = (unsigned char)tmp;
                    In.A = (unsigned char)jSlot; In.B = 0; In.Tok = 0; In.Imm = -1;
                    if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                }
                SealCont(Code, *N);
                In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = head_pc;
                if (!Emit(Code, Cap, N, In)) { LoopSp--; return 0; }
                end_pc = *N;
                Code[br0].Imm = end_pc;
                Code[br1].Imm = end_pc;
                SealBrk(Code, end_pc);
                return 1;
            }
        }
        /* Generic while lower. */
        if (LoopSp >= 8) return 0;
        {
            int tbr = EmitTreeLoopTry(S, IR_K_TREE_WHILE, Code, Cap, N, Next);
            if (tbr == -2) return 0;
            LinLoopWrites loop_writes;
            memset(&loop_writes, 0, sizeof(loop_writes));
            LinScanExprWrites(S->Expr, &loop_writes);
            LinScanStmtWrites(S->BodyStmt, &loop_writes);
            {
                unsigned char promoted[IR_CALL_MAX_LOCALS], prom_owned[IR_CALL_MAX_LOCALS];
                if (!LinPromPrepare(S, S->Expr, S->BodyStmt, NULL, Code, Cap, N, Next, promoted, prom_owned)) return 0;
                PushLoop(1);
                head = *N;
            LinCseLoopRegion++;
            LinCseInvalidateLoop(loop_writes.slot, loop_writes.memory, loop_writes.call);
            {
                br = -1;
                if (!LowerCondFalse(S->Expr, Code, Cap, N, Next, &br, 0))
                { LinCseLoopRegion--; LinPromFinish(promoted, prom_owned); return 0; }
            }
            LinCsePinLiveEntries();
            /* A condition load is safe to reuse in the body only when the
             * body cannot write memory or call unknown code. Local-only
             * writes are invalidated at their statement boundary, allowing
             * reuse until the actual mutation point. */
            /* Condition values for locals assigned by the body must not be
             * reused by the body; memory dependencies are invalidated too. */
            LinCseInvalidateLoopPreserveExpr(loop_writes.slot, loop_writes.memory || loop_writes.call,
                                         loop_writes.call);
            if (!LowerStmtRec(S->BodyStmt, Code, Cap, N, Next)) { LinCseLoopRegion--; LinPromFinish(promoted, prom_owned); return 0; }
            SealCont(Code, *N);
            /* Selectively invalidate at the backedge using the source write-set. */
            LinCseInvalidateLoop(loop_writes.slot, loop_writes.memory, loop_writes.call);
            In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = head;
            if (!Emit(Code, Cap, N, In)) { LinCseLoopRegion--; LinPromFinish(promoted, prom_owned); return 0; }
            LinCseLoopRegion--;
            end = *N;
            LinPromRewriteRegion(Code, head, end, promoted);
            CondPatch(Code, br, end);
            LinPromEmitSelectedSync(Code, Cap, N, promoted);
            if (tbr >= 0) Code[tbr].Imm = *N; /* kernel success already wrote the slots */
            SealBrk(Code, end);
            LinPromFinish(promoted, prom_owned);
            if (getenv("GEN_DUMP")) { int di; fprintf(stderr, "[gen] n=%d\n", *N); for (di = 0; di < *N && di < 30; di++) fprintf(stderr, "  %d: op=%d d=%d a=%d imm=%lld\n", di, Code[di].Op, Code[di].Dest, Code[di].A, (long long)Code[di].Imm); }
            return 1;
            }
        }
    }
    if (S->Kind == IrStFor)
    {
        int head, br, end, tpre = -1, tpost = -1;
        if (getenv("FOR_TRACE")) fprintf(stderr, "[for-entry] line=%d fastcond=%d fastinc=%d\n", S->SourceLine, S->FastCond.Active, S->FastInc.Active);
        if (getenv("FOR_TRACE") && S->SourceLine == 7) fprintf(stderr, "[for7] before kernel1\n");
        /* EXEC=vm: array-accum native kernel. */
        if (IrExecIsVmProd() && KernelN < 16 && IrKernelFpEnabledFor(S)
            && S->FastCond.Active && S->FastInc.Active
            && S->FastCond.VarName && S->FastInc.VarName
            && strcmp(S->FastCond.VarName, S->FastInc.VarName) == 0)
        {
            IrStmt *Body = S->BodyStmt;
            IrExpr *Add, *Dest, *IdxE = NULL;
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            int aSlot = -1, ok_shape = 0;
            if (Body && Body->Kind == IrStBlock && Body->StmtCount == 1)
                Body = Body->Stmts[0];
            if (Body && Body->Kind == IrStExpr && Body->Expr
                && Body->Expr->Kind == IrExAssign
                && Body->Expr->Left && Body->Expr->Right)
            {
                Dest = Body->Expr->Left;
                Add = Body->Expr->Right;
                if (Dest->Kind == IrExIdent && Add->Kind == IrExBinOp
                    && Add->Op == TokenPlus)
                {
                    if (Add->Left && Add->Left->Kind == IrExIndex
                        && Add->Right && Add->Right->Kind == IrExIdent
                        && Add->Right->LocalSlot == Dest->LocalSlot)
                        IdxE = Add->Left;
                    else if (Add->Right && Add->Right->Kind == IrExIndex
                             && Add->Left && Add->Left->Kind == IrExIdent
                             && Add->Left->LocalSlot == Dest->LocalSlot)
                        IdxE = Add->Right;
                    if (IdxE && IdxE->Left && IdxE->Left->Kind == IrExIdent
                        && IdxE->Right && IdxE->Right->Kind == IrExIdent
                        && IdxE->Right->Ident
                        && strcmp(IdxE->Right->Ident, S->FastInc.VarName) == 0)
                    {
                        aSlot = IdxE->Left->LocalSlot;
                        /* Param int* p[i]: ARRAY_ACCUM only knows LocalVal —
                         * expand LoadIndex (param path). Global/local OK. */
                        if (IdxE->Left->ParamSlot >= 0)
                            ok_shape = 0;
                        else if (aSlot >= 0 && aSlot < IR_CALL_MAX_LOCALS
                            && ((Fr->LocalArraySize[aSlot] > 0 && Fr->LocalVal[aSlot] != 0)
                                || (IrExecIsVmProd() && IdxE->Left->Ident != NULL)))
                            ok_shape = 1;
                        else if (aSlot < 0 && IdxE->Left->Ident != NULL
                                 && IdxE->Left->ParamSlot < 0)
                            ok_shape = 0; /* global: IrTryFastArrayAccum does not handle; fall to generic */
                    }
                }
            }
            if (ok_shape && !LinPromStmtTouchesActive(S))
            {
                IrLinInst Kin;
                if (S->InitStmt != NULL
                    && !LowerStmtRec(S->InitStmt, Code, Cap, N, Next))
                    ; /* fall through */
                else
                {
                    KernelStmtTab[KernelN] = S;
                    Kin.Op = IrLinKernel; Kin.Dest = Kin.A = Kin.B = 0;
                    Kin.Tok = IR_K_ARRAY_ACCUM; Kin.Imm = KernelN;
                    KernelN++;
                    if (Emit(Code, Cap, N, Kin))
                    { if (getenv("FOR_TRACE")) fprintf(stderr, "[accum] emit ok line=%d\n", S->SourceLine); return 1; }
                    KernelN--;
                }
            }
        }
        if (getenv("FOR_TRACE") && S->SourceLine == 7) fprintf(stderr, "[for7] after kernel1\n");
        /* EXEC=vm: array-fill native kernel. */
        if (IrExecIsVmProd() && !IrVmStrictOn() && KernelN < 16 && IrKernelFpEnabledFor(S)
            && S->FastCond.Active && S->FastInc.Active
            && S->FastCond.VarName && S->FastInc.VarName
            && strcmp(S->FastCond.VarName, S->FastInc.VarName) == 0)
        {
            IrStmt *Body = S->BodyStmt;
            IrExpr *Dest, *Rhs;
            int ok_shape = 0;
            if (Body && Body->Kind == IrStBlock && Body->StmtCount == 1)
                Body = Body->Stmts[0];
            if (Body && Body->Kind == IrStExpr && Body->Expr
                && Body->Expr->Kind == IrExAssign
                && Body->Expr->Left && Body->Expr->Right)
            {
                Dest = Body->Expr->Left;
                Rhs = Body->Expr->Right;
                if (Dest->Kind == IrExIndex && Dest->Left
                    && Dest->Left->Kind == IrExIdent
                    && Dest->Right && Dest->Right->Kind == IrExIdent
                    && Dest->Right->Ident
                    && strcmp(Dest->Right->Ident, S->FastInc.VarName) == 0
                    && (Rhs->Kind == IrExConst
                        || (Rhs->Kind == IrExIdent && Rhs->Ident
                            && strcmp(Rhs->Ident, S->FastInc.VarName) == 0)))
                    ok_shape = 1;
                /* Complex BinOp fills (isort a[i]=(i*k+c)%m) stay expanded LDX. */
            }
            if (ok_shape && !LinPromStmtTouchesActive(S))
            {
                IrLinInst Kin;
                if (S->InitStmt != NULL
                    && !LowerStmtRec(S->InitStmt, Code, Cap, N, Next))
                    ; /* fall through */
                else
                {
                    KernelStmtTab[KernelN] = S;
                    Kin.Op = IrLinKernel; Kin.Dest = Kin.A = Kin.B = 0;
                    Kin.Tok = IR_K_ARRAY_FILL; Kin.Imm = KernelN;
                    KernelN++;
                    if (Emit(Code, Cap, N, Kin))
                        return 1;
                    KernelN--;
                }
            }
        }
        /* EXEC=vm: ptr-walk for (p=head; p; p=p->next) s+=p->v → IrLinKernel.
         * Body must be single acc += p->field (not printf).
         * Gate on IrKernelFpEnabledFor(S): under BABYC_NO_KERNEL IrTryFastPtrWalk
         * returns 0 and the return was ignored → silent no-op (list_walk → 0).
         * Fall through to generic for lower when kernels are off. */
        if (IrExecIsVmProd() && KernelN < 16 && IrCallDepth > 0
            && IrKernelFpEnabledFor(S))
        {
            IrExpr *Cond = S->Expr;
            IrStmt *Body = S->BodyStmt;
            IrExpr *BE;
            int ok_shape = 0;
            if (Body && Body->Kind == IrStBlock && Body->StmtCount == 1)
                Body = Body->Stmts[0];
            BE = (Body && Body->Kind == IrStExpr) ? Body->Expr : NULL;
            if (Cond && Cond->Kind == IrExBinOp
                && Cond->Op == TokenNotEqual
                && Cond->Left && Cond->Left->Kind == IrExIdent
                && Cond->Left->LocalSlot >= 0
                && Cond->Right && Cond->Right->Kind == IrExConst
                && Cond->Right->ConstVal == 0
                && S->IncStmt != NULL
                && BE != NULL && BE->Kind == IrExAssign
                && (BE->Op == TokenAddAssign || BE->Op == 0 || BE->Op == TokenAssign)
                && BE->Left && BE->Left->Kind == IrExIdent
                && BE->Right != NULL
                && (BE->Right->Kind == IrExMember
                    || (BE->Right->Kind == IrExBinOp && BE->Right->Op == TokenPlus
                        && BE->Right->Right && BE->Right->Right->Kind == IrExMember)))
            {
                ok_shape = 1;
            }
            if (ok_shape && !LinPromStmtTouchesActive(S))
            {
                IrLinInst Kin;
                if (S->InitStmt != NULL
                    && !LowerStmtRec(S->InitStmt, Code, Cap, N, Next))
                    ; /* fall through */
                else
                {
                    KernelStmtTab[KernelN] = S;
                    Kin.Op = IrLinKernel; Kin.Dest = Kin.A = Kin.B = 0;
                    Kin.Tok = IR_K_PTR_WALK; Kin.Imm = KernelN;
                    KernelN++;
                    if (Emit(Code, Cap, N, Kin))
                        return 1;
                    KernelN--;
                }
            }
        }
        if (getenv("FOR_TRACE") && S->SourceLine == 7) fprintf(stderr, "[for7] before unroll\n");
        /* Small const-trip for-unroll (≤4): life neighbors dy/dx -1..1.
         * Prefer FastCond Bound when Active (reliable for compiled for). */
        if (IrExecIsVmProd()
            && S->FastCond.Active && S->FastInc.Active
            && S->FastCond.VarName && S->FastInc.VarName
            && strcmp(S->FastCond.VarName, S->FastInc.VarName) == 0
            && !S->FastCond.BoundIsVar
            && S->BodyStmt != NULL)
        {
            long long lo = 0, hi = S->FastCond.Bound;
            int slot = S->FastInc.VarSlot;
            int ok_fc = 0;
            if (slot < 0 && S->FastInc.VarName != NULL && IrCallDepth > 0)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                int li;
                for (li = 0; li < IR_CALL_MAX_LOCALS; li++)
                    if (Fr->LocalName[li] != NULL
                        && strcmp(Fr->LocalName[li], S->FastInc.VarName) == 0)
                    { slot = li; break; }
            }
            /* Init assign const → lo; Bound is exclusive for < and inclusive for <=.
             * FastCond stores Bound as the RHS of the comparison; Op in FastCond.Op. */
            if (S->InitStmt && S->InitStmt->Kind == IrStExpr && S->InitStmt->Expr
                && S->InitStmt->Expr->Kind == IrExAssign
                && S->InitStmt->Expr->Right)
            {
                IrExpr *Rv = S->InitStmt->Expr->Right;
                if (Rv->Kind == IrExConst)
                { lo = Rv->ConstVal; ok_fc = 1; }
                else if (Rv->Kind == IrExUnary && Rv->Op == TokenMinus
                         && Rv->Left && Rv->Left->Kind == IrExConst)
                { lo = -Rv->Left->ConstVal; ok_fc = 1; }
                else if (Rv->Kind == IrExBinOp && Rv->Op == TokenMinus
                         && Rv->Right && Rv->Right->Kind == IrExConst)
                {
                    /* -1 often lowered as (0-1) or (x-1) with x const 0 */
                    long long L = 0;
                    if (Rv->Left == NULL) L = 0;
                    else if (Rv->Left->Kind == IrExConst) L = Rv->Left->ConstVal;
                    else { L = 0; /* keep try */ }
                    if (Rv->Left == NULL || Rv->Left->Kind == IrExConst)
                    { lo = L - Rv->Right->ConstVal; ok_fc = 1; }
                }
            }
if (ok_fc)
            {
                if (S->FastCond.Op == TokenLessThan)
                    hi = S->FastCond.Bound - 1;
                else if (S->FastCond.Op == TokenLessEqual)
                    hi = S->FastCond.Bound;
                else if (S->FastCond.Op == TokenGreaterThan)
                { /* countdown not handled */ ok_fc = 0; }
                else
                    ok_fc = 0;
            }
            if (ok_fc && slot >= 0 && hi >= lo && (hi - lo) <= 3
                && IrConstUnrollStmtSafe(S->BodyStmt, 0))
            {
                /* Only leaf fors (no nested loop in body): neighbors inner dx. */
                {
                    IrStmt *st[32];
                    int sp = 0, nested = 0;
                    st[sp++] = S->BodyStmt;
                    while (sp > 0 && !nested)
                    {
                        IrStmt *B = st[--sp];
                        int si;
                        if (B == NULL) continue;
                        if (B->Kind == IrStFor || B->Kind == IrStWhile || B->Kind == IrStDo)
                        { nested = 1; break; }
                        if (B->Kind == IrStBlock)
                            for (si = 0; si < B->StmtCount && sp < 32; si++)
                                st[sp++] = B->Stmts[si];
                        if (B->ThenStmt && sp < 32) st[sp++] = B->ThenStmt;
                        if (B->ElseStmt && sp < 32) st[sp++] = B->ElseStmt;
                        if (B->BodyStmt && sp < 32) st[sp++] = B->BodyStmt;
                    }
                    if (nested)
                        goto after_const_unroll;
                }
                long long v;
                int ok_all = 1;
                int saved_n = *N;
                for (v = lo; v <= hi; v++)
                {
                    IrLinInst In;
                    int tmp, body_end;
                    if (LoopSp >= 8) { ok_all = 0; break; }
                    tmp = AllocReg(Next);
                    if (tmp < 0) { ok_all = 0; break; }
                    In.Op = IrLinImm; In.Dest = (unsigned char)tmp;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = v;
                    if (!Emit(Code, Cap, N, In)) { ok_all = 0; break; }
                    In.Op = IrLinStoreLocal; In.Dest = 0;
                    In.A = (unsigned char)tmp; In.B = 0; In.Tok = 0;
                    In.Imm = slot;
                    if (!Emit(Code, Cap, N, In)) { ok_all = 0; break; }
                    PushLoop(1);
                    if (!LowerStmtRec(S->BodyStmt, Code, Cap, N, Next))
                    {
                        /* SealBrk pops; if body failed before any seal, pop manually */
                        if (LoopSp > 0) LoopSp--;
                        ok_all = 0; break;
                    }
                    body_end = *N;
                    SealCont(Code, body_end);
                    SealBrk(Code, body_end); /* pops LoopSp */
                }
                if (ok_all)
                {
                    if (getenv("FOR_TRACE")) fprintf(stderr, "[unroll1] success line=%d\n", S->SourceLine);
                    return 1;
                }
                *N = saved_n;
            }
        }
        after_const_unroll: (void)0;
        if (getenv("FOR_TRACE") && S->SourceLine == 7) fprintf(stderr, "[for7] after unroll\n");
        if (IrExecIsVmProd()
            && S->InitStmt != NULL && S->Expr != NULL && S->IncStmt != NULL
            && S->BodyStmt != NULL)
        {
            IrStmt *Init = S->InitStmt;
            IrExpr *Cond = S->Expr;
            IrStmt *Inc = S->IncStmt;
            long long lo = 0, hi = 0;
            int slot = -1, ok_u = 0;
            if (Init->Kind == IrStExpr && Init->Expr
                && Init->Expr->Kind == IrExAssign
                && (Init->Expr->Op == 0 || Init->Expr->Op == TokenAssign)
                && Init->Expr->Left && Init->Expr->Left->Kind == IrExIdent
                && Init->Expr->Right)
            {
                IrExpr *Rv = Init->Expr->Right;
                long long cv = 0;
                int got = 0;
                if (Rv->Kind == IrExConst)
                { cv = Rv->ConstVal; got = 1; }
                else if (Rv->Kind == IrExUnary && Rv->Op == TokenMinus
                         && Rv->Left && Rv->Left->Kind == IrExConst)
                { cv = -Rv->Left->ConstVal; got = 1; }
                if (got)
                {
                    slot = Init->Expr->Left->LocalSlot;
                    lo = cv;
                    ok_u = 1;
                }
            }
            if (ok_u && Cond->Kind == IrExBinOp
                && Cond->Left && Cond->Left->Kind == IrExIdent
                && Cond->Right && Cond->Right->Kind == IrExConst)
            {
                /* slot may still be -1; bind from Cond */
                if (slot < 0)
                    slot = Cond->Left->LocalSlot;
                if (slot >= 0 && (Cond->Left->LocalSlot == slot || Cond->Left->LocalSlot < 0))
                {
                    if (Cond->Op == TokenLessEqual)
                        hi = Cond->Right->ConstVal;
                    else if (Cond->Op == TokenLessThan)
                        hi = Cond->Right->ConstVal - 1;
                    else
                        ok_u = 0;
                }
                else
                    ok_u = 0;
            }
            else
                ok_u = 0;
            if (ok_u)
            {
                IrExpr *Ie = (Inc->Kind == IrStExpr) ? Inc->Expr : NULL;
                /* LocalSlot may be unset on Init Ident; prefer Cond/Inc/FastInc. */
                if (slot < 0 && Cond->Left && Cond->Left->LocalSlot >= 0)
                    slot = Cond->Left->LocalSlot;
                if (slot < 0 && Ie && Ie->Left && Ie->Left->LocalSlot >= 0)
                    slot = Ie->Left->LocalSlot;
                if (slot < 0 && S->FastInc.Active && S->FastInc.VarSlot >= 0)
                    slot = S->FastInc.VarSlot;
                if (slot < 0
                    || !(Ie && Ie->Kind == IrExUnary
                         && Ie->Op == TokenIncrement
                         && Ie->Left && Ie->Left->Kind == IrExIdent
                         && (Ie->Left->LocalSlot == slot || Ie->Left->LocalSlot < 0)))
                    ok_u = 0;
            }
            if (getenv("BABYC_UNROLL_DEBUG") && ok_u)
                fprintf(stderr, "[unroll] candidate slot=%d lo=%lld hi=%lld\n", slot, lo, hi);
            else if (getenv("BABYC_UNROLL_DEBUG"))
            {
                IrExpr *Ie = (Inc && Inc->Kind == IrStExpr) ? Inc->Expr : NULL;
                fprintf(stderr, "[unroll] miss initK=%d condK=%d condOp=%d "
                    "incK=%d incOp=%d slot=%d cslot=%d\n",
                    Init && Init->Kind == IrStExpr,
                    Cond && Cond->Kind == IrExBinOp,
                    Cond && Cond->Kind == IrExBinOp ? (int)Cond->Op : -1,
                    Ie != NULL, Ie ? (int)Ie->Op : -1,
                    slot,
                    Cond && Cond->Left && Cond->Left->Kind == IrExIdent
                        ? Cond->Left->LocalSlot : -99);
            }
            if (ok_u && hi >= lo && (hi - lo) <= 3
                && IrConstUnrollStmtSafe(S->BodyStmt, 0)
                && BabycAccelOn() && !IrVmStrictOn())
            {
                long long v;
                int ok_all = 1;
                int saved_n = *N;
                for (v = lo; v <= hi; v++)
                {
                    IrLinInst In;
                    int tmp, body_end;
                    if (LoopSp >= 8) { ok_all = 0; break; }
                    tmp = AllocReg(Next);
                    if (tmp < 0) { ok_all = 0; break; }
                    In.Op = IrLinImm; In.Dest = (unsigned char)tmp;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = v;
                    if (!Emit(Code, Cap, N, In)) { ok_all = 0; break; }
                    In.Op = IrLinStoreLocal; In.Dest = 0;
                    In.A = (unsigned char)tmp; In.B = 0; In.Tok = 0;
                    In.Imm = slot;
                    if (!Emit(Code, Cap, N, In)) { ok_all = 0; break; }
                    PushLoop(1);
                    if (!LowerStmtRec(S->BodyStmt, Code, Cap, N, Next))
                    {
                        /* SealBrk pops; if body failed before any seal, pop manually */
                        if (LoopSp > 0) LoopSp--;
                        ok_all = 0; break;
                    }
                    body_end = *N;
                    SealCont(Code, body_end);
                    SealBrk(Code, body_end); /* pops LoopSp */
                }
                if (ok_all)
                    return 1;
                *N = saved_n; /* discard partial emit */
            }
        }


        if (getenv("FOR_TRACE") && S->SourceLine == 7) fprintf(stderr, "[for7] before KERNEL_FOR\n");
        /* EXEC=vm: KERNEL_FOR for scalar tight loops (fib-style).
         * IrKernelStmtOk rejects Index/Call/global — matches Body-IR gate. */
        /* EXEC=vm KERNEL_FOR: scalar-only body (no Index) + same cond/inc var.
         * isort/array stay on INSERT_SHIFT / ARRAY_ACCUM. Light path runs
         * IrTryKernelForLight so LocalVal vs LightLocal is correct. */
        if (IrExecIsVmProd() && KernelN < 16 && IrKernelFpEnabledFor(S)
            && S->FastCond.Active && S->FastInc.Active
            && S->FastCond.VarName && S->FastInc.VarName
            && strcmp(S->FastCond.VarName, S->FastInc.VarName) == 0)
        {
            IrStmt *Body = S->BodyStmt;
            int nested = 0;
            if (Body && Body->Kind == IrStBlock && Body->StmtCount == 1)
                Body = Body->Stmts[0];
            if (Body && (Body->Kind == IrStFor || Body->Kind == IrStWhile
                         || Body->Kind == IrStDo))
                nested = 1;
            if (!LinPromStmtTouchesActive(S) && !nested && Body != NULL && IrKernelStmtScalarOnly(Body, 0)
                && (S->InitStmt == NULL || IrKernelStmtScalarOnly(S->InitStmt, 0)))
            {
                IrLinInst Kin;
                int ok_init = 1;
                if (S->InitStmt != NULL
                    && !LowerStmtRec(S->InitStmt, Code, Cap, N, Next))
                    ok_init = 0;
                if (ok_init)
                {
                    KernelStmtTab[KernelN] = S;
                    Kin.Op = IrLinKernel;
                    Kin.Dest = 0;
                    Kin.A = 0;
                    Kin.B = 0;
                    Kin.Tok = IR_K_KERNEL_FOR;
                    Kin.Imm = KernelN;
                    /* A = paramIndex+1 when bound is a param (fib n). */
                    if (S->FastCond.BoundIsVar)
                    {
                        if (S->FastCond.BoundSlot >= 0
                            && S->FastCond.BoundSlot < IR_CALL_MAX_PARAMS
                            && S->FastCond.BoundSlot != S->FastInc.VarSlot)
                            Kin.A = (unsigned char)(S->FastCond.BoundSlot + 1);
                        else if (S->FastCond.BoundSlot < 0)
                            Kin.A = 1; /* param0 hint (fib n) — light only */
                    }
                    KernelN++;
                    if (Emit(Code, Cap, N, Kin))
                        return 1;
                    KernelN--;
                }
            }
        }
        /* Strided global-array fill: for (j = start; j <= b; j += st) g[j] = c.
         * Sieve inner loop. Unit-stride ARRAY_FILL misses j += i. */
        if (IrExecIsVmProd() && KernelN < 16 && IrKernelFpEnabledFor(S)
            && S->InitStmt && S->Expr && S->IncStmt && S->BodyStmt)
        {
            IrStmt *Bd = S->BodyStmt;
            IrStmt *Ic = S->IncStmt;
            IrExpr *Cn = S->Expr;
            IrExpr *As, *Idx, *Rhs;
            int jslot, match = 0;
            if (Bd->Kind == IrStBlock && Bd->StmtCount == 1) Bd = Bd->Stmts[0];
            if (Bd && Bd->Kind == IrStExpr && Bd->Expr
                && Bd->Expr->Kind == IrExAssign && Bd->Expr->Left && Bd->Expr->Right)
            {
                As = Bd->Expr;
                Idx = As->Left;
                Rhs = As->Right;
                if (Idx->Kind == IrExIndex && Idx->Left && Idx->Right
                    && Idx->Left->Kind == IrExIdent && Idx->Left->LocalSlot < 0
                    && Idx->Left->ParamSlot < 0 && Idx->Left->Ident
                    && Idx->Right->Kind == IrExIdent && Idx->Right->LocalSlot >= 0
                    && Rhs->Kind == IrExConst)
                {
                    jslot = Idx->Right->LocalSlot;
                    /* cond: j <= bound  or  j < bound */
                    if (Cn->Kind == IrExBinOp
                        && (Cn->Op == TokenLessEqual || Cn->Op == TokenLessThan)
                        && Cn->Left && Cn->Left->Kind == IrExIdent
                        && Cn->Left->LocalSlot == jslot
                        && Cn->Right
                        && (Cn->Right->Kind == IrExConst || Cn->Right->Kind == IrExIdent))
                    {
                        /* inc: j = j + stride  (stride Ident or Const) */
                        if (Ic->Kind == IrStExpr && Ic->Expr
                            && Ic->Expr->Kind == IrExAssign && Ic->Expr->Left
                            && Ic->Expr->Left->Kind == IrExIdent
                            && Ic->Expr->Left->LocalSlot == jslot
                            && Ic->Expr->Right && Ic->Expr->Right->Kind == IrExBinOp
                            && Ic->Expr->Right->Op == TokenPlus
                            && Ic->Expr->Right->Left && Ic->Expr->Right->Left->Kind == IrExIdent
                            && Ic->Expr->Right->Left->LocalSlot == jslot
                            && Ic->Expr->Right->Right
                            && (Ic->Expr->Right->Right->Kind == IrExIdent
                                || Ic->Expr->Right->Right->Kind == IrExConst))
                            match = 1;
                    }
                }
            }
            if (match && !LinPromStmtTouchesActive(S))
            {
                IrLinInst Kin;
                int saved_n = *N;
                if (LowerStmtRec(S->InitStmt, Code, Cap, N, Next))
                {
                    KernelStmtTab[KernelN] = S;
                    Kin.Op = IrLinKernel; Kin.Dest = Kin.A = Kin.B = 0;
                    Kin.Tok = IR_K_STRIDE_FILL; Kin.Imm = KernelN;
                    KernelN++;
                    if (Emit(Code, Cap, N, Kin))
                        return 1;
                    KernelN--;
                }
                *N = saved_n;
            }
        }
        /* Scan-accum: for (i=s; i<=b; i++) if (!g[i]) { c++; sum+=i; } */
        if (IrExecIsVmProd() && KernelN < 16 && IrKernelFpEnabledFor(S)
            && S->InitStmt && S->Expr && S->IncStmt && S->BodyStmt
            && S->FastInc.Active)
        {
            IrStmt *Bd = S->BodyStmt;
            IrExpr *Cn = S->Expr;
            int islot = -1, match = 0;
            if (Cn->Kind == IrExBinOp
                && (Cn->Op == TokenLessEqual || Cn->Op == TokenLessThan)
                && Cn->Left && Cn->Left->Kind == IrExIdent
                && Cn->Left->LocalSlot >= 0
                && Cn->Right && Cn->Right->Kind == IrExConst
                && S->FastInc.VarSlot == Cn->Left->LocalSlot)
            {
                islot = Cn->Left->LocalSlot;
                if (Bd->Kind == IrStBlock && Bd->StmtCount == 1 && Bd->Stmts)
                    Bd = Bd->Stmts[0];
                if (Bd && Bd->Kind == IrStIf && Bd->Expr && Bd->ThenStmt
                    && Bd->ElseStmt == NULL
                    && Bd->Expr->Kind == IrExUnary && Bd->Expr->Op == TokenUnaryNot
                    && Bd->Expr->Left && Bd->Expr->Left->Kind == IrExIndex
                    && Bd->Expr->Left->Left && Bd->Expr->Left->Left->Kind == IrExIdent
                    && Bd->Expr->Left->Left->LocalSlot < 0
                    && Bd->Expr->Left->Left->ParamSlot < 0
                    && Bd->Expr->Left->Right && Bd->Expr->Left->Right->Kind == IrExIdent
                    && Bd->Expr->Left->Right->LocalSlot == islot
                    && Bd->ThenStmt->Kind == IrStBlock
                    && Bd->ThenStmt->StmtCount == 2
                    && Bd->ThenStmt->Stmts)
                {
                    int saw_inc = 0, saw_add = 0, k;
                    for (k = 0; k < 2; k++)
                    {
                        IrStmt *St = Bd->ThenStmt->Stmts[k];
                        if (!St || St->Kind != IrStExpr || !St->Expr) continue;
                        if (St->Expr->Kind == IrExUnary && St->Expr->Op == TokenIncrement
                            && St->Expr->Left && St->Expr->Left->Kind == IrExIdent
                            && St->Expr->Left->LocalSlot >= 0)
                            saw_inc = 1;
                        else if (St->Expr->Kind == IrExAssign
                            && St->Expr->Left && St->Expr->Left->Kind == IrExIdent
                            && St->Expr->Left->LocalSlot >= 0
                            && St->Expr->Right && St->Expr->Right->Kind == IrExBinOp
                            && St->Expr->Right->Op == TokenPlus
                            && St->Expr->Right->Left && St->Expr->Right->Left->Kind == IrExIdent
                            && St->Expr->Right->Left->LocalSlot == St->Expr->Left->LocalSlot
                            && St->Expr->Right->Right && St->Expr->Right->Right->Kind == IrExIdent
                            && St->Expr->Right->Right->LocalSlot == islot)
                            saw_add = 1;
                    }
                    if (saw_inc && saw_add) match = 1;
                }
            }
            if (match && !LinPromStmtTouchesActive(S))
            {
                IrLinInst Kin;
                int saved_n = *N;
                if (LowerStmtRec(S->InitStmt, Code, Cap, N, Next))
                {
                    KernelStmtTab[KernelN] = S;
                    Kin.Op = IrLinKernel; Kin.Dest = Kin.A = Kin.B = 0;
                    Kin.Tok = IR_K_SCAN_ACCUM; Kin.Imm = KernelN;
                    KernelN++;
                    if (Emit(Code, Cap, N, Kin))
                        return 1;
                    KernelN--;
                }
                *N = saved_n;
            }
        }
        if (getenv("FOR_TRACE") && S->SourceLine == 7) fprintf(stderr, "[for7] before generic\n");
        if (getenv("CSE_DUMP") && S->SourceLine == 6) { int di; fprintf(stderr, "[cse-dump] n=%d\n", *N); for (di = 0; di < *N && di < 40; di++) fprintf(stderr, "  %d: op=%d d=%d a=%d imm=%lld\n", di, Code[di].Op, Code[di].Dest, Code[di].A, (long long)Code[di].Imm); }
        /* FastCond/Inc ok: lower Init/Expr/Body/Inc (F6 shadow/vm). */
        if (getenv("FOR_TRACE")) fprintf(stderr, "[for] generic entry line=%d\n", S->SourceLine);
        if (LoopSp >= 8) return 0;
        tpre = EmitTreeLoopTry(S, IR_K_TREE_FOR_PRE, Code, Cap, N, Next);
        if (tpre == -2) return 0;
        if (!LowerStmtRec(S->InitStmt, Code, Cap, N, Next)) { if (getenv("FOR_TRACE")) fprintf(stderr, "[for] init fail\n"); return 0; }
        tpost = EmitTreeLoopTry(S, IR_K_TREE_FOR, Code, Cap, N, Next);
        if (tpost == -2) return 0;
        unsigned char promoted[IR_CALL_MAX_LOCALS], prom_owned[IR_CALL_MAX_LOCALS];
        LinLoopWrites loop_writes;
        memset(&loop_writes, 0, sizeof(loop_writes));
        LinScanExprWrites(S->Expr, &loop_writes);
        LinScanStmtWrites(S->BodyStmt, &loop_writes);
        LinScanStmtWrites(S->IncStmt, &loop_writes);
        if (!LinPromPrepare(S, S->Expr, S->BodyStmt, S->IncStmt, Code, Cap, N, Next, promoted, prom_owned)) return 0;
        PushLoop(1);
        head = *N;
        LinCseLoopRegion++;
        LinCseInvalidateLoop(loop_writes.slot, loop_writes.memory, loop_writes.call);
        if (S->Expr == NULL) {
            br = -1;
        }
        else
        {
            {
                br = -1;
                if (!LowerCondFalse(S->Expr, Code, Cap, N, Next, &br, 0))
                { if (getenv("FOR_TRACE")) fprintf(stderr, "[for] cond fail\n"); LinCseLoopRegion--; LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
            }
            LinCsePinLiveEntries();
            /* Condition values for locals assigned by the body must not be
             * reused by the body; memory dependencies are invalidated too. */
            LinCseInvalidateLoop(loop_writes.slot, loop_writes.memory || loop_writes.call,
                                 loop_writes.call);
        }
        if (!LowerStmtRec(S->BodyStmt, Code, Cap, N, Next))
        { if (getenv("FOR_TRACE")) fprintf(stderr, "[for] body fail\n"); LinCseLoopRegion--; LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
        SealCont(Code, *N);
        if (!LowerStmtRec(S->IncStmt, Code, Cap, N, Next))
        { LinCseLoopRegion--; LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
        LinCseInvalidateLoop(loop_writes.slot, loop_writes.memory, loop_writes.call);
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = head;
        if (!Emit(Code, Cap, N, In))
        { LinCseLoopRegion--; LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
        LinCseLoopRegion--;
        end = *N;
        LinPromRewriteRegion(Code, head, end, promoted);
        if (br >= 0) CondPatch(Code, br, end);
        LinPromEmitSelectedSync(Code, Cap, N, promoted);
        if (tpre >= 0) Code[tpre].Imm = *N; /* fast path has already committed locals */
        if (tpost >= 0) Code[tpost].Imm = *N;
        SealBrk(Code, end);
        LinPromFinish(promoted, prom_owned);
        if (getenv("FOR_TRACE")) fprintf(stderr, "[for] success line=%d\n", S->SourceLine);
        return 1;
    }
    if (S->Kind == IrStDo && S->Expr != NULL)
    {
        int head, br;
        if (IrExecIsVmProd() && KernelN < 16 && !LinPromStmtTouchesActive(S)
            && S->FastCond.Active && S->FastCond.Op == TokenGreaterThan)
        {
            IrStmt *Body = S->BodyStmt;
            if (Body && Body->Kind == IrStBlock && Body->StmtCount == 2)
            {
                IrLinInst Kin;
                KernelStmtTab[KernelN] = S;
                Kin.Op = IrLinKernel; Kin.Dest = Kin.A = Kin.B = 0;
                Kin.Tok = IR_K_DO_COUNTDOWN; Kin.Imm = KernelN;
                KernelN++;
                if (Emit(Code, Cap, N, Kin))
                    return 1;
                KernelN--;
            }
        }
        if (LoopSp >= 8) return 0;
        {
        unsigned char promoted[IR_CALL_MAX_LOCALS], prom_owned[IR_CALL_MAX_LOCALS];
        if (!LinPromPrepare(S, S->Expr, S->BodyStmt, NULL, Code, Cap, N, Next, promoted, prom_owned)) return 0;
        PushLoop(1);
        head = *N;
        if (!LowerStmtRec(S->BodyStmt, Code, Cap, N, Next))
        { LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
        SealCont(Code, *N);
        { extern void LinCseReset(void); LinCseReset(); }
        {
            br = -1;
            if (!LowerCondFalse(S->Expr, Code, Cap, N, Next, &br, 0))
            { LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
        }
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = head;
        if (!Emit(Code, Cap, N, In)) { LoopSp--; LinPromFinish(promoted, prom_owned); return 0; }
        { int end = *N; LinPromRewriteRegion(Code, head, end, promoted); CondPatch(Code, br, end);
          LinPromEmitSelectedSync(Code, Cap, N, promoted);
          SealBrk(Code, end); LinPromFinish(promoted, prom_owned); }
        }
        if (getenv("CSE_DUMP") && S->SourceLine == 5) { int di; fprintf(stderr, "[cse-dump] n=%d\n", *N); for (di = 0; di < *N && di < 40; di++) fprintf(stderr, "  %d: op=%d d=%d a=%d imm=%lld\n", di, Code[di].Op, Code[di].Dest, Code[di].A, (long long)Code[di].Imm); }
        return 1;
    }
    if (S->Kind == IrStBreak)
        return EmitBreak(Code, Cap, N);
    if (S->Kind == IrStContinue)
        return EmitCont(Code, Cap, N);
    if (S->Kind == IrStSwitch && S->Expr != NULL && S->BodyStmt != NULL
        && S->BodyStmt->Kind == IrStBlock)
    {
        int sel, ci, ncase, defpc, endjmp;
        int casePc[16], caseVal[16], caseAt[16];
        IrStmt *B = S->BodyStmt;
        if (LoopSp >= 8) return 0;
        ncase = 0;
        defpc = -1;
        for (ci = 0; ci < B->StmtCount; ci++)
        {
            IrStmt *T = B->Stmts[ci];
            if (T == NULL) continue;
            if (T->Kind == IrStCase)
            {
                if (ncase >= 16) return 0;
                if (T->Expr == NULL) return 0;
                caseVal[ncase] = ci; /* index into body, expr lowered later */
                caseAt[ncase] = ci;
                ncase++;
            }
            else if (T->Kind == IrStDefault)
                defpc = ci;
        }
        if (!LowerRec(S->Expr, Code, Cap, N, Next, &sel)) return 0;
        LinCsePinLiveEntries();
        for (ci = 0; ci < ncase; ci++)
        {
            int one, eq, brz;
            if (!LowerRec(B->Stmts[caseAt[ci]]->Expr, Code, Cap, N, Next, &one))
                return 0;
            eq = AllocReg(Next);
            if (eq < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)eq;
            In.A = (unsigned char)sel; In.B = (unsigned char)one;
            In.Tok = TokenEqual; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            brz = *N;
            In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)eq; In.B = 0;
            In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            casePc[ci] = *N;
            In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            Code[brz].Imm = *N;
            (void)casePc;
        }
        endjmp = *N;
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        PushLoop(0);
        {
            int k, nx = 0;
            int marks[17];
            for (k = 0; k < ncase; k++) marks[k] = -1;
            for (ci = 0; ci < B->StmtCount; ci++)
            {
                IrStmt *T = B->Stmts[ci];
                if (T == NULL) continue;
                if (T->Kind == IrStCase)
                {
                    for (k = 0; k < ncase; k++)
                        if (caseAt[k] == ci) Code[casePc[k]].Imm = *N;
                    continue;
                }
                if (T->Kind == IrStDefault)
                {
                    Code[endjmp].Imm = *N;
                    defpc = *N;
                    continue;
                }
                if (!LowerStmtRec(T, Code, Cap, N, Next)) return 0;
            }
            if (Code[endjmp].Imm == 0)
                Code[endjmp].Imm = *N;
            (void)nx; (void)marks; (void)defpc;
        }
        SealBrk(Code, *N);
        return 1;
    }
    if (S->Kind == IrStReturn)
    {
        if (S->Expr == NULL)
        {
            LinPromSyncAll(Code, Cap, N);
            /* void return must still terminate the linear body. */
            In.Op = IrLinRet; In.Dest = 0;
            In.A = 0; In.B = 0; In.Tok = 0; In.Imm = 0;
            return Emit(Code, Cap, N, In);
        }
        /* Unlowerable return expr must refuse the whole body. Returning 1
         * left an eligible linear body without IrLinRet; under vm_strict
         * (no tiny-body tree skip) that body ran and yielded 0
         * (503_bitfield_fn_inc: p->a++ not linearised). */
        if (!LowerRec(S->Expr, Code, Cap, N, Next, &dest))
            return 0;
        LinPromSyncAll(Code, Cap, N);
        In.Op = IrLinRet; In.Dest = 0;
        In.A = (unsigned char)dest; In.B = 0; In.Tok = 0; In.Imm = 0;
        /* Aggregate return (S->DeclFromType = declared return type): dest is
         * the address of the returned object (any lvalue form or a nested
         * result). Imm carries the exact size so the executor copies the
         * value out before the callee frame is freed. */
        if (S->DeclFromType != NULL && IrTypeIsCopyableAggregate(S->DeclFromType))
            In.Imm = S->DeclFromType->Sizeof;
        return Emit(Code, Cap, N, In);
    }
    return 0;
}

int IrLowerStmt(IrStmt *S, IrLinInst *Code, int Cap, int *NOut)
{
    int next = 0, n = 0;
    int nested = (LowerDepth > 0);
    int pushed = 0;
    /* Nested lower (e.g. FnCodeCompile while parent tables live): isolate. */
    if (nested)
        pushed = CompileFramePush();
    LowerDepth++;
    LoopSp = 0;
    if (!nested) {
        int pi; for (pi = 0; pi < IR_CALL_MAX_LOCALS; ++pi) { LinPromReg[pi] = -1; LinPromRefs[pi] = 0; LinPromType[pi] = 0; }
        memset(LinAddrTaken, 0, sizeof(LinAddrTaken));
    }
    NLab = 0;
    memset(CallSkip, 0, sizeof(CallSkip));
    GlobN = 0;
    IdxN = 0;
    IdxDirty = 0;
    GlobSaved = 0;
    GlobDirty = 0;
    /* LINE opcodes emitted per-stmt inside LowerStmtRec when BABYC_DEBUG. */
    if (!LowerStmtRec(S, Code, Cap, &n, &next))
    {
        LowerDepth--;
        if (pushed)
            CompileFramePop();
        return 0;
    }
    {
        int li;
        for (li = 0; li < NLab; li++)
            if (Labs[li].Pc < 0)
            {
                LowerDepth--;
                if (pushed)
                    CompileFramePop();
                return 0;
            }
    }
    if (LoopSp != 0)
    {
        LowerDepth--;
        if (pushed)
            CompileFramePop();
        return 0;
    }
    *NOut = n;
    LowerDepth--;
    /* Nested: restore parent. Top-level: leave tables for subsequent Step. */
    if (pushed)
        CompileFramePop();
    return 1;
}


/* Prove that LightRunBody can execute this function in an isolated per-depth
 * scalar window even when an ordinary IrCallFrame is active in the caller.
 * Reject local-address formation and all memory/kernel operations: those are
 * the operations that could accidentally consult or mutate the parent frame. */
static int FnCodeLightFrameSafe(const FnCodeSlot *S)
{
    int i;
    if (S == NULL || S->N <= 0 || S->N > 128 || S->MaxLocal > LIGHT_LOCALS
        || S->HasKernel || S->HasArrayKernel || S->IdxN > 0 || S->Func == NULL)
        return 0;
    for (i = 0; i < IR_CALL_MAX_LOCALS; ++i)
        if (S->SlotIsPtr[i] || S->SlotIsFptr[i])
            return 0;
    for (i = 0; i < S->N; ++i)
    {
        const IrLinInst *In = &S->Code[i];
        switch (In->Op)
        {
        case IrLinImm:
            /* Tok=4 materializes a frame-local address. */
            if (In->Tok == 4) return 0;
            break;
        case IrLinLoadLocal: case IrLinStoreLocal:
        case IrLinJmp: case IrLinBrZ: case IrLinBrCmp: case IrLinRet:
        case IrLinLine: case IrLinAddI: case IrLinAddLocal:
        case IrLinAddStoreLocal:
            break;
        case IrLinUnary:
            if (!(In->Tok == TokenMinus || In->Tok == TokenUnaryNot
                  || In->Tok == TokenUnaryExor
                  || (In->Tok == TokenCast && (In->Imm == 1 || In->Imm == 2))))
                return 0; /* LightRunBody must implement the exact conversion. */
            break;
        case IrLinBin:
            if (In->Imm & (IR_LIN_F_FP | IR_LIN_F_WIDE)) return 0;
            if ((In->Imm & IR_LIN_F_UNSIGNED)
                && (In->Tok == TokenSlash || In->Tok == TokenModulus
                    || In->Tok == TokenLessThan || In->Tok == TokenGreaterThan
                    || In->Tok == TokenLessEqual || In->Tok == TokenGreaterEqual))
                return 0; /* LightRunBody's unsigned comparison/division is incomplete. */
            switch (In->Tok)
            {
            case TokenPlus: case TokenMinus: case TokenAsterisk:
            case TokenSlash: case TokenModulus: case TokenEqual:
            case TokenNotEqual: case TokenLessThan: case TokenGreaterThan:
            case TokenLessEqual: case TokenGreaterEqual: case TokenAmpersand:
            case TokenArithmeticOr: case TokenArithmeticExor:
            case TokenShiftLeft: case TokenShiftRight:
                break;
            default: return 0;
            }
            break;
        case IrLinLoadGlobal: case IrLinStoreGlobal: case IrLinIncG:
        {
            int g = (int)In->Imm;
            struct Value *V;
            enum BaseType b;
            if (g < 0 || g >= S->GlobN || (V = S->GlobVal[g]) == NULL
                || V->Typ == NULL || V->Val == NULL) return 0;
            b = V->Typ->Base;
            if (!(b == TypeChar || b == TypeUnsignedChar || b == TypeShort
                  || b == TypeUnsignedShort || b == TypeInt || b == TypeUnsignedInt
                  || b == TypeLong || b == TypeUnsignedLong || b == TypeLongLong
                  || b == TypeUnsignedLongLong)) return 0;
            break;
        }
        case IrLinCall:
        {
            int ci = (int)In->Imm;
            if (ci < 0 || ci >= S->CallN || S->CallFnTab[ci] != S->Func
                || S->CallIndirectSlot[ci] >= 0
                || S->CallIndirectReg[ci] >= 0
                || S->CallIndirectParam[ci] >= 0
                || S->CallIndirectExpr[ci] != NULL)
                return 0;
            break;
        }
        default:
            return 0;
        }
    }
    return 1;
}

static const char *LinFuncNameFromValue(struct ParseState *Parser, struct Value *Func)
{
    int b;
    struct Table *T;
    if (Parser == NULL || Parser->pc == NULL || Func == NULL) return NULL;
    T = &Parser->pc->GlobalTable;
    if (T->HashTable == NULL || T->Size <= 0) return NULL;
    for (b = 0; b < T->Size; ++b) {
        struct TableEntry *E;
        for (E = T->HashTable[b]; E != NULL; E = E->Next)
            if (E->p.v.Val == Func && E->p.v.Key != NULL && E->p.v.Key[0] != '\0')
                return E->p.v.Key;
    }
    return NULL;
}

FnCodeSlot *FnCodeCompile(struct ParseState *Parser, IrStmt *Body,
                                   struct Value *Func)
{
    FnCodeSlot *S;
    int n = 0, ok, pushed;
    S = FnCodeFind(Body);
    if (S != NULL)
    {
        if (S->Elig == 0) return NULL;
        if (S->N > 0) return S;
    }
    else
    {
        if (FnSlotN >= FNCODE_SLOTS)
            return NULL; /* cache full → tree */
        S = &FnSlots[FnSlotN++];
        S->Body = Body;
        S->N = 0;
        S->Elig = -1;
        S->MonoElig = -1;
        S->MemoElig = -1;
        S->PtrIndexedElig = -1;
        S->Func = Func;
    }
    if (Func != NULL && S->Func == NULL)
        S->Func = Func;
    if (S->Elig < 0)
        S->Elig = IrFnBodyIntegerEligible(Body) ? 1 : 0;
    if (S->Elig != 1)
    {
        return NULL;
    }

    /* Isolate only when parent has live tables / wide / depth. */
    pushed = 0;
    if (FnActive != NULL || CallN > 0 || GlobN > 0 || IdxN > 0
        || LinExecDepth > 0 || LowerDepth > 0 || LinWideMax > 0
        || CompileFrameSp > 0)
        pushed = CompileFramePush();
    LinParser = Parser;
    CompileSlot = S;
    memset(S->SlotIsPtr, 0, sizeof(S->SlotIsPtr));
    memset(S->SlotIsFptr, 0, sizeof(S->SlotIsFptr));
    memset(S->SlotFromType, 0, sizeof(S->SlotFromType));
    memset((void *)S->SlotName, 0, sizeof(S->SlotName)); /* discard const on ptr elems */
    CallN = 0;
    {
        int zi;
        for (zi = 0; zi < IR_LIN_CALL_SLOTS; zi++)
        {
            CallIndirectSlot[zi] = -1;
            CallIndirectReg[zi] = -1;
            CallIndirectParam[zi] = -1;
            CallIndirectExpr[zi] = NULL;
        }
    }
    KernelN = 0;
    memset(CompLocalNDims, 0, sizeof(CompLocalNDims));
    memset(CompLocalDims, 0, sizeof(CompLocalDims));
    memset(CompLocalElemIsPtr, 0, sizeof CompLocalElemIsPtr);
    memset(CompLocalIsConst, 0, sizeof CompLocalIsConst);
    memset(CompLocalLeafEs, 0, sizeof(CompLocalLeafEs));
    memset(CompLocalPointeeSize, 0, sizeof(CompLocalPointeeSize));
    memset(CompLocalFromType, 0, sizeof(CompLocalFromType));
    LinSkippedCall = 0;
    LinRetSeen = 0;
    LinStepAbort = 0;
    LinDiv0 = 0;
    GlobN = 0;
    IdxN = 0;
    memset(GlobVal, 0, sizeof(GlobVal)); memset(GlobRaw, 0, sizeof(GlobRaw));
    memset(LinWideOn, 0, sizeof(LinWideOn));
    LinWideMax = 0;
    memset(CallSkip, 0, sizeof(CallSkip));
    /* LoopSp/NLab reset inside IrLowerStmt */
    LinCompileActive = 1;
    /* Callee compile must not overwrite existing LocalName on the live frame
     * (both-mode: light-call to f() stamped f's names over main's k,r).
     * New stamps (NULL→name) are kept — loop-body compile of the current
     * function needs them (148/149 BoundIsVar).
     *
     * LocalInit: same class of bug as 1035 — body-local slot indices collide
     * with the caller. IrLiveMarkInit no-ops under LinCompileActive; this
     * snapshot/restore is the hard guard against any lower path that still
     * writes Fr->LocalInit[slot] directly. Cost: only at FnCodeCompile, never
     * on the IrExecLinLocals hot path. */
    {
        char *saved_names[IR_CALL_MAX_LOCALS];
        char saved_init[IR_CALL_MAX_LOCALS];
        int si;
        int have = (IrCallDepth > 0 && IrCallStack[IrCallDepth - 1] != NULL);
        IrCallFrame *Fr = have ? IrCallStack[IrCallDepth - 1] : NULL;
        int init_poison = 0;
        if (have)
        {
            for (si = 0; si < IR_CALL_MAX_LOCALS; si++)
            {
                saved_names[si] = Fr->LocalName[si];
                saved_init[si] = Fr->LocalInit[si];
            }
        }
        ok = IrLowerStmt(Body, S->Code, IR_LIN_STMT_MAX, &n);
        if (have)
        {
            for (si = 0; si < IR_CALL_MAX_LOCALS; si++)
            {
                if (saved_names[si] != NULL)
                    Fr->LocalName[si] = saved_names[si];
                if (Fr->LocalInit[si] != saved_init[si])
                {
                    init_poison = 1;
                    Fr->LocalInit[si] = saved_init[si];
                }
            }
            if (init_poison)
            {
                /* Always restore (above). Report under guard env:
                 * BABYC_LIVE_INIT_GUARD=1 → stderr once; =2 → abort. */
                const char *g = getenv("BABYC_LIVE_INIT_GUARD");
                if (g != NULL && g[0] != '0')
                {
                    fprintf(stderr,
                        "[babyc-live-init-guard] FnCodeCompile mutated caller "
                        "LocalInit (restored); fix path to use IrLiveMarkInit only\n");
                    if (g[0] == '2')
                        abort();
                }
            }
        }
    }
    LinCompileActive = 0;
    /* Sparse body: lower returned success but emitted almost nothing while
     * the source had many stmts (ptr/switch skipped as no-op return 1). */
    if (ok && n > 0 && n < 3 && !LinSkippedCall)
    {
        /* allow tiny pure helpers (return x;); refuse empty-ish larger bodies */
        ;
    }
    /* Empty body (ok, n==0, no skipped call) is a valid linear no-op.
     * Reject only real failures and bodies with an unlowerable call. */
    if (!ok || LinSkippedCall || n < 0)
    {
        if (IrVmProfileOn())
            fprintf(stderr, "[babyc-exec-profile] compile_reject=lower body_kind=%d n=%d ok=%d skipped_call=%d calls=%d\n",
                    Body ? (int)Body->Kind : -1, n, ok, LinSkippedCall, CallN);
        S->Elig = 0;
        S->N = 0;
        if (pushed)
            CompileFramePop();
        return NULL;
    }
    if (n == 0)
    {
        /* Legitimate empty function: accept as linear no-op. */
        S->Elig = 1;
        S->N = 0;
        S->MaxLocal = 0;
        S->MetaLocalN = 0;
        S->HasKernel = 0;
        S->HasArrayKernel = 0;
        if (pushed)
            CompileFramePop();
        IrVmProdNotice();
        return S;
    }
    /* Cap body size / call fan-out: large blocks residual SEGV under FnCode. */
    if (n > IR_LIN_STMT_MAX - 64 || CallN > 255)
    {
        if (IrVmProfileOn())
            fprintf(stderr, "[babyc-exec-profile] compile_reject=cap body_kind=%d n=%d calls=%d\n",
                    Body ? (int)Body->Kind : -1, n, CallN);
        S->Elig = 0;
        S->N = 0;
        if (pushed)
            CompileFramePop();
        return NULL;
    }
    n = IrLinPeephole(S->Code, n);
    S->N = n;
    /* Register file is sized per compiled function. Scan all operand fields
       conservatively: unused operand bytes are zero-initialized by emitters. */
    {
        int ri, maxreg = 0;
        for (ri = 0; ri < n; ++ri) {
            const IrLinInst *x = &S->Code[ri];
            if (x->Dest < IR_LIN_REGS && (int)x->Dest + 1 > maxreg) maxreg = (int)x->Dest + 1;
            if (x->A < IR_LIN_REGS && (int)x->A + 1 > maxreg) maxreg = (int)x->A + 1;
            if (x->B < IR_LIN_REGS && (int)x->B + 1 > maxreg) maxreg = (int)x->B + 1;
        }
        for (ri = 0; ri < CallN; ++ri) {
            int ai;
            if (CallIndirectReg[ri] >= 0 && CallIndirectReg[ri] < IR_LIN_REGS
                && CallIndirectReg[ri] + 1 > maxreg) maxreg = CallIndirectReg[ri] + 1;
            for (ai = 0; ai < CallArgN[ri] && ai < IR_LIN_CALL_ARGS; ++ai)
                if (CallArgReg[ri][ai] >= 0 && CallArgReg[ri][ai] < IR_LIN_REGS
                    && CallArgReg[ri][ai] + 1 > maxreg) maxreg = CallArgReg[ri][ai] + 1;
        }
        S->RegCount = (unsigned char)(maxreg > 0 ? maxreg : 1);
    }
    /* Permanent IR diagnostic. Set BABYC_DUMP_LIN=1 to dump all compiled
     * functions and their post-peephole linear instructions to stderr. */
    if (getenv("BABYC_DUMP_LIN") != NULL && getenv("BABYC_DUMP_LIN")[0] != '0')
    {
        int di;
        const char *fn_name = LinFuncNameFromValue(Parser, S->Func);
        fprintf(stderr, "[lin-ir] function=%s function_ptr=%p instructions=%d\n",
                fn_name != NULL ? fn_name : "<unknown>", (void *)S->Func, n);
        for (di = 0; di < n; di++)
        {
            const IrLinInst *x = &S->Code[di];
            fprintf(stderr, "[lin-ir] %04d op=%d dest=%u a=%u b=%u tok=%u imm=%lld\n",
                    di, (int)x->Op, (unsigned)x->Dest, (unsigned)x->A,
                    (unsigned)x->B, (unsigned)x->Tok, (long long)x->Imm);
        }
    }
    {
        int k, mx = 0;
        for (k = 0; k < n; k++)
        {
            IrLinInst *In = &S->Code[k];
            if ((In->Op == IrLinLoadLocal || In->Op == IrLinStoreLocal)
                && In->Imm >= 0 && In->Imm + 1 > mx)
                mx = (int)In->Imm + 1;
            if (In->Op == IrLinAddStoreLocal && (int)In->A + 1 > mx)
                mx = (int)In->A + 1;
        }
        S->MaxLocal = mx;
        /* Storage metadata can exist for address-taken locals that are not
         * represented by LoadLocal/StoreLocal. Compute its exact high-water
         * mark once here so recursive calls do not scan every possible slot. */
        {
            int si;
            S->MetaLocalN = 0;
            for (si = 0; si < IR_CALL_MAX_LOCALS; ++si)
                if (S->SlotIsPtr[si] || S->SlotIsFptr[si]
                    || S->SlotFromType[si] != NULL)
                    S->MetaLocalN = si + 1;
        }
        {
            int hi; S->HasKernel = 0; S->HasArrayKernel = 0;
            for (hi = 0; hi < S->N; hi++)
            {
                if (S->Code[hi].Op == IrLinKernel)
                {
                    S->HasKernel = 1;
                    if (S->Code[hi].Tok == IR_K_ARRAY_ACCUM
                        || S->Code[hi].Tok == IR_K_ARRAY_FILL
                        || S->Code[hi].Tok == IR_K_DECL_ARRAY
                        || S->Code[hi].Tok == IR_K_DECL_STRUCT
                        || S->Code[hi].Tok == IR_K_INSERT_SHIFT
                        || S->Code[hi].Tok == IR_K_DECL_STATIC
                        || S->Code[hi].Tok == IR_K_DECL_STATIC_ARR
                        || S->Code[hi].Tok == IR_K_DECL_STATIC_STU
                        || S->Code[hi].Tok == IR_K_TREE_WHILE
                        || S->Code[hi].Tok == IR_K_TREE_FOR_PRE
                        || S->Code[hi].Tok == IR_K_TREE_FOR)
                        S->HasArrayKernel = 1;
                }
            }
        }
    }
    if (S->Func == NULL && CallN > 0)
    {
        int ci, self = 1;
        struct Value *CF0 = CallFnTab[0];
        for (ci = 1; ci < CallN; ci++)
            if (CallFnTab[ci] != CF0) { self = 0; break; }
        /* Only self-recursion: same FV and BodyIr matches this body (not host). */
        if (self && CF0 != NULL && CF0->Val != NULL
            && CF0->Val->FuncDef.Intrinsic == NULL
            && (IrStmt *)CF0->Val->FuncDef.BodyIr == Body)
            S->Func = CF0;
    }
    FnCodeSnapshot(S);
    S->LightFrameSafe = (unsigned char)FnCodeLightFrameSafe(S);

    /* Mono/memo eligibility once per slot (purity walk is expensive). */
    if (S->Func != NULL)
    {
        int np = S->Func->Val && S->Func->Val->FuncDef.NumParams > 0
            ? S->Func->Val->FuncDef.NumParams : 1;
        if (S->MonoElig < 0)
            S->MonoElig = (!IrFuncUsesFP(S->Func) && IrMonoIsEligible(Body, np)) ? 1 : 0;
        if (S->MemoElig < 0)
            S->MemoElig = (!IrFuncUsesFP(S->Func) && IrMemoEligible(S->Func, np)) ? 1 : 0;
    }
    else
    {
        S->MonoElig = 0;
        S->MemoElig = 0;
    }
    if (pushed)
        CompileFramePop();
    CompileSlot = NULL;
    IrVmProdNotice();
    return S;
}

/* integer eligibility extract */

/* Shared host list for eligibility and emit. Keep in sync with Intrinsic
 * surface that linear can pack and call via IrCallHostFromRegs. */
/* <math.h> / <ctype.h> / atof: pure host functions whose double or int
 * result comes back as IR_RET_BITS from IrCallHostFromRegs; argument kinds are
 * packed from the callee's declared ParamType (IrLinHostArgIsFp). */
static int IrLinHostMathCtype(const char *Name)
{
    static const char *const names[] = {
        "sqrt", "fabs", "pow", "floor", "ceil", "sin", "cos", "tan",
        "asin", "acos", "atan", "atan2", "exp", "log", "log10", "fmod",
        "sinh", "cosh", "tanh", "round", "atof",
        "isalpha", "isdigit", "isalnum", "isspace", "isupper", "islower",
        "isprint", "ispunct", "isxdigit", "iscntrl", "isgraph",
        "toupper", "tolower", NULL
    };
    int i;
    for (i = 0; names[i] != NULL; i++)
        if (strcmp(Name, names[i]) == 0)
            return 1;
    return 0;
}

int IrLinHostSupported(const char *Name)
{
    if (Name == NULL) return 0;
    return (strcmp(Name, "printf") == 0 || strcmp(Name, "sprintf") == 0
         || strcmp(Name, "snprintf") == 0 || strcmp(Name, "puts") == 0
         || strcmp(Name, "putchar") == 0 || strcmp(Name, "fprintf") == 0
         || strcmp(Name, "strcpy") == 0 || strcmp(Name, "strncpy") == 0
         || strcmp(Name, "strcat") == 0 || strcmp(Name, "strncat") == 0
         || strcmp(Name, "strcmp") == 0 || strcmp(Name, "strncmp") == 0
         || strcmp(Name, "strlen") == 0 || strcmp(Name, "memcpy") == 0
         || strcmp(Name, "memcmp") == 0 || strcmp(Name, "memmove") == 0
         || strcmp(Name, "memset") == 0
         || strcmp(Name, "malloc") == 0 || strcmp(Name, "free") == 0
         || strcmp(Name, "calloc") == 0 || strcmp(Name, "realloc") == 0
         || strcmp(Name, "qsort") == 0 || strcmp(Name, "bsearch") == 0
         || strcmp(Name, "atoi") == 0 || strcmp(Name, "atol") == 0
         || strcmp(Name, "strtol") == 0 || strcmp(Name, "strtoul") == 0
         || strcmp(Name, "abs") == 0 || strcmp(Name, "labs") == 0
         || strcmp(Name, "exit") == 0
         || strcmp(Name, "getchar") == 0 || strcmp(Name, "fgetc") == 0
         || strcmp(Name, "fgets") == 0 || strcmp(Name, "fputs") == 0
         || strcmp(Name, "scanf") == 0 || strcmp(Name, "sscanf") == 0
         || strcmp(Name, "fopen") == 0 || strcmp(Name, "fclose") == 0
         || strcmp(Name, "remove") == 0 || strcmp(Name, "gets") == 0
         || strcmp(Name, "assert") == 0 || strcmp(Name, "__assert_fail") == 0
         || strcmp(Name, "__assert_rtn") == 0
         || strcmp(Name, "clock") == 0
         || strcmp(Name, "strcasecmp") == 0
         || strcmp(Name, "strncasecmp") == 0
         || IrLinHostMathCtype(Name));
}

int IrExprIntegerOk(IrExpr *E)
{
    int i;
    if (E == NULL) return 1;
    /* Structural pointer ops only — TypeFlags IR_TF_PTR is often inherited
     * from function designators / call results and is not reliable here. */
    if (E->Kind == IrExMember)
    {
        if (E->Left == NULL) return 0;
        if (E->Left->Kind == IrExIdent) return 1;
        if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
            && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
            return 1;
        return IrExprIntegerOk(E->Left);
    }
    if (E->Kind == IrExIndex)
    {
        IrExpr *Base = E->Left;
        IrExpr *Root;
        if (Base == NULL) return 0;
        /* Nested a[i][j]: the root must be an identifier. Local/global
         * arrays use flattened indexing; pointer-to-pointer parameters use
         * the typed indirect-index path for argv[i][j]. */
        if (Base->Kind == IrExIndex)
        {
            Root = Base;
            while (Root != NULL && Root->Kind == IrExIndex)
                Root = Root->Left;
            if (Root == NULL || Root->Kind != IrExIdent)
                return 0;
            if (!IrExprIntegerOk(E->Right)) return 0;
            return IrExprIntegerOk(Base);
        }
        /* s.arr[i] / (*p).arr[i]: Index of array Member (nested / arrow OK) */
        if (Base->Kind == IrExMember
            && Base->Left != NULL && Base->Ident != NULL
            && (Base->Left->Kind == IrExIdent || Base->Left->Kind == IrExMember
                || (Base->Left->Kind == IrExUnary && Base->Left->Op == TokenAsterisk
                    && Base->Left->Left != NULL
                    && Base->Left->Left->Kind == IrExIdent)))
        {
            if (!IrExprIntegerOk(E->Right)) return 0;
            return IrExprIntegerOk(Base->Left);
        }
        /* Local array, param int*, global array — LDX under VM. */
        if (!IrExprIntegerOk(E->Right)) return 0;
        return 1;
    }
    /* &x / *p: allow when operand is Ident/Index/simple ptr expr —
     * lower already emits LoadLocal / LoadIndex Tok=2. */
    if (E->Kind == IrExUnary && E->Op == TokenAmpersand)
    {
        if (E->Left == NULL) return 0;
        if (E->Left->Kind == IrExIdent) return 1;
        if (E->Left->Kind == IrExIndex)
            return IrExprIntegerOk(E->Left);
        /* &s.m / &v[i].m / &ret()->m — bitfields cannot form an address (506). */
        if (E->Left->Kind == IrExMember)
        {
            if (E->Left->BitWidth > 0)
                return 0;
            return IrExprIntegerOk(E->Left);
        }
        return 0;
    }
    if (E->Kind == IrExUnary && E->Op == TokenAsterisk)
    {
        if (E->Left == NULL) return 0;
        if (E->Left->Kind == IrExIdent) return 1;
        if (E->Left->Kind == IrExIndex)
            return IrExprIntegerOk(E->Left); /* *a[i] / *ptrs[i] */
        if (E->Left->Kind == IrExMember)
            return IrExprIntegerOk(E->Left); /* *o.ip / *s.p */
        if (E->Left->Kind == IrExBinOp || E->Left->Kind == IrExUnary)
            return IrExprIntegerOk(E->Left);
        if (E->Left->Kind == IrExCall)
            return IrExprIntegerOk(E->Left);
        return 0;
    }
    if (E->Kind == IrExIdent)
    {
        /* Pointer Ident OK under linear (LoadLocal of address bits). */
        return 1;
    }
    if (E->Kind == IrExCall)
    {
        const char *n = NULL;
        if (E->Left != NULL && E->Left->Ident != NULL)
            n = E->Left->Ident;
        else if (E->Ident != NULL)
            n = E->Ident;
        /* Call forms: name, local_fp, (*fp), tab[i], (*s.fn), (*p->fn). */
        if (E->Left != NULL)
        {
            IrExpr *Callee = E->Left;
            if (Callee->Kind == IrExUnary && Callee->Op == TokenAsterisk
                && Callee->Left != NULL)
                Callee = Callee->Left; /* (*x)(...) */
            if (Callee->Kind == IrExIdent)
            {
                /* ParamSlot formal fptr OK */
            }
            else if (Callee->Kind == IrExIndex
                     && Callee->Left != NULL && Callee->Left->Kind == IrExIdent
                     && Callee->Right != NULL
                     && IrExprIntegerOk(Callee->Right)
                     && (Callee->Left->LocalSlot >= 0
                         || (Callee->Left->LocalSlot < 0
                             && Callee->Left->ParamSlot < 0
                             && Callee->Left->Ident != NULL)))
            {
                /* tab[i](...) local or global fptr table */
            }
            else if (Callee->Kind == IrExMember
                     && Callee->Left != NULL && Callee->Ident != NULL
                     && IrExprIntegerOk(Callee->Left))
            {
                /* s.fn / p->fn / tab[i].fn — member fptr */
            }
            else
                return 0;
        }
        /* stdio host calls (scanf/fgets/fopen/fclose/sscanf/gets)
         * are on the shared IrLinHostSupported list and lower as Intrinsic.
         * Pointer args are packed by the existing CallArgIsPtr path. */
        if (E->Args != NULL)
            for (i = 0; i < E->ArgCount; i++)
            {
                IrExpr *A = E->Args[i];
                if (A == NULL) continue;
                /* Function designator as arg: emit packs the persistent
                 * Func Value* (Tok=5 Imm). Guest qsort/bsearch and similar
                 * resolve it via BabycGuestResolveFuncValue. Trampolines
                 * that need a different ABI stay on tree only if the host
                 * is not on IrLinHostSupported. */
                if (!IrExprIntegerOk(A)) return 0;
            }
        return 1;
    }
    if (!IrExprIntegerOk(E->Left)) return 0;
    if (!IrExprIntegerOk(E->Right)) return 0;
    if (E->Args != NULL)
        for (i = 0; i < E->ArgCount; i++)
            if (!IrExprIntegerOk(E->Args[i])) return 0;
    return 1;
}


int IrStmtIntegerOk(IrStmt *S)
{
    int i;
    if (S == NULL) return 1;
    if (S->Kind == IrStDecl)
    {
        /* VLA storage and sizeof depend on the active runtime frame. */
        if (S->DeclArraySize == -2)
            return 0;
        /* Static scalar or BSS array of scalar elems via IrStaticLocalGet. */
        if (S->DeclIsStatic)
        {
            if (S->DeclIsFptr)
                return 0;
            /* DeclIsPtr is often set on a[N] (blob addr in LocalVal) — OK for arrays. */
            if (S->DeclIsPtr && S->DeclArraySize <= 0)
                return 0; /* scalar pointer static residual */
            if (S->DeclFromType != NULL && S->DeclFromType->Base == TypeFP)
                return 0;
            /* Static struct/union BSS (no brace-init). */
            if (S->DeclFromType != NULL
                && (S->DeclFromType->Base == TypeStruct
                    || S->DeclFromType->Base == TypeUnion)
                && S->DeclArraySize <= 1
                && S->DeclInitCount <= 0 && S->DeclInitVals == NULL)
            {
                size_t nbytes = S->DeclFromType->Sizeof > 0
                    ? (size_t)S->DeclFromType->Sizeof : 0;
                if (nbytes == 0 || nbytes > 4096)
                    return 0;
                return 1;
            }
            if (S->DeclFromType != NULL
                && (S->DeclFromType->Base == TypeStruct
                    || S->DeclFromType->Base == TypeUnion))
                return 0; /* brace / array-of-struct residual */
            if (S->DeclArraySize > 0)
            {
                int es = S->DeclElemSize > 0 ? S->DeclElemSize : 0;
                long long nbytes;
                if (es <= 0) es = (int)sizeof(int);
                if (es != 1 && es != 2 && es != 4 && es != 8)
                    return 0;
                if (S->DeclArraySize > 1024)
                    return 0;
                nbytes = (long long)S->DeclArraySize * (long long)es;
                if (nbytes <= 0 || nbytes > 4096)
                    return 0;
                return 1; /* static char/int/... a[N] BSS */
            }
            return 1; /* scalar static int/char/... */
        }
        /* DeclIsFptr: allowed under linear — LocalVal holds Value*. */
        if (S->DeclIsFptr) return 1;
        /* Value arrays first: ir_compile often sets DeclIsPtr=1 on a[N]
         * (LocalVal holds blob addr). DECL_ARRAY handles int-scale elems. */
        if (S->DeclArraySize > 0 && !S->DeclElemIsPtr)
        {
            int es = S->DeclElemSize;
            long long nbytes;
            if (es <= 0 && S->DeclFromType != NULL && S->DeclFromType->FromType != NULL)
                es = S->DeclFromType->FromType->Sizeof;
            if (es <= 0)
                es = (int)sizeof(int);
            /* char/short/int arrays → DECL_ARRAY (Kin.A = es 1..255).
             * Cap size so stack-overflow / charge tests stay on tree
             * (linear calloc does not charge the PicoC stack arena). */
            if (S->DeclFromType != NULL && S->DeclFromType->FromType != NULL)
            {
                enum BaseType eb = S->DeclFromType->FromType->Base;
                if (eb == TypePointer || eb == TypeArray
                    || eb == TypeFunction)
                    return 0;
                /* Array of struct/union: DECL_ARRAY blob, Member via Index. */
                if (eb == TypeStruct || eb == TypeUnion)
                {
                    if (es <= 0)
                        es = S->DeclFromType->FromType->Sizeof;
                    if (es <= 0)
                        return 0;
                    if (S->DeclArraySize > 64)
                        return 0;
                    /* fall through to nbytes cap */
                }
            }
            /* LocalElemSize / IdxMetaEs now preserve full positive byte strides;
             * large struct elements are safe when the total object remains bounded. */
            nbytes = (long long)S->DeclArraySize * (long long)es;
            /* Size caps removed: linear DECL_ARRAY now applies the same
             * virtual stack-charge check as the tree path. Arrays that fit
             * the budget run; truly huge ones fail with stack overflow. */
            if (nbytes <= 0)
                return 0;
            /* Array of bitfield-struct allowed: Load/Store Tok=1 + IrSafeMemberAddr
             * stamp BitWidth and use IrBitExtract/Insert (same as scalar s.bf).
             * InitMap is storage-unit grain, not per-bit — fine for assign-then-read. */
            return 1;
        }
        /* Array of pointers: int *a[N]. Enable for the Linear/bytecode path
         * once pointer-word Index stores can update LocalInitMap. */
        if (S->DeclArraySize > 0 && S->DeclElemIsPtr && !S->DeclIsFptr
            && IrVmLinearFeaturesOn())
        {
            int n = S->DeclArraySize;
            int es = (int)sizeof(void *);
            long long nbytes = (long long)n * (long long)es;
            if (n <= 0 || n > 1024 || nbytes > 8192)
                return 0;
            return 1;
        }
        /* Scalar pointer locals: lower stores bits + LocalIsPtr/LocalElemSize. */
        if (S->DeclIsPtr && !S->DeclIsFptr && S->DeclArraySize <= 0)
            return 1;
        if (S->DeclIsPtr) return 0;
        if (S->DeclFromType != NULL)
        {
            enum BaseType b = S->DeclFromType->Base;
            /* Scalar struct/union: DECL_STRUCT allocates owned blob.
             * Brace-init (bitfields) stays on tree. */
            if ((b == TypeStruct || b == TypeUnion) && S->DeclArraySize <= 1)
            {
                if (S->DeclInitCount > 0 || S->DeclInitVals != NULL)
                    return 0;
                return 1;
            }
            if (b == TypeStruct || b == TypeUnion)
                return 0;
            if (b == TypeFunction || b == TypeEnum || b == TypePointer
                || b == TypeArray)
                return 0;
        }
    }
    /* Switch lower present (case/default/break). Fallthrough is sequential
     * body walk; return-in-case terminates via IrLinRet. */
    /* Do-while: lower present but residual SEGV under EXEC=vm — tree. */
    if (S->Kind == IrStDelegate) return 0;
    if (!IrExprIntegerOk(S->Expr)) return 0;
    if (!IrStmtIntegerOk(S->ThenStmt)) return 0;
    if (!IrStmtIntegerOk(S->ElseStmt)) return 0;
    if (!IrStmtIntegerOk(S->BodyStmt)) return 0;
    if (!IrStmtIntegerOk(S->InitStmt)) return 0;
    if (!IrStmtIntegerOk(S->IncStmt)) return 0;
    if (S->Stmts != NULL)
        for (i = 0; i < S->StmtCount; i++)
            if (!IrStmtIntegerOk(S->Stmts[i])) return 0;
    return 1;
}


int IrFnBodyIntegerEligible(IrStmt *Body)
{
    return IrStmtIntegerOk(Body);
}


