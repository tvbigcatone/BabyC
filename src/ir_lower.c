/* ir_lower.c — F5 linear form + shadow.
 * Expr: const, local, + - *, unary minus, relops, assign (value only).
 * Stmt: block, expr assign, if, while. No calls, no break/continue.
 * BABYC_LOWER=1. Default off. Linear side does not print or store the frame. */
#include "picoc.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ir_lin.h"
#include "ir_fncode.h"
#include "ir_lin_tabs.h"
#include "ir_lin_light.h"
#include "ir_lin_exec.h"
#include "ir_lin_ptr.h"
#include "ir_lin_compile.h"
#include "ir_lin_emit.h"
#include "ir_runtime.h"
#include "ir_storage.h"
#ifdef _MSC_VER
#pragma warning(disable: 4101) /* unreferenced local in dual-path stubs */
#endif
long long IrApplyBinOpRaw(struct ParseState *, enum LexToken, long long, long long, int, int);

/* ir_ops.c */
int IrTypeIsUnsignedBase(enum BaseType B);
/* IrVmShadow / IrVmCommit: declared in ir_lin.h */
int IrLinPeephole(IrLinInst *Code, int N);

extern long long IrEvalExprTree(struct ParseState *Parser, IrExpr *E);
extern void IrExecuteStmtTree(struct ParseState *Parser, IrStmt *S);

int BabycLowerOn;   /* 0/1 cached — see IrLowerEnabled() inline */
int BabycDebugOn;   /* 0/1 cached from BabycDebugEnabled */
unsigned long long BabycLinInstExec;
unsigned long long BabycTreeStmtExec;
unsigned long long BabycTreeExprExec;
unsigned long long BabycFnCodeBodyRuns;
unsigned long long BabycTreeBodyRuns;

/* Optional diagnostics for the experimental body-level VM path. */
static int VmProfileEnabled = -1;
static unsigned long long VmProfAttempts, VmProfRuns, VmProfRunFail;
static unsigned long long VmProfRejectDepth, VmProfRejectPtr, VmProfRejectSmall;
static unsigned long long VmProfCompileFail, VmProfIneligible, VmProfTinyTree;
static unsigned long long VmProfRunNested, VmProfRunTop, VmProfPtrIndexedAdmit;
unsigned long long VmProfFuseLocalAdd, VmProfFuseIndexCopy;
int IrVmProfileOn(void)
{
    if (VmProfileEnabled < 0)
    {
        const char *e = getenv("BABYC_VM_PROFILE");
        VmProfileEnabled = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return VmProfileEnabled;
}

static int ShadowDepth;
int LinExecDepth;
int BabycLinExecDepth; /* mirror for tree counters */ /* >0 while linear/VM Step is running */
int LinDiv0;
int LinStepAbort; /* linear hit step budget */
int LinRetSeen;
unsigned char CallSkip[128];
int LinSkippedCall;
/* Compile-time multidim dims: stamped at Decl lower, read at Index lower
 * (runtime LocalNDims is only set when DECL_ARRAY kernel Steps). */
unsigned char CompLocalNDims[IR_CALL_MAX_LOCALS];
int CompLocalDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
int CompLocalLeafEs[IR_CALL_MAX_LOCALS];
int CompLocalPointeeSize[IR_CALL_MAX_LOCALS];
unsigned char CompLocalElemIsPtr[IR_CALL_MAX_LOCALS];
/* Slot declared const somewhere in the body being lowered (sticky per body:
 * a slot reused by a sibling scope stays conservatively const). Read by the
 * linear fusions that would otherwise bypass the StoreLocal const check. */
unsigned char CompLocalIsConst[IR_CALL_MAX_LOCALS];
struct ValueType *CompLocalFromType[IR_CALL_MAX_LOCALS];
IrExpr *GlobTab[32];
struct Value *GlobVal[32]; /* resolved at lower; no name lookup in Step */
long long *GlobRaw[32]; /* direct scalar payload when TypeLongLong/Int */

struct Value *CallFnTab[IR_LIN_CALL_SLOTS];
/* >=0: resolve FV from LocalVal[slot] at runtime (local fptr). */
int CallIndirectSlot[IR_LIN_CALL_SLOTS];
/* >=0: FV bits already in LR[reg] (tab[i] / member). */
int CallIndirectReg[IR_LIN_CALL_SLOTS];
/* >=0: FV from ParamVal[param] (formal fptr). */
int CallIndirectParam[IR_LIN_CALL_SLOTS];
/* Pending Member/Index callee to lower AFTER args (avoid reg clobber). */
IrExpr *CallIndirectExpr[IR_LIN_CALL_SLOTS];
const char *CallNameTab[IR_LIN_CALL_SLOTS];
int CallCharPos[IR_LIN_CALL_SLOTS];
const char *LinHostCallName;
int CallArgReg[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
int CallArgN[IR_LIN_CALL_SLOTS];
unsigned char CallArgIsPtr[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS]; /* 1 = pack as pointer (string/ptr) */
unsigned char CallArgIsFp[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];  /* 1 = pack as double (IEEE bits) */
unsigned char CallArgTypeBase[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
struct ValueType *CallArgType[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
long long CallStrBits[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
unsigned char CallStrOn[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
int CallN;
IrStmt *KernelStmtTab[16];
int KernelN;
/* Kernel kinds stored in Tok of IrLinKernel */
/* from ir_fast.c */
int IrKernelFpEnabled(void);
#define IR_K_DECL_STATIC  11 /* scalar static local: persist table */
#define IR_K_DECL_STATIC_ARR 12 /* static scalar array BSS persist */
#define IR_K_DECL_STATIC_STU 13 /* static struct/union BSS persist */
#define IR_K_STRIDE_FILL  9  /* for(j=start; j<=b; j+=st) g[j]=c  global char/int fill */
#define IR_K_SCAN_ACCUM  10 /* for(i=s; i<=b; i++) if(!g[i]){c++;sum+=i;} */

/* Local pointer ident whose declared pointee type is unsigned (unsigned char *q):
 * TypeFlags are not reliable for locals, so ask the frame type stamped at Decl. */
int IrLinLocalPointeeUnsigned(IrExpr *Base)
{
    IrCallFrame *Fr;
    struct ValueType *T;
    if (Base == NULL || Base->Kind != IrExIdent || Base->LocalSlot < 0
        || Base->LocalSlot >= IR_CALL_MAX_LOCALS || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    T = Fr->LocalFromType[Base->LocalSlot] ? Fr->LocalFromType[Base->LocalSlot]
                                           : Fr->LocalTyp[Base->LocalSlot];
    if (T == NULL) return 0;
    if (T->Base == TypePointer && T->FromType != NULL)
        T = T->FromType;
    return IrTypeIsUnsignedBase(T->Base);
}

/* Return the declared element type for a local array/index base.  Multidimensional
 * declarations can retain nested TypeArray nodes in LocalFromType, so a single
 * FromType step is not sufficient for determining scalar signedness. */
struct ValueType *IrLinLocalIndexedType(IrCallFrame *Fr, int Slot)
{
    struct ValueType *T;
    if (Fr == NULL || Slot < 0 || Slot >= IR_CALL_MAX_LOCALS)
        return NULL;
    T = Fr->LocalFromType[Slot] ? Fr->LocalFromType[Slot] : Fr->LocalTyp[Slot];
    while (T != NULL && T->Base == TypeArray && T->FromType != NULL)
        T = T->FromType;
    /* A non-owned local pointer indexes its pointee; an owned array of
     * pointers instead yields pointer values and must keep TypePointer. */
    if (T != NULL && Fr->LocalIsPtr[Slot] && !Fr->LocalOwned[Slot]
        && T->Base == TypePointer && T->FromType != NULL)
        T = T->FromType;
    return T;
}

/* Multi-dim a[i][j] uninit caret: last ']' on the access line (tree engine parity). */
void IrLinCaretOnLastBracket(struct ParseState *P, IrExpr *IX)
{
    const char *Src, *LP, *C, *Best = NULL;
    int ln;
    if (P == NULL || P->SourceText == NULL) return;
    if (IX != NULL && IX->SourceLine > 0) P->Line = IX->SourceLine;
    Src = P->SourceText;
    for (LP = Src, ln = 1; *LP != '\0' && ln < P->Line; LP++)
        if (*LP == '\n') ln++;
    for (C = LP; *C && *C != '\n' && *C != '\r'; C++)
        if (*C == ']') Best = C;
    if (Best != NULL) P->CharacterPos = (int)(Best - LP) + 1;
}

int LowerCallEnabled(void)
{
    static int On = -1;
    if (On < 0)
    {
        const char *e = getenv("BABYC_LOWER_CALL");
        /* Explicit 1, or auto under linear/bytecode path. */
        if (e != NULL && e[0] == '1')
            On = 1;
        else if (e != NULL && e[0] == '0')
            On = 0;
        else if (IrExecIsVmProd())
            On = 1;
        else
            On = 0;
    }
    return On;
}

/* Resolve local struct/union member address. *Es = size, *Eb = ValueType Base
 * (signed vs unsigned) so IrLoadPtrElem zero-extends ushort/uchar correctly
 * (analysis §10.2 compound member UNSIGNED soft-mismatch). */
/* Forward: Light window used by IrSafeMemberAddr for Param bases. */
/* Light state → ir_lin_light.c */

/* IrSafeMemberAddr → extracted */

IrExpr *IdxTab[32];
int IdxN;
/* LDX/STX meta filled at lower: skip IrExpr walk on hot path. */
signed char IdxMetaSlot[32];
unsigned char IdxMetaKind[32]; /* 0=local 1=global 2=param 3=other */
int IdxMetaEs[32];
int IdxMetaN[32];
/* Full-width Imm when ConstVal does not fit int32 (unsigned 4e9). */
long long LinWideImm[IR_LIN_STMT_MAX];
unsigned char LinWideOn[IR_LIN_STMT_MAX];
int LinWideMax; /* exclusive max index with WideOn */
long long IdxSave[32];
void *IdxAddr[32];
int IdxEs[32];
void *IdxGlobBase[32]; /* global array base cache */
int IdxEb[32];
unsigned IdxDirty;
int GlobN;
struct ParseState *LinParser;

/* Stack-safe Index runtime state across nested Lin Call / LightRunBody.
 * IdxTab/meta are restored by FnCodeInstall; base cache + Dirty must not
 * leak between frames (dfs_rec: global 2D adj + 1D vis recursion). */
/* IdxRunSave → ir_lin_tabs.h */

void IdxRunPush(IdxRunSave *S)
{
    int i, n;
    /* Fast path: no Index ops live → nothing to save/clear. */
    if (IdxN <= 0 && IdxDirty == 0)
    {
        S->Dirty = 0;
        return;
    }
    S->Dirty = IdxDirty;
    n = IdxN > 0 ? IdxN : 32;
    if (n > 32) n = 32;
    for (i = 0; i < n; i++)
    {
        S->GlobBase[i] = IdxGlobBase[i];
        S->Addr[i] = IdxAddr[i];
        S->Save[i] = IdxSave[i];
        S->Es[i] = IdxEs[i];
        S->Eb[i] = IdxEb[i];
        IdxGlobBase[i] = NULL;
    }
    for (; i < 32; i++)
        IdxGlobBase[i] = NULL;
    IdxDirty = 0;
}

void IdxRunPop(const IdxRunSave *S)
{
    int i, n;
    if (S->Dirty == 0 && IdxN <= 0)
        return;
    IdxDirty = S->Dirty;
    n = IdxN > 0 ? IdxN : 32;
    if (n > 32) n = 32;
    for (i = 0; i < n; i++)
    {
        IdxGlobBase[i] = S->GlobBase[i];
        IdxAddr[i] = S->Addr[i];
        IdxSave[i] = S->Save[i];
        IdxEs[i] = S->Es[i];
        IdxEb[i] = S->Eb[i];
    }
}

void IdxRunClearCacheN(int n)
{
    int i;
    if (n < 0 || n > 32) n = 32;
    for (i = 0; i < n; i++)
        IdxGlobBase[i] = NULL;
    IdxDirty = 0;
}

void IdxRunClearCache(void)
{
    IdxRunClearCacheN(32);
}

/* Resolve Call return type → pointee struct/union for ret()->m linear path.
 * Mirrors IrCallReturnStruct (ir_compile) without cross-unit dependency. */
/* *IrLowerCallReturnStruct → ir_lin_ptr.c */

/* Full return type of a Call (not pointee). NULL if unresolved. */
/* *IrLowerCallReturnType → ir_lin_ptr.c */

/* Member offset + size from struct type. Returns 0 on failure. */
/* IrLowerMemberOffEs → ir_lin_ptr.c */

void IdxMetaPush(IrExpr *E)
{
    int i = IdxN;
    IrExpr *B;
    if (i >= 32)
        return;
    IdxTab[i] = E;
    IdxMetaSlot[i] = -1;
    IdxMetaKind[i] = 3;
    IdxMetaEs[i] = 0;
    IdxMetaN[i] = 0;
    /* A metadata slot may be reused while compiling another function. Its
     * cached global base belongs to the previous slot owner until resolved. */
    IdxGlobBase[i] = NULL;
    if (E == NULL || E->Kind != IrExIndex || E->Left == NULL)
    {
        IdxN = i + 1;
        return;
    }
    /* Walk nested a[i][j]… to root Ident (leaf element meta). */
    B = E->Left;
    while (B != NULL && B->Kind == IrExIndex)
        B = B->Left;
    if (B == NULL || B->Kind != IrExIdent)
    {
        IdxN = i + 1;
        return;
    }
    if (B->LocalSlot >= 0 && B->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        IdxMetaSlot[i] = (signed char)B->LocalSlot;
        IdxMetaKind[i] = 0;
        /* Pointer-element arrays are classified by metadata, never by
         * byte-size: a 4-byte struct on 32-bit targets is not a pointer.
         * The compile-time flag may be unset before DECL_ARRAY stamps the
         * live frame, so consult both metadata sinks. */
        if (IrCompLocalElemIsPtrOrFrame(B->LocalSlot)
            || (IrCallDepth > 0
                && IrCallStack[IrCallDepth - 1]->LocalIsFptr[B->LocalSlot]))
            IdxMetaEs[i] = (int)sizeof(void *);
        else
        {
            /* Use the compile-time leaf stride when lowering runs without a
             * live frame; the frame value is a fallback, not the only source.
             * Keep pointer-element classification above this branch because
             * array leaf stride and pointer-pointee stride are distinct. */
            IdxMetaEs[i] = IrCompLocalLeafEsOrFrame(B->LocalSlot);
            if (IdxMetaEs[i] <= 0)
                IdxMetaEs[i] = 4;
        }
        /* Bounds metadata is independent of element representation. In
         * particular, pointer-element arrays still need their known extent. */
        if (IrCallDepth > 0)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            if (Fr->LocalOwned[B->LocalSlot] || Fr->LocalArraySize[B->LocalSlot] > 0)
                IdxMetaN[i] = Fr->LocalArraySize[B->LocalSlot];
            else
                IdxMetaN[i] = 0;
            if (getenv("BABYC_FP_TRACE"))
                fprintf(stderr, "[idxmeta] slot=%d leafEs=%d frEs=%d metaEs=%d fptr=%d\n",
                    B->LocalSlot, IrCompLocalLeafEsOrFrame(B->LocalSlot), (int)Fr->LocalElemSize[B->LocalSlot],
                    (int)IdxMetaEs[i], (int)Fr->LocalIsFptr[B->LocalSlot]);
        }
    }
    else if (B->ParamSlot >= 0 && B->ParamSlot < IR_CALL_MAX_PARAMS
        && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot == B->ParamSlot
        && !(E->TypeFlags & IR_TF_PTR)
        && (E->ElemSize == 1 || E->ElemSize == 2
            || E->ElemSize == 4 || E->ElemSize == 8))
    {
        /* Only mark direct pointer-to-scalar indexing. A pointer-to-pointer
         * expression such as argv[i], or a nested pointer-to-array expression,
         * must not share the scalar fast-path metadata. */
        IdxMetaSlot[i] = (signed char)B->ParamSlot;
        IdxMetaKind[i] = 2;
        IdxMetaEs[i] = E->ElemSize;
    }
    else if (B->ParamSlot >= 0 && B->ParamSlot < IR_CALL_MAX_PARAMS
        && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot == B->ParamSlot
        && ((E->TypeFlags & IR_TF_PTR)
            || E->ElemSize == (int)sizeof(void *)))
    {
        /* argv[i] / char **p index: pointer-sized elements (kind 4). */
        IdxMetaSlot[i] = (signed char)B->ParamSlot;
        IdxMetaKind[i] = 4;
        IdxMetaEs[i] = (int)sizeof(void *);
    }
    else if (B->Ident != NULL)
    {
        /* Global: leaf es + total flat N from TypeArray chain. */
        IdxMetaKind[i] = 1;
        IdxMetaEs[i] = 4;
        if (LinParser != NULL)
        {
            struct Value *GV = IrGlobalVal(LinParser, B);
            if (GV != NULL && GV->Typ != NULL)
            {
                struct ValueType *T = GV->Typ;
                int total = 1;
                int leaf_es = 4;
                while (T != NULL && T->Base == TypeArray)
                {
                    if (T->ArraySize > 0)
                        total *= T->ArraySize;
                    if (T->FromType != NULL && T->FromType->Base != TypeArray)
                        leaf_es = T->FromType->Sizeof > 0
                            ? (int)T->FromType->Sizeof : 4;
                    T = T->FromType;
                }
                if (leaf_es > 0 && leaf_es <= 255)
                    IdxMetaEs[i] = leaf_es;
                IdxMetaN[i] = total > 0 ? total : 0;
            }
        }
    }
    IdxN = i + 1;
}

/* GlobPush → ir_lin_emit.c */
long long GlobSave[32];
int GlobSaved;
unsigned GlobDirty;
long long LinRetVal;
static int GotoPending;
static IrLinInst GotoCode[256];
static int GotoN;
static long long GotoCopy[128];
static int GotoDepth;
static int StmtDepth;
static int StmtBaseStack[IR_CALL_MAX_DEPTH];
static int StmtBaseSp; /* base StmtDepth per frame — callee gets its own top */
static int ShadowOwned; /* 1 after top lower succeeds — suppress nested tree debug */
#define LIN_BREAK_MAX 16
static int LinBreak[LIN_BREAK_MAX];
static int NLinBreak;
static int LinBreakSeeded;
long LinMatch, LinMismatch, LinLowered, LinSkipCallCount;
static long StMatch, StMismatch, StLowered, StGoto;

int IrDebugOn(void)
{
    return BabycDebugEnabled();
}

static int ShadowOwnedStack[IR_CALL_MAX_DEPTH];
static int ShadowOwnedSp;

void IrLowerOnFramePush(void)
{
    if (!BabycLowerOn && !BabycDebugOn)
        return;
    if (StmtBaseSp < IR_CALL_MAX_DEPTH)
        StmtBaseStack[StmtBaseSp++] = StmtDepth;
    if (ShadowOwnedSp < IR_CALL_MAX_DEPTH)
        ShadowOwnedStack[ShadowOwnedSp++] = ShadowOwned;
    ShadowOwned = 0;
}

void IrLowerOnFramePop(void)
{
    if (!BabycLowerOn && !BabycDebugOn)
        return;
    if (StmtBaseSp > 0)
        StmtBaseSp--;
    if (ShadowOwnedSp > 0)
        ShadowOwned = ShadowOwnedStack[--ShadowOwnedSp];
}

static int IrStmtIsTop(void)
{
    int base = (StmtBaseSp > 0) ? StmtBaseStack[StmtBaseSp - 1] : 0;
    return StmtDepth == base;
}

/* Seed structured breaks from BABYC_BREAK once. */
static void LinBreakSeed(void)
{
    const char *Br;
    if (LinBreakSeeded)
        return;
    LinBreakSeeded = 1;
    Br = getenv("BABYC_BREAK");
    if (Br == NULL)
        return;
    while (*Br && NLinBreak < LIN_BREAK_MAX)
    {
        int ln = atoi(Br);
        if (ln > 0)
            LinBreak[NLinBreak++] = ln;
        while (*Br && *Br != ',')
            Br++;
        if (*Br == ',')
            Br++;
    }
}

int LinBreakHas(int Line)
{
    int i;
    LinBreakSeed();
    for (i = 0; i < NLinBreak; i++)
        if (LinBreak[i] == Line)
            return 1;
    return 0;
}

void LinBreakAdd(int Line)
{
    int i;
    if (Line <= 0 || NLinBreak >= LIN_BREAK_MAX)
        return;
    for (i = 0; i < NLinBreak; i++)
        if (LinBreak[i] == Line)
            return;
    LinBreak[NLinBreak++] = Line;
#ifndef NO_DEBUGGER
    if (LinParser != NULL && LinParser->pc != NULL)
        DebugSetBreakpointAtLine(LinParser->pc,
            LinParser->FileName ? LinParser->FileName : "", Line);
#endif
}

void LinBreakClear(int Line)
{
    int i, j;
#ifndef NO_DEBUGGER
    if (LinParser != NULL && LinParser->pc != NULL)
    {
        if (Line <= 0)
            DebugClearAllBreakpoints(LinParser->pc);
        else
        {
            struct ParseState Fake;
            memset(&Fake, 0, sizeof(Fake));
            Fake.pc = LinParser->pc;
            Fake.FileName = LinParser->FileName ? (char *)LinParser->FileName : (char *)"";
            Fake.Line = (short)Line;
            DebugClearBreakpoint(&Fake);
        }
    }
#endif
    if (Line <= 0)
    {
        NLinBreak = 0;
        return;
    }
    for (i = 0; i < NLinBreak; i++)
    {
        if (LinBreak[i] == Line)
        {
            for (j = i + 1; j < NLinBreak; j++)
                LinBreak[j - 1] = LinBreak[j];
            NLinBreak--;
            return;
        }
    }
}

static const char *LinSlotName(int Slot)
{
    IrCallFrame *Fr;
    if (IrCallDepth <= 0 || Slot < 0)
        return NULL;
    Fr = IrCallStack[IrCallDepth - 1];
    if (Fr == NULL)
        return NULL;
    if (Slot < IR_CALL_MAX_LOCALS && Fr->LocalName[Slot] != NULL)
        return Fr->LocalName[Slot];
    if (Slot < Fr->NumParams && Fr->ParamName[Slot] != NULL)
        return Fr->ParamName[Slot];
    return NULL;
}

static int LinSlotByName(const char *Name)
{
    IrCallFrame *Fr;
    int i;
    if (Name == NULL || IrCallDepth <= 0)
        return -1;
    Fr = IrCallStack[IrCallDepth - 1];
    if (Fr == NULL)
        return -1;
    for (i = 0; i < IR_CALL_MAX_LOCALS; i++)
        if (Fr->LocalName[i] != NULL && strcmp(Fr->LocalName[i], Name) == 0)
            return i;
    for (i = 0; i < Fr->NumParams && i < IR_CALL_MAX_PARAMS; i++)
        if (Fr->ParamName[i] != NULL && strcmp(Fr->ParamName[i], Name) == 0)
            return i;
    return -1;
}

void LinPrintLocals(long long *Locals, int NLocals)
{
    int li;
    if (Locals == NULL)
        return;
    fprintf(stderr, "[babyc-debug] locals");
    for (li = 0; li < NLocals && li < 8; li++)
    {
        const char *nm = LinSlotName(li);
        if (nm != NULL)
            fprintf(stderr, " %s:%lld", nm, Locals[li]);
        else
            fprintf(stderr, " %d:%lld", li, Locals[li]);
    }
    fprintf(stderr, "\n");
}

static void LinPrintSlot(long long *Locals, int NLocals, int Slot)
{
    const char *nm;
    if (Locals == NULL || Slot < 0 || Slot >= NLocals)
    {
        fprintf(stderr, "[babyc-debug] print ?\n");
        return;
    }
    nm = LinSlotName(Slot);
    if (nm != NULL)
        fprintf(stderr, "[babyc-debug] print %s:%lld\n", nm, Locals[Slot]);
    else
        fprintf(stderr, "[babyc-debug] print %d:%lld\n", Slot, Locals[Slot]);
}

void LinPrintArg(long long *Locals, int NLocals, const char *Arg)
{
    int slot;
    if (Arg == NULL || *Arg == '\0')
        return;
    if (Arg[0] >= '0' && Arg[0] <= '9')
    {
        LinPrintSlot(Locals, NLocals, atoi(Arg));
        return;
    }
    slot = LinSlotByName(Arg);
    if (slot < 0)
    {
        fprintf(stderr, "[babyc-debug] print %s:?\n", Arg);
        return;
    }
    LinPrintSlot(Locals, NLocals, slot);
}


void IrLowerExecEnter(void) { LinExecDepth++; BabycLinExecDepth = LinExecDepth; }
void IrLowerSetRet(long long v) { LinRetSeen = 1; LinRetVal = v; }

/* A compact VM return of a local struct currently carries its blob address in
 * a scalar register. The callee frame frees that blob on pop, so copy aggregate
 * results into a depth-specific stable buffer before publishing the return.
 * The per-depth slot prevents recursive callees from clobbering their caller's
 * in-flight aggregate result. */
static unsigned char VmAggregateReturnBlob[IR_CALL_MAX_DEPTH][512];

int IrLowerVmIsAggregateReturnRange(long long v, size_t bytes)
{
    uintptr_t a = (uintptr_t)v;
    int i;
    if (v == 0 || bytes == 0 || a > UINTPTR_MAX - bytes) return 0;
    for (i = 0; i < IR_CALL_MAX_DEPTH; ++i)
    {
        uintptr_t b = (uintptr_t)VmAggregateReturnBlob[i];
        if (a >= b && a + bytes <= b + sizeof(VmAggregateReturnBlob[i])) return 1;
    }
    return 0;
}

long long IrLowerVmStabilizeAggregateReturn(long long v)
{
    int d = IrCallDepth - 1, i;
    struct Value *fn;
    struct ValueType *rt;
    IrCallFrame *fr;
    uintptr_t a = (uintptr_t)v;
    int sz, local_source = 0;
    if (v == 0 || d < 0 || d >= IR_CALL_MAX_DEPTH) return v;
    fn = IrCurrentFunc[d];
    if (fn == NULL || fn->Typ == NULL || fn->Typ->Base != TypeFunction
        || fn->Val == NULL) return v;
    rt = fn->Val->FuncDef.ReturnType;
    if (rt == NULL || (rt->Base != TypeStruct && rt->Base != TypeUnion)) return v;
    sz = rt->Sizeof;
    if (sz <= 0 || sz > (int)sizeof(VmAggregateReturnBlob[d])) return 0;
    fr = IrCallStack[d];
    if (fr == NULL || a > UINTPTR_MAX - (size_t)sz) return v;
    for (i = 0; i < IR_CALL_MAX_LOCALS; ++i)
    {
        struct ValueType *t;
        uintptr_t b;
        size_t n;
        if (!fr->LocalOwned[i] || fr->LocalVal[i] == 0) continue;
        b = (uintptr_t)fr->LocalVal[i];
        t = fr->LocalFromType[i] ? fr->LocalFromType[i] : fr->LocalTyp[i];
        if (fr->LocalArraySize[i] > 0 && fr->LocalElemSize[i] > 0)
            n = (size_t)fr->LocalArraySize[i] * (size_t)fr->LocalElemSize[i];
        else if (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion)
                 && t->Sizeof > 0) n = (size_t)t->Sizeof;
        else continue;
        if (b <= UINTPTR_MAX - n && a >= b && a + (size_t)sz <= b + n)
        { local_source = 1; break; }
    }
    /* External/global blobs and already-stable nested return values outlive
     * this frame; only frame-owned sources need copying before the pop. */
    if (!local_source) return v;
    memcpy(VmAggregateReturnBlob[d], (const void *)a, (size_t)sz);
    return (long long)(uintptr_t)VmAggregateReturnBlob[d];
}
int IrLowerRetSeen(void) { return LinRetSeen; }
long long IrLowerRetVal(void) { return LinRetVal; }
void IrLowerExecLeave(void) { if (LinExecDepth > 0) LinExecDepth--; BabycLinExecDepth = LinExecDepth; }

struct Value *IrLowerCallFn(int i)
{
    return (i >= 0 && i < CallN) ? CallFnTab[i] : NULL;
}
const char *IrLowerCallName(int i)
{
    return (i >= 0 && i < CallN) ? CallNameTab[i] : NULL;
}
int IrLowerCallArgN(int i)
{
    return (i >= 0 && i < CallN) ? CallArgN[i] : 0;
}
int IrLowerCallArgReg(int ci, int ai)
{
    if (ci < 0 || ci >= CallN || ai < 0 || ai >= IR_LIN_CALL_ARGS) return 0;
    return CallArgReg[ci][ai];
}
const unsigned char *IrLowerCallArgIsPtr(int ci)
{
    if (ci < 0 || ci >= CallN) return NULL;
    return CallArgIsPtr[ci];
}

const unsigned char *IrLowerCallArgIsFp(int ci)
{
    if (ci < 0 || ci >= CallN) return NULL;
    return CallArgIsFp[ci];
}

IrStmt *IrLowerKernelStmt(int i)
{
    return (i >= 0 && i < KernelN) ? KernelStmtTab[i] : NULL;
}



/* ---- P0.26 unified execution config ---- */
static int BabycCfgReady = 0;
static BabycPathKind BabycCfgPath = BABYC_PATH_TREE;
static int BabycCfgAccel = 1;
static int BabycCfgStrict = 0;

static int BabycEnvIs1(const char *e)
{
    return e != NULL && e[0] == '1' && e[1] == '\0';
}

static int BabycEnvIs0(const char *e)
{
    return e != NULL && e[0] == '0' && e[1] == '\0';
}

void BabycExecConfigInit(void)
{
    const char *path = getenv("BABYC_PATH");
    const char *accel = getenv("BABYC_ACCEL");
    const char *strict = getenv("BABYC_STRICT");
    const char *exec = getenv("BABYC_EXEC");
    const char *noir = getenv("BABYC_NO_IR");
    const char *vstrict = getenv("BABYC_VM_STRICT");
    const char *bc = getenv("BABYC_BYTECODE_VM");
    const char *noshape = getenv("BABYC_NO_SHAPE_FP");
    const char *nokern = getenv("BABYC_NO_KERNEL");

    BabycCfgPath = BABYC_PATH_TREE;
    BabycCfgAccel = 1;
    BabycCfgStrict = 0;

    /* Preferred PATH */
    if (path != NULL && path[0] != '\0')
    {
        if (strcmp(path, "token") == 0) BabycCfgPath = BABYC_PATH_TOKEN;
        else if (strcmp(path, "tree") == 0) BabycCfgPath = BABYC_PATH_TREE;
        else if (strcmp(path, "linear") == 0) BabycCfgPath = BABYC_PATH_LINEAR;
        else if (strcmp(path, "bytecode") == 0) BabycCfgPath = BABYC_PATH_BYTECODE;
        else if (strcmp(path, "vm") == 0) BabycCfgPath = BABYC_PATH_LINEAR; /* alias */
    }
    else if (noir != NULL && noir[0] != '\0' && noir[0] != '0')
        BabycCfgPath = BABYC_PATH_TOKEN;
    else if (exec != NULL)
    {
        if (strcmp(exec, "vm_strict") == 0)
        {
            BabycCfgPath = BABYC_PATH_LINEAR;
            BabycCfgStrict = 1;
        }
        else if (strcmp(exec, "vm") == 0)
            BabycCfgPath = BABYC_PATH_LINEAR;
        else if (strcmp(exec, "tree") == 0)
            BabycCfgPath = BABYC_PATH_TREE;
        else if (strcmp(exec, "both") == 0 || strcmp(exec, "ir") == 0)
            BabycCfgPath = BABYC_PATH_TREE; /* lower on, tree product */
    }

    /* Bytecode overlay */
    if (BabycCfgPath != BABYC_PATH_TOKEN && bc != NULL
        && (BabycEnvIs1(bc) || (bc[0] != '\0' && bc[0] != '0')))
    {
        if (BabycCfgPath == BABYC_PATH_TREE || BabycCfgPath == BABYC_PATH_LINEAR)
            BabycCfgPath = BABYC_PATH_BYTECODE;
    }
    /* Prefer explicit PATH=bytecode */
    if (path != NULL && strcmp(path, "bytecode") == 0)
        BabycCfgPath = BABYC_PATH_BYTECODE;

    /* STRICT */
    if (strict != NULL)
        BabycCfgStrict = BabycEnvIs1(strict) ? 1 : 0;
    else if (vstrict != NULL && BabycEnvIs1(vstrict))
        BabycCfgStrict = 1;
    /* EXEC=vm_strict already set above */

        /* ACCEL master: default ON. ACCEL=0 forces all fast-paths off.
     * When ACCEL=1/unset, per-feature BABYC_NO_SHAPE_FP / BABYC_NO_KERNEL still apply. */
    if (accel != NULL)
        BabycCfgAccel = BabycEnvIs0(accel) ? 0 : 1;
    else
        BabycCfgAccel = 1;
    (void)noshape;
    (void)nokern;

BabycCfgReady = 1;
}

static void BabycCfgEnsure(void)
{
    if (!BabycCfgReady)
        BabycExecConfigInit();
}

BabycPathKind BabycPathGet(void)
{
    BabycCfgEnsure();
    return BabycCfgPath;
}

int BabycAccelOn(void)
{
    BabycCfgEnsure();
    return BabycCfgAccel;
}

int BabycStrictOn(void)
{
    BabycCfgEnsure();
    return BabycCfgStrict;
}

void IrLowerEnabledInit(void)
{
    const char *e = getenv("BABYC_LOWER");
    const char *x = getenv("BABYC_EXEC");
    BabycPathKind path;

    BabycExecConfigInit();
    path = BabycPathGet();

    BabycLowerOn = (e != NULL && e[0] == '1' && e[1] == '\0') ? 1 : 0;
    /* Lower when linear/bytecode path, or legacy both/ir/vm* */
    if (path == BABYC_PATH_LINEAR || path == BABYC_PATH_BYTECODE)
        BabycLowerOn = 1;
    if (x && (strcmp(x, "both") == 0 || strcmp(x, "vm") == 0
            || strcmp(x, "vm_strict") == 0 || strcmp(x, "ir") == 0))
        BabycLowerOn = 1;
    BabycDebugOn = BabycDebugEnabled() ? 1 : 0;
}

/* PATH is linear or bytecode → VM production (no dual tree re-run). */
int IrExecIsVmProd(void)
{
    BabycPathKind p = BabycPathGet();
    return (p == BABYC_PATH_LINEAR || p == BABYC_PATH_BYTECODE) ? 1 : 0;
}

/* STRICT axis — no silent tree residual. */
int IrVmStrictOn(void)
{
    return BabycStrictOn();
}

/* STRICT controls what happens after a lowering failure; it must not select a
 * different C semantic lowering. Use the complete Linear feature set whenever
 * the production path is Linear/bytecode, even if residual-tree policy is off. */
int IrVmLinearFeaturesOn(void)
{
    return IrExecIsVmProd();
}
/* Under vm_strict: any residual tree body after a failed lower is a hard error. */
void IrVmStrictTreeFallback(struct ParseState *Parser, const char *why)
{
    if (!IrVmStrictOn() || Parser == NULL)
        return;
    ProgramFail(Parser, "strict: tree fallback (%s)", why ? why : "body");
}

/* True if lowered linear code uses memory ops incomplete under experimental
 * EXEC=vm prod (index/global). Shared by expr and stmt hooks. */
static int IrExprContainsCall(const IrExpr *E, int Depth);

static int IrStmtContainsCall(const IrStmt *S, int Depth)
{
    int i;
    if (S == NULL || Depth > 64) return 0;
    if (IrExprContainsCall(S->Expr, 0)) return 1;
    if (IrStmtContainsCall(S->ThenStmt, Depth + 1)
        || IrStmtContainsCall(S->ElseStmt, Depth + 1)
        || IrStmtContainsCall(S->BodyStmt, Depth + 1)
        || IrStmtContainsCall(S->InitStmt, Depth + 1)
        || IrStmtContainsCall(S->IncStmt, Depth + 1)) return 1;
    for (i = 0; S->Stmts != NULL && i < S->StmtCount; ++i)
        if (IrStmtContainsCall(S->Stmts[i], Depth + 1)) return 1;
    return 0;
}

static int IrExprContainsCall(const IrExpr *E, int Depth)
{
    int i;
    if (E == NULL || Depth > 64) return 0;
    if (E->Kind == IrExCall) return 1;
    if (IrExprContainsCall(E->Left, Depth + 1)
        || IrExprContainsCall(E->Right, Depth + 1)) return 1;
    for (i = 0; E->Args != NULL && i < E->ArgCount; ++i)
        if (IrExprContainsCall(E->Args[i], Depth + 1)) return 1;
    return 0;
}

/* Shadow comparison must never execute a side-effecting expression against
 * the live token frame. The linear executor writes LocalVal slots directly,
 * while the tree executor updates Value objects; restoring only LocalVal
 * therefore leaves the two representations inconsistent. Statement-level
 * shadowing already uses isolated copies, but expression-level shadowing did
 * not. Calls are included for completeness even though the caller has a call
 * guard of its own. */
static int IrExprHasSideEffects(const IrExpr *E, int Depth)
{
    int i;
    if (E == NULL || Depth > 64) return 0;
    if (E->Kind == IrExAssign || E->Kind == IrExCall) return 1;
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)) return 1;
    if (IrExprHasSideEffects(E->Left, Depth + 1)
        || IrExprHasSideEffects(E->Right, Depth + 1)) return 1;
    for (i = 0; E->Args != NULL && i < E->ArgCount; ++i)
        if (IrExprHasSideEffects(E->Args[i], Depth + 1)) return 1;
    return 0;
}

static int IrLinCodeHasCalls(const IrLinInst *Code, int N)
{
    int i;
    if (Code == NULL || N <= 0) return 0;
    for (i = 0; i < N; ++i)
        if (Code[i].Op == IrLinCall) return 1;
    return 0;
}

static int IrExecIsBoth(void)
{
    const char *x = getenv("BABYC_EXEC");
    return x != NULL && strcmp(x, "both") == 0;
}

static int IrLinCodeHasMemOps(const IrLinInst *Code, int N)
{
    int i;
    if (Code == NULL || N <= 0)
        return 0;
    for (i = 0; i < N; i++)
    {
        unsigned char op = Code[i].Op;
        if (op == IrLinLoadIndex || op == IrLinStoreIndex
            || op == IrLinLoadMemberArray || op == IrLinStoreMemberArray
            || op == IrLinStoreDeref
            || op == IrLinLoadGlobal || op == IrLinStoreGlobal)
            return 1;
    }
    return 0;
}

/* One-time notice: EXEC=vm is experimental (not default). */
void IrVmProdNotice(void)
{
    static int Once;
    IrLowerEnabledInit(); /* ensure BabycLowerOn cached under EXEC=vm */
    if (!Once && IrExecIsVmProd())
    {
        Once = 1;
        fprintf(stderr,
            "[babyc] PATH=%s STRICT=%d experimental: linear instruction interpreter prod "
            "(index/global mem ops + host Intrinsic ≤8 args)\n",
            BabycPathGet() == BABYC_PATH_BYTECODE ? "bytecode" : "linear",
            BabycStrictOn());
    }
}

/* Structural: body uses only integer-ish IR constructs (no index/member/ptr
 * decl, no printf-family calls). Foundation for per-function VM eligibility;
 * not wired into the default product path. */
/* IrExprIntegerOk → compile */ int UNUSED_IrExprIntegerOk_proto(IrExpr *E);

/* IrExprIntegerOk → extracted */

/* IrStmtIntegerOk → extracted */

/* Exposed for future per-function FnCode gate (default path unused). */
/* IrFnBodyIntegerEligible → extracted */

/* FnCodeSlot + FnSlots/FnActive/CompileSlot → ir_fncode.h/.c */
/* Thin wrapper → P0 IrStorageDescStampFnSlot (kept for residual call sites). */
void CompileStampPtrSlot(int slot, struct ValueType *FT, int is_fptr)
{
    IrStorageDesc D;
    IrStorageDescClear(&D);
    D.IsFptr = is_fptr ? 1 : 0;
    D.IsPtr = is_fptr ? 0 : 1;
    D.FromType = FT;
    IrStorageDescStampFnSlot(slot, &D, NULL);
}
void CompileStampName(int slot, const char *name)
{
    if (CompileSlot == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS || name == NULL)
        return;
    CompileSlot->SlotName[slot] = name;
}



/* Light window defined above IrSafeMemberAddr. */

/* FnCodeFind / FnCodeFindByFunc → ir_fncode.c */

/* FnCodeInstall → ir_fncode.c */

/* FnCodeSnapshot → ir_fncode.c */

/* Restore all live tables from a snapshot (Call/Glob/Idx/Kernel). */
/* LinTabsRestore → ir_fncode.c */

/* Nested linear frame: snapshot parent tables + runtime Idx cache. */
typedef struct LinFrame {
    FnCodeSlot Tabs;
    FnCodeSlot *PrevActive;
    IdxRunSave Run;
    int Valid;
    int SameCode;
} LinFrame;

#define LIN_FRAME_MAX 64
static LinFrame LinFrames[LIN_FRAME_MAX];
static int LinFrameSp;

int LinFramePushFor(FnCodeSlot *Target)
{
    LinFrame *F;
    if (LinFrameSp >= LIN_FRAME_MAX)
        return 0;
    F = &LinFrames[LinFrameSp++];
    F->PrevActive = FnActive;
    /* Same-function recursion leaves Call/Glob/Idx/Kernel/Wide tables
     * installed unchanged. Preserve only the runtime index-cache frame;
     * copying every compile table on each recursive call is pure overhead. */
    F->SameCode = Target != NULL && Target == FnActive;
    if (!F->SameCode)
        FnCodeSnapshot(&F->Tabs);
    IdxRunPush(&F->Run);
    F->Valid = 1;
    return 1;
}

int LinFramePush(void)
{
    return LinFramePushFor(NULL);
}

void LinFramePop(void)
{
    LinFrame *F;
    if (LinFrameSp <= 0)
        return;
    F = &LinFrames[--LinFrameSp];
    if (!F->Valid)
        return;
    if (!F->SameCode)
        LinTabsRestore(&F->Tabs);
    FnActive = F->PrevActive;
    IdxRunPop(&F->Run);
    F->Valid = 0;
}

/* Nested compile frame — defined after LinLoop/LinLab. */
int LowerDepth;
/* CompileFrameSp → ir_lin_compile.c */
int LinCompileActive; /* 1 during FnCodeCompile */

/* Live-frame LocalInit may be stamped only when we are truly executing on
 * that frame — not while FnCodeCompile of a callee is lowering into the
 * caller's live IrCallStack, and not under a light window (LightInitMask
 * owns init there). Slot indices are body-local; stamping during compile
 * aliases caller slots (1035: inner z and outer x both slot 0). */
int IrLiveInitStampOk(void)
{
    return !LinCompileActive && LightSp == 0;
}
void IrLiveMarkInit(IrCallFrame *Fr, int slot)
{
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;
    if (!IrLiveInitStampOk())
        return;
    Fr->LocalInit[slot] = 1;
}

/* forward */
int IrKernelStmtScalarOnly(IrStmt *S, int depth);
static int IrKernelExprScalarOnly(IrExpr *E, int depth);
 /* defined with CompileFrames */

/* Compile body into a free slot (or reuse). Saves/restores live Call/Glob
 * tables so a nested compile (main→add1) does not clobber the parent. */
/* FnCodeCompile → ir_lin_compile.c */

/* Run a compiled slot on the current frame. Swaps Call/Glob tables. */
static int FnCodeRun(struct ParseState *Parser, FnCodeSlot *S, int was_nested)
{
    int saved_ret = LinRetSeen;
    long long saved_retval = LinRetVal;
    int saved_abort = LinStepAbort;
    int saved_div0 = LinDiv0;
    enum RunMode saved_mode = Parser->Mode;
    int got_ret, bad;
    int pushed;
    /* Snapshot frame locals so a mid-run LinStepAbort cannot leave dirty
     * LocalVal for the tree fallback (P1-1: kernel fail then tree on
     * partially-updated i/b/a[] → silent wrong results). */
    long long snap_val[IR_CALL_MAX_LOCALS];
    char snap_init[IR_CALL_MAX_LOCALS];
    int snap_n = 0;
    int have_snap = 0;
    IrCallFrame *FrSnap = NULL;
    struct ParseState *saved_lin_parser = LinParser;

    LinParser = Parser;
    /* Only nested VM execution needs to preserve the parent's live side tables.
     * A top-level body runs from the tree/token caller, so there is no active
     * linear frame to restore: keep the most recently installed slot live and
     * skip redundant table copies / frame push-pop on repeated calls. */
    if (was_nested && FnActive == S)
    {
        IdxRunClearCache();
        pushed = 0;
    }
    else if (was_nested)
    {
        LinFramePush();
        pushed = 1;
        FnCodeInstall(S);
    }
    else
    {
        pushed = 0;
        if (FnActive != S)
            FnCodeInstall(S);
    }
    if (IrCallDepth > 0 && S != NULL)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int si;
        /* GUARD: ptr/fptr/FromType via descriptor seed. */
        IrStorageDescSeedFrameFromFnSlot(Fr, S);
        for (si = 0; si < IR_CALL_MAX_LOCALS; si++)
        {
            if (S->SlotName[si] != NULL && Fr->LocalName[si] == NULL)
                Fr->LocalName[si] = (char *)(void *)S->SlotName[si];
            /* Heuristic: unowned TypePointer without SlotIsPtr stamp (619). */
            if (!Fr->LocalIsPtr[si]
                && S->SlotFromType[si] != NULL
                && !Fr->LocalOwned[si]
                && Fr->LocalArraySize[si] <= 0
                && S->SlotFromType[si]->Base == TypePointer)
            {
                IrStorageDesc D;
                IrStorageDescFromType(S->SlotFromType[si], 1, 0, &D);
                IrStorageDescApplyFrame(Fr, si, &D, NULL,
                    IR_STAMP_FRAME_PTR | IR_STAMP_FRAME_TYPE);
            }
        }
    }
    LinRetSeen = 0;
    LinStepAbort = 0;
    LinDiv0 = 0;
    if (!was_nested)
        LightSp = 0;
    if (IrCallDepth > 0)
    {
        FrSnap = IrCallStack[IrCallDepth - 1];
        snap_n = FrSnap->NumLocals;
        if (snap_n < 0) snap_n = 0;
        if (snap_n > IR_CALL_MAX_LOCALS) snap_n = IR_CALL_MAX_LOCALS;
        /* Also cover slots that DECL_ARRAY / StoreLocal may touch beyond
         * the pre-declared NumLocals (some paths grow slots lazily). */
        if (snap_n < IR_CALL_MAX_LOCALS)
            snap_n = IR_CALL_MAX_LOCALS;
        memcpy(snap_val, FrSnap->LocalVal, (size_t)snap_n * sizeof(snap_val[0]));
        memcpy(snap_init, FrSnap->LocalInit, (size_t)snap_n * sizeof(snap_init[0]));
        have_snap = 1;
    }
    if (IrCallDepth <= 0)
    {
        /* Token-path nesting may zero depth; refuse linear rather than
         * indexing IrCallStack[-1] (659 stack-UAR under EXEC=vm). */
        if (pushed)
            LinFramePop();
        return 0;
    }
    /* The compact bytecode VM is opt-in until differential coverage is broad.
     * Its preflight is side-effect-free; rejection safely falls back to the
     * established linear executor before any instruction has run. */
    {
        const char *bytecode = getenv("BABYC_BYTECODE_VM");
        int use_bytecode = (BabycPathGet() == BABYC_PATH_BYTECODE)
            || (IrExecIsVmProd() && bytecode != NULL
                && bytecode[0] == '1' && bytecode[1] == '\0');
        if (!use_bytecode || !IrVmExecProd(S->Code, S->N,
                IrCallStack[IrCallDepth - 1]->LocalVal, IR_CALL_MAX_LOCALS))
            IrExecLinLocals(S->Code, S->N,
                IrCallStack[IrCallDepth - 1]->LocalVal, IR_CALL_MAX_LOCALS);
    }
    got_ret = LinRetSeen;
    bad = (LinStepAbort || LinDiv0);
    if (!bad) BabycFnCodeBodyRuns++;
    if (IrVmProfileOn())
    {
        if (bad) VmProfRunFail++;
        else
        {
            VmProfRuns++;
            if (was_nested) VmProfRunNested++;
            else VmProfRunTop++;
        }
    }
    if (bad && have_snap && FrSnap != NULL)
    {
        /* Discard partial linear effects so tree fallback starts clean.
         * Owned struct/array blobs: do NOT restore LocalVal from the pre-body
         * snapshot (usually 0). DECL allocated a real payload; rolling the
         * pointer back to 0 leaves LocalOwned=1 with a NULL blob (619: g.seen
         * after dfs while gp still holds the live address). Free the payload
         * and clear owned so tree Decl can re-allocate cleanly. */
        {
            int si;
            for (si = 0; si < snap_n && si < IR_CALL_MAX_LOCALS; si++)
            {
                if (FrSnap->LocalOwned[si] && FrSnap->LocalVal[si] != 0
                    && FrSnap->LocalVal[si] != snap_val[si]
                    && !FrSnap->LocalIsStatic[si])
                {
                    free((void *)(intptr_t)FrSnap->LocalVal[si]);
                    FrSnap->LocalVal[si] = 0;
                    IrFrameSetLocalOwned(FrSnap, si, 0);
                    if (FrSnap->LocalInitMap[si] != NULL)
                    {
                        free(FrSnap->LocalInitMap[si]);
                        FrSnap->LocalInitMap[si] = NULL;
                    }
                }
                else if (!FrSnap->LocalOwned[si])
                    FrSnap->LocalVal[si] = snap_val[si];
                FrSnap->LocalInit[si] = snap_init[si];
            }
        }
        /* Do not publish a partial return. */
        got_ret = 0;
        FrSnap->HasReturn = 0;
        /* Permanently demote this body: a shape that aborts at runtime must
         * not be retried on every call (would re-mutate then re-fallback). */
        if (S != NULL)
            S->Elig = 0;
    }
    else if (got_ret)
    {
        IrCallStack[IrCallDepth - 1]->ReturnVal = LinRetVal;
        IrCallStack[IrCallDepth - 1]->HasReturn = 1;
    }
    LinRetSeen = saved_ret;
    LinRetVal = saved_retval;
    LinStepAbort = saved_abort;
    LinDiv0 = saved_div0;
    if (was_nested)
        Parser->Mode = saved_mode;
    else if (got_ret)
        Parser->Mode = RunModeReturn;
    if (pushed)
        LinFramePop();
    else if (FnActive == NULL)
        FnActive = S;
    /* Restore prior LinParser. Leaving it as Parser is wrong when Parser was
     * a stack FuncParser of a returned token-path call (659 UAR). */
    LinParser = saved_lin_parser;
    return bad ? 0 : 1;
}

/* Pointer-parameter admission for the body-level linear executor.
 * Only admit parameters used as p[index] roots. Pointer values loaded as scalar
 * locals or explicit *p operations may require pointer arithmetic semantics
 * that this slice does not yet guarantee, so those bodies stay on the tree. */
/* Admit void* callback parameters only when each use is explicitly cast to
 * a typed pointer and immediately consumed by dereference, member access, or
 * indexing. This is general AST validation, not a comparator-name fast path. */
static int FnCodeVoidParamExprSafe(const IrExpr *E, int Param,
                                   const IrExpr *Parent,
                                   const IrExpr *Grandparent,
                                   int Depth, int *Seen)
{
    int i;
    if (E == NULL) return 1;
    if (Depth > 96) return 0;
    if (E->Kind == IrExIdent && E->ParamSlot == Param)
    {
        int context_ok = 0;
        *Seen = 1;
        if (Parent != NULL && Parent->Kind == IrExUnary
            && Parent->Op == TokenCast && Parent->Left == E
            && (Parent->TypeFlags & IR_TF_PTR) != 0
            && Parent->ElemSize > 0 && Grandparent != NULL)
        {
            if (Grandparent->Kind == IrExUnary
                && Grandparent->Op == TokenAsterisk
                && Grandparent->Left == Parent)
                context_ok = 1;
            else if (Grandparent->Kind == IrExMember
                     && Grandparent->Left == Parent
                     && Grandparent->Ident != NULL)
                context_ok = 1;
            else if (Grandparent->Kind == IrExIndex
                     && Grandparent->Left == Parent)
                context_ok = 1;
        }
        return context_ok;
    }
    if (!FnCodeVoidParamExprSafe(E->Left, Param, E, Parent, Depth + 1, Seen)
        || !FnCodeVoidParamExprSafe(E->Right, Param, E, Parent, Depth + 1, Seen))
        return 0;
    for (i = 0; E->Args != NULL && i < E->ArgCount; ++i)
        if (!FnCodeVoidParamExprSafe(E->Args[i], Param, E, Parent, Depth + 1, Seen))
            return 0;
    return 1;
}

static int FnCodeVoidParamStmtSafe(const IrStmt *S, int Param, int Depth,
                                   int *Seen)
{
    int i;
    if (S == NULL) return 1;
    if (Depth > 96) return 0;
    if (!FnCodeVoidParamExprSafe(S->Expr, Param, NULL, NULL, 0, Seen)
        || !FnCodeVoidParamStmtSafe(S->ThenStmt, Param, Depth + 1, Seen)
        || !FnCodeVoidParamStmtSafe(S->ElseStmt, Param, Depth + 1, Seen)
        || !FnCodeVoidParamStmtSafe(S->BodyStmt, Param, Depth + 1, Seen)
        || !FnCodeVoidParamStmtSafe(S->InitStmt, Param, Depth + 1, Seen)
        || !FnCodeVoidParamStmtSafe(S->IncStmt, Param, Depth + 1, Seen))
        return 0;
    for (i = 0; S->Stmts != NULL && i < S->StmtCount; ++i)
        if (!FnCodeVoidParamStmtSafe(S->Stmts[i], Param, Depth + 1, Seen))
            return 0;
    return 1;
}

static int FnCodeVoidParamTypedAccessSafe(const FnCodeSlot *S, int Param)
{
    int Seen = 0;
    return S != NULL && FnCodeVoidParamStmtSafe(S->Body, Param, 0, &Seen) && Seen;
}

static int FnCodeIndexedPointerParamsSafe(const FnCodeSlot *S,
                                          const IrCallFrame *Fr)
{
    int p, i;
    if (S == NULL || Fr == NULL) return 0;
    for (p = 0; p < Fr->NumParams && p < IR_CALL_MAX_PARAMS; p++)
    {
        int used = 0;
        int referenced = 0;
        if (!Fr->ParamIsPtr[p]) continue;
        /* An unused pointer parameter cannot make the body unsafe.  In
         * particular, main(argc, argv) commonly leaves argv untouched; the
         * old gate rejected that body merely because argv had pointer type.
         * Require the stricter pointee/index checks only when the lowered
         * body actually reads the parameter or records an indexed access. */
        for (i = 0; i < S->N; i++)
        {
            const IrLinInst *In = &S->Code[i];
            if (In->Op == IrLinLoadLocal
                && In->Imm == -(long long)(p + 1))
            {
                referenced = 1;
                break;
            }
            if ((In->Op == IrLinLoadIndex || In->Op == IrLinStoreIndex
                 || In->Op == IrLinLoadMemberArray || In->Op == IrLinStoreMemberArray)
                && In->Imm >= 0 && In->Imm < S->IdxN
                && S->IdxMetaSlot[In->Imm] == p
                && (S->IdxMetaKind[In->Imm] == 2 || S->IdxMetaKind[In->Imm] == 4))
            {
                referenced = 1;
                break;
            }
        }
        if (!referenced) continue;
        if (Fr->ParamTyp[p] == NULL || Fr->ParamTyp[p]->Base != TypePointer
            || Fr->ParamTyp[p]->FromType == NULL) return 0;
        if (Fr->ParamTyp[p]->FromType->Base == TypeVoid)
        {
            if (!FnCodeVoidParamTypedAccessSafe(S, p)) return 0;
            continue;
        }
        switch (Fr->ParamTyp[p]->FromType->Base)
        {
            case TypeInt: case TypeShort: case TypeChar: case TypeLong:
            case TypeLongLong: case TypeUnsignedInt: case TypeUnsignedShort:
            case TypeUnsignedChar: case TypeUnsignedLong: case TypeUnsignedLongLong: break;
            /* char** / T**: allowed when every Index on this param is kind-4
             * (argv[i] -> void*). Nested p[0][0] must lower via kind-4 + Tok=2;
             * bare *p of char** still refuses (not kind-4 Index). */
            case TypePointer: break;
            default: return 0;
        }
        for (i = 0; i < S->N; i++)
        {
            const IrLinInst *In = &S->Code[i];
            if (In->Op == IrLinLoadLocal && In->Imm == -(long long)(p + 1))
            {
                int r = (int)In->Dest, j, forwarded = 0;
                if (r >= IR_LIN_REGS) return 0;
                /* Validate the live range: lowering reuses registers, so a
                 * later definition of r is unrelated to this pointer value. */
                for (j = i + 1; j < S->N; j++)
                {
                    const IrLinInst *Use = &S->Code[j];
                    int reads = 0, writes_dest = 0, ai;
                    switch (Use->Op)
                    {
                        case IrLinImm: case IrLinLoadLocal: case IrLinBin:
                        case IrLinUnary: case IrLinLoadGlobal: case IrLinLoadIndex: case IrLinLoadMemberArray:
                        case IrLinCall: case IrLinAddI: case IrLinAddLocal: case IrLinAddStoreLocal:
                            writes_dest = 1; break;
                        case IrLinAddRegLocal:
                            writes_dest = (Use->Dest == r || Use->A == r); break;
                        default: break;
                    }
                    switch (Use->Op)
                    {
                        case IrLinBin:
                            reads = (Use->A == r || Use->B == r);
                            /* Pointer-to-pointer indexing lowers a parameter base
                             * into address arithmetic before the typed indirect
                             * load. Permit only the emitter's explicit wide
                             * pointer-plus-offset form; arbitrary arithmetic on a
                             * parameter pointer remains outside this proof. */
                            if (reads && Use->Tok == TokenPlus
                                && (Use->Imm & IR_LIN_F_WIDE))
                                reads = 0;
                            break;
                        case IrLinUnary: case IrLinBrZ: case IrLinRet:
                        case IrLinStoreGlobal: case IrLinStoreLocal:
                        case IrLinAddI: reads = (Use->A == r); break;
                        case IrLinAddRegLocal: reads = (Use->A == r); break;
                        case IrLinBrLT: case IrLinBrCmp: reads = (Use->A == r || Use->B == r); break;
                        case IrLinLoadIndex:
                            /* A = index or base depending on shape; pointer base is OK. */
                            reads = 0;
                            break;
                        case IrLinLoadMemberArray:
                            reads = (Use->A == r);
                            break;
                        case IrLinStoreIndex:
                            reads = 0; /* value/index/base may use r */
                            break;
                        case IrLinStoreMemberArray: case IrLinStoreDeref:
                            reads = (Use->A == r || Use->B == r);
                            break;
                        case IrLinAddStoreLocal: reads = (Use->B == r); break;
                        case IrLinCopyIndex: reads = (Use->A == r || Use->B == r); break;
                        case IrLinCall:
                        {
                            int ci = (int)Use->Imm;
                            if (ci >= 0 && ci < S->CallN)
                                for (ai = 0; ai < S->CallArgN[ci]; ai++)
                                    if (S->CallArgReg[ci][ai] == r)
                                    {
                                        struct Value *CF = S->CallFnTab[ci];
                                        struct ValueType *CT = NULL, *CE = NULL;
                                        struct ValueType *PE = Fr->ParamTyp[p]->FromType;
                                        if (CF != NULL && CF->Val != NULL
                                            && CF->Val->FuncDef.Intrinsic == NULL
                                            && ai < CF->Val->FuncDef.NumParams
                                            && CF->Val->FuncDef.ParamType != NULL)
                                            CT = CF->Val->FuncDef.ParamType[ai];
                                        if (CT != NULL && CT->Base == TypePointer)
                                            CE = CT->FromType;
                                        if (Use->Tok != 0 || CF != S->Func || ai != p
                                            || CE == NULL || CE->Base != PE->Base
                                            || CE->Sizeof != PE->Sizeof)
                                            return 0;
                                        forwarded = 1;
                                    }
                            break;
                        }
                        default: break;
                    }
                    if (reads) return 0;
                    if (forwarded) break;
                    if (writes_dest && Use->Dest == r) break;
                }
                /* Pointer reg may be only used as Index base (qsort/heapsort)
                 * without a recursive forward — that is safe. */
                (void)forwarded;
            }
            /* kind 2 scalar p[i]; kind 4 pointer-to-pointer argv[i]. */
            if ((In->Op == IrLinLoadIndex || In->Op == IrLinStoreIndex
              || In->Op == IrLinLoadMemberArray || In->Op == IrLinStoreMemberArray)
                && In->Imm >= 0 && In->Imm < S->IdxN
                && S->IdxMetaSlot[In->Imm] == p
                && (S->IdxMetaKind[In->Imm] == 2 || S->IdxMetaKind[In->Imm] == 4))
                used = 1;
        }
        if (!used) return 0;
    }
    return 1;
}

/* Body-level VM entry (plan §2): integer-eligible body → linear/VM.
 * Falls back to tree by returning 0. */
static int VmTraceOn(void)
{
    static int On = -1;
    if (On < 0)
        On = (getenv("BABYC_VM_TRACE") != NULL) ? 1 : 0;
    return On;
}

int IrTryExecBodyVm(struct ParseState *Parser, IrStmt *Body)
{
    FnCodeSlot *S;
    int was_nested;
    int prof = IrVmProfileOn();
    if (VmTraceOn())
        fprintf(stderr, "[vm-try] body=%p depth=%d kind=%d\n",
            (void*)Body, IrCallDepth, Body?Body->Kind:-1);
    if (Parser == NULL || Body == NULL)
        return 0;
    if (!IrExecIsVmProd())
    { if (VmTraceOn()) fprintf(stderr, "[vm-rej] not-prod\n"); return 0; }
    if (prof) VmProfAttempts++;
    if (IrCallDepth <= 0 || IrCallDepth > IR_CALL_MAX_DEPTH)
    {
        if (prof) VmProfRejectDepth++;
        if (VmTraceOn()) fprintf(stderr, "[vm-rej] depth\n");
        return 0;
    }

    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int pi;
        for (pi = 0; pi < Fr->NumParams && pi < IR_CALL_MAX_PARAMS; pi++)
        {
            /* Reject unsupported pointee types before lowering. Besides being
             * conservative for pointer-to-pointer and array pointers, this avoids leaving
             * partially lowered metadata behind for bodies that must fall back. */
            if (Fr->ParamIsPtr[pi])
            {
                struct ValueType *PT = Fr->ParamTyp[pi];
                enum BaseType eb;
                if (PT == NULL || PT->Base != TypePointer || PT->FromType == NULL)
                {
                    if (prof) VmProfRejectPtr++;
                    if (VmTraceOn()) fprintf(stderr, "[vm-rej] ptr-type\n");
                    return 0;
                }
                eb = PT->FromType->Base;
                /* char** / T** pointer params are admitted on Linear/bytecode
                 * only when used by a safe kind-4 Index access; the
                 * FnCodeIndexedPointerParamsSafe proof is independent of STRICT.
                 * Scalar pointees and selected struct/union/array forms have
                 * dedicated paths. FP pointees remain outside this gate. */
                if (!(eb == TypeInt || eb == TypeShort || eb == TypeChar
                      || eb == TypeLong || eb == TypeLongLong
                      || eb == TypeUnsignedInt || eb == TypeUnsignedShort
                      || eb == TypeUnsignedChar || eb == TypeUnsignedLong
                      || eb == TypeUnsignedLongLong
                      || eb == TypeStruct || eb == TypeUnion || eb == TypeArray
                      || eb == TypeVoid
                      || (IrVmLinearFeaturesOn() && eb == TypePointer)))
                {
                    if (prof) VmProfRejectPtr++;
                    return 0;
                }
            }
            /* char/short params are lowered; narrowing is handled by
             * StoreLocal/AddLocal. Rejecting them forced a whole-body tree
             * fallback (vm_strict coverage gap). */
        }
    }

    was_nested = (LinExecDepth > 0);
    S = FnCodeFind(Body);
    if (S == NULL || (S->N <= 0 && S->Elig != 0))
    {
        /* Nested compile is OK: FnCodeCompile saves/restores live tables. */
        S = FnCodeCompile(Parser, Body,
            (IrCallDepth > 0 && IrCurrentFunc[IrCallDepth - 1] != NULL
             && IrCurrentFunc[IrCallDepth - 1]->Val != NULL
             && IrCurrentFunc[IrCallDepth - 1]->Val->FuncDef.BodyIr == Body)
                ? IrCurrentFunc[IrCallDepth - 1] : NULL);
        if (S == NULL)
        {
            if (prof) VmProfCompileFail++;
            if (VmTraceOn()) fprintf(stderr, "[vm-rej] compile-null\n");
            return 0;
        }
    }
    else if (S->Elig != 1 || S->N <= 0)
    {
        if (prof) VmProfIneligible++;
        if (VmTraceOn()) fprintf(stderr, "[vm-rej] ineligible elig=%d n=%d\n", S->Elig, S->N);
        return 0;
    }

    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int has_ptr = 0, pi;
        for (pi = 0; pi < Fr->NumParams && pi < IR_CALL_MAX_PARAMS; pi++)
            if (Fr->ParamIsPtr[pi]) { has_ptr = 1; break; }
        if (has_ptr)
        {
            /* The lowered body and declared parameter types are immutable;
             * cache the safe/fallback decision rather than scanning live
             * ranges on every call to a hot pointer function. */
            if (S->PtrIndexedElig < 0)
                S->PtrIndexedElig = FnCodeIndexedPointerParamsSafe(S, Fr) ? 1 : 0;
            if (!S->PtrIndexedElig)
            {
                if (prof) VmProfRejectPtr++;
                if (VmTraceOn()) fprintf(stderr, "[vm-rej] ptr-indexed\n");
                return 0;
            }
            if (prof) VmProfPtrIndexedAdmit++;
        }
    }

    /* Do not route tiny bodies to Tree. STRICT is a fallback policy, not a
     * semantic/path selector; all production Linear bodies must use FnCode. */

    return FnCodeRun(Parser, S, was_nested);
}


/* AllocReg → ir_lin_emit.c */

void IrLowerRestoreGlobs(void)
{
    int i;
    if (LinParser == NULL) { GlobSaved = 0; IdxDirty = 0;
    memset(LinWideOn, 0, sizeof(LinWideOn)); return; }
    if (GlobSaved)
    {
        for (i = 0; i < GlobN; i++)
        {
            struct Value *V = IrGlobalVal(LinParser, GlobTab[i]);
            if ((GlobDirty & (1u << i)) && V && V->Val)
                (void)IrValueStoreBits(V, V->Typ ? V->Typ->Base : TypeInt, GlobSave[i]);
        }
        GlobSaved = 0;
        GlobDirty = 0;
    }
    if (IdxDirty)
    {
        for (i = 0; i < IdxN; i++)
            if ((IdxDirty & (1u << i)) && IdxAddr[i])
            {
                int eb = IdxEb[i];
                if (eb == 0)
                {
                    if (IdxEs[i] == 1) eb = TypeChar;
                    else if (IdxEs[i] == 2) eb = TypeShort;
                    else eb = TypeInt;
                }
                IrStorePtrElem(LinParser, eb, IdxAddr[i], IdxSave[i]);
            }
    }
    IdxDirty = 0;
}

/* Tok=2 pointee load: In->B==1 unsigned; In->B>=2 -> param slot (B-2), signedness from live frame. */
int LinTok2Unsigned(const IrLinInst *In)
{
    if (In->B == 1) return 1;
    if (In->B >= 2 && IrCallDepth > 0 && (In->B - 2) < IR_CALL_MAX_PARAMS)
    {
        struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[In->B - 2];
        if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL
            && IrTypeIsUnsignedBase(PT->FromType->Base))
            return 1;
    }
    return 0;
}

/* Emit → ir_lin_emit.c */

/* LINE markers: always emit so production VM diagnostics (div0/uninit)
 * have correct Parser->Line. Cheap (one inst per stmt with SourceLine). */
/* EmitLine → ir_lin_emit.c */

/* IsRel → ir_lin_emit.c */

/* Distinguish a true array-of-pointers row from an ordinary multidimensional
 * array whose row byte-size happens to equal sizeof(void *). ElemSize is a
 * stride, not a type tag: int a[2][2] has 8-byte rows on 32-bit targets and
 * 8-byte rows only in some layouts on others. Strict mode must inspect the
 * declaration before routing nested indexing through the pointer-array path. */
/* IrLowerIndexRowIsPointer → ir_lin_ptr.c */

/* Recover a missing aggregate Sizeof from the complete member table. This
 * fallback is used only for pointer arithmetic when the type node is stale. */
/* IrLowerInferAggregateSize → ir_lin_ptr.c */

/* LowerRec → ir_lin_emit.c */


/* Produce the address of a local aggregate element for a complete index chain.
 * The compiler's dimension metadata is authoritative: flatten row-major indices
 * first, then scale once by the leaf aggregate size. */
/* LowerLocalStructArrayAddress → ir_lin_ptr.c */

/* Produce the address of a global struct/union array element for a complete
 * row-major index chain. Do not scale each intermediate index by the leaf
 * size: flatten indices using the declared extents, then scale once. */
/* LowerGlobalStructArrayAddress → ir_lin_ptr.c */

int IrLowerExpr(IrExpr *E, IrLinInst *Code, int Cap, int *NOut, int *DestOut)
{
    int next = 0, n = 0, dest = 0;
    int nested = (LowerDepth > 0 || CallN > 0 || IdxN > 0 || GlobN > 0);
    int pushed = 0;
    if (nested)
        pushed = CompileFramePush();
    LowerDepth++;
    CallN = 0;
    if (!LowerRec(E, Code, Cap, &n, &next, &dest))
    {
        LowerDepth--;
        if (pushed)
            CompileFramePop();
        return 0;
    }
    *NOut = n;
    *DestOut = dest;
    LowerDepth--;
    if (pushed)
        CompileFramePop();
    return 1;
}

/* LinLoop/Lab/CompileFrame + LowerStmtRec + IrLowerStmt → ir_lin_compile.c */

static long long Rel(enum LexToken Op, long long L, long long R)
{
    switch (Op)
    {
        case TokenEqual: return L == R;
        case TokenNotEqual: return L != R;
        case TokenLessThan: return L < R;
        case TokenGreaterThan: return L > R;
        case TokenLessEqual: return L <= R;
        case TokenGreaterEqual: return L >= R;
        default: return 0;
    }
}
static long long RelU(enum LexToken Op, long long L, long long R)
{
    unsigned long long UL = (unsigned long long)L;
    unsigned long long UR = (unsigned long long)R;
    switch (Op)
    {
        case TokenEqual: return UL == UR;
        case TokenNotEqual: return UL != UR;
        case TokenLessThan: return UL < UR;
        case TokenGreaterThan: return UL > UR;
        case TokenLessEqual: return UL <= UR;
        case TokenGreaterEqual: return UL >= UR;
        default: return 0;
    }
}

/* Analysis §10.5: after LoadLocal/LoadIndex, remember UNSIGNED on the dest reg
 * so a subsequent Bin in the same linear block can zero-extend even if Imm
 * flag was missing (annotate gap on a parent BinOp). */
unsigned char LinRegU[IR_LIN_REGS];


/* IrCallHostFromRegs: defined in ir_call.c */
long long IrCallHostFromRegs(struct ParseState *, struct Value *, long long *, int, const unsigned char *, const unsigned char *);

/* Compact VM's deliberately narrow terminal-call bridge. Only the VM
 * preflight-approved printf tail reaches this function; all argument packing
 * stays shared with the established linear executor. */
long long IrLowerCallHostProd(int ci, long long *Regs)
{
    long long args[IR_LIN_CALL_ARGS];
    struct Value *FV;
    int ai, na;
    if (ci < 0 || ci >= CallN || Regs == NULL || LinParser == NULL)
        return 0;
    FV = CallFnTab[ci];
    na = CallArgN[ci];
    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val == NULL || FV->Val->FuncDef.Intrinsic == NULL
        || na < 0 || na > IR_LIN_CALL_ARGS)
        return 0;
    for (ai = 0; ai < na; ai++)
    {
        args[ai] = Regs[CallArgReg[ci][ai]];
        if (CallStrOn[ci][ai])
            args[ai] = CallStrBits[ci][ai];
    }
    LinHostCallName = CallNameTab[ci];
    if (CallCharPos[ci] > 0)
        LinParser->CharacterPos = CallCharPos[ci];
    {
        long long result = IrCallHostFromRegs(LinParser, FV, args, na, CallArgIsPtr[ci], CallArgIsFp[ci]);
        LinHostCallName = NULL;
        return result;
    }
}

/* Narrow production-VM bridge for terminal user calls.  Only fixed-arity
 * integer scalar signatures are admitted; pointer, FP, aggregate, variadic,
 * and string-packed arguments remain on the established executor. */
static int IrLowerVmScalarIntType(const struct ValueType *T)
{
    if (T == NULL) return 0;
    switch (T->Base)
    {
    case TypeChar: case TypeUnsignedChar:
    case TypeShort: case TypeUnsignedShort:
    case TypeInt: case TypeUnsignedInt:
    case TypeLong: case TypeUnsignedLong:
    case TypeLongLong: case TypeUnsignedLongLong:
        return 1;
    default:
        return 0;
    }
}

/* Runtime VM helper: a frame entered through the established tree-call path
 * may not yet have its scalar local type metadata copied from FnCode.  Resolve
 * that metadata from the per-function compiled slot table without weakening
 * the VM's type/ownership checks. */
/* IrLowerCallLocalType → ir_lin_ptr.c */

static int IrAggregateExprReadOnly(const IrExpr *e, int depth)
{
    if (e == NULL || depth > 64) return e == NULL;
    switch (e->Kind)
    {
    case IrExConst: return 1;
    case IrExIdent: return e->LocalSlot >= 0 || e->ParamSlot >= 0;
    case IrExMember:
        return IrAggregateExprReadOnly(e->Left, depth + 1);
    case IrExBinOp:
        return IrAggregateExprReadOnly(e->Left, depth + 1)
            && IrAggregateExprReadOnly(e->Right, depth + 1);
    case IrExUnary:
        return e->Op != TokenIncrement && e->Op != TokenDecrement
            && e->Op != TokenAsterisk && e->Op != TokenAmpersand
            && IrAggregateExprReadOnly(e->Left, depth + 1);
    default: return 0; /* calls, assignments, indexing with effects, ternary */
    }
}

static int IrAggregateLocalMemberLvalue(const IrExpr *e)
{
    const IrExpr *r = e;
    if (r == NULL || r->Kind != IrExMember) return 0;
    while (r != NULL && r->Kind == IrExMember) r = r->Left;
    return r != NULL && r->Kind == IrExIdent && r->LocalSlot >= 0;
}

static int IrAggregatePureStmt(const IrStmt *s, int depth)
{
    int i;
    if (s == NULL || depth > 64) return s == NULL;
    switch (s->Kind)
    {
    case IrStBlock:
        if (s->StmtCount > 0 && s->Stmts == NULL) return 0;
        for (i = 0; i < s->StmtCount; ++i)
            if (!IrAggregatePureStmt(s->Stmts[i], depth + 1)) return 0;
        return 1;
    case IrStDecl:
        return s->LocalSlot >= 0 && !s->DeclIsPtr && !s->DeclIsStatic
            && s->DeclArraySize <= 1 && s->DeclInitExprs == NULL
            && s->DeclInitVals == NULL && s->DeclFromType != NULL
            && (s->DeclFromType->Base == TypeStruct || s->DeclFromType->Base == TypeUnion);
    case IrStExpr:
        return s->Expr != NULL && s->Expr->Kind == IrExAssign
            && (s->Expr->Op == 0 || s->Expr->Op == TokenAssign)
            && IrAggregateLocalMemberLvalue(s->Expr->Left)
            && IrAggregateExprReadOnly(s->Expr->Right, 0);
    case IrStReturn:
        return s->Expr != NULL && s->Expr->Kind == IrExIdent
            && s->Expr->LocalSlot >= 0;
    default: return 0; /* no branches, loops, globals, I/O or nested calls */
    }
}

/* Struct-returning call bridge is permitted only for a deliberately tiny
 * pure constructor shape: local aggregate declaration, field assignments
 * from scalar parameters/constants, and return of that local aggregate.
 * It can safely be replayed if a later compact instruction aborts. */
int IrLowerCallAggregatePureProd(int ci)
{
    struct Value *FV;
    struct ValueType *rt;
    int ai, na, np;
    if (ci < 0 || ci >= CallN) return 0;
    FV = CallFnTab[ci];
    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val == NULL || FV->Val->FuncDef.Intrinsic != NULL
        || FV->Val->FuncDef.BodyIr == NULL || FV->Val->FuncDef.VarArgs
        || FV->Val->FuncDef.ParamType == NULL) return 0;
    rt = FV->Val->FuncDef.ReturnType;
    if (rt == NULL || (rt->Base != TypeStruct && rt->Base != TypeUnion)
        || rt->Sizeof <= 0 || rt->Sizeof > 512) return 0;
    na = CallArgN[ci];
    np = FV->Val->FuncDef.NumParams;
    if (na != np || na < 0 || na > IR_LIN_CALL_ARGS) return 0;
    for (ai = 0; ai < na; ++ai)
        if (!IrLowerVmScalarIntType(FV->Val->FuncDef.ParamType[ai])
            || CallArgIsPtr[ci][ai] || CallArgIsFp[ci][ai] || CallStrOn[ci][ai]
            || CallArgReg[ci][ai] < 0 || CallArgReg[ci][ai] >= IR_LIN_REGS) return 0;
    return IrAggregatePureStmt((const IrStmt *)FV->Val->FuncDef.BodyIr, 0);
}

int IrLowerCallUserSafeProd(int ci)
{
    struct Value *FV;
    int ai, na, np;
    if (ci < 0 || ci >= CallN) return 0;
    FV = CallFnTab[ci];
    na = CallArgN[ci];
    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
        || FV->Val == NULL || FV->Val->FuncDef.Intrinsic != NULL
        || FV->Val->FuncDef.BodyIr == NULL || FV->Val->FuncDef.VarArgs
        || FV->Val->FuncDef.ParamType == NULL
        || !IrLowerVmScalarIntType(FV->Val->FuncDef.ReturnType))
        return 0;
    np = FV->Val->FuncDef.NumParams;
    if (na != np || na < 0 || na > IR_LIN_CALL_ARGS)
        return 0;
    /* Admit only scalar callees whose lowered body does not require ordinary
     * local slots. Per-depth VM contexts now isolate bytecode and transactional
     * state; nested-call/recursion eligibility remains subject to the runtime
     * preflight performed before the callee executes. */
    {
        FnCodeSlot *CS = FnCodeFindByFunc(FV);
        if ((CS == NULL || CS->N <= 0) && LinParser != NULL
            && FV->Val->FuncDef.BodyIr != NULL)
            CS = FnCodeCompile(LinParser, (IrStmt *)FV->Val->FuncDef.BodyIr, FV);
        if (CS == NULL || CS->N <= 0 || CS->Elig != 1)
            return 0;
        /* This predicate is consumed by the compact-bytecode terminal-call
         * executor, not by Linear VM's IrExecLinLocals call dispatcher. Keep
         * this gate scoped to compact preflight; Linear's local-slot support
         * and hot path are independent of this bridge. */
        if (CS->MaxLocal > 0)
        {
            const char *bytecode = getenv("BABYC_BYTECODE_VM");
            const char *path = getenv("BABYC_PATH");
            if (!((bytecode != NULL && bytecode[0] == '1' && bytecode[1] == '\0')
                  || (path != NULL && strcmp(path, "bytecode") == 0)))
                return 0;
        }
        /* Per-depth VM contexts isolate the caller while a callee executes.
         * Nested call sites are checked by VmTerminalCallSafe in the callee
         * before its first instruction. */
        /* Global/indexed stores and increments are legitimate effects of
             * a terminal scalar callee. VmTerminalCallSafe preflights the
             * complete caller tail before execution and marks the call as the
             * commit point, so these effects cannot be followed by a fallback
             * that replays the call. Rejecting them here hid the bridge's
             * actual limitation instead of proving the no-replay invariant. */
    }
    for (ai = 0; ai < na; ai++)
    {
        if (!IrLowerVmScalarIntType(FV->Val->FuncDef.ParamType[ai])
            || CallArgIsPtr[ci][ai] || CallArgIsFp[ci][ai] || CallStrOn[ci][ai]
            || CallArgReg[ci][ai] < 0 || CallArgReg[ci][ai] >= IR_LIN_REGS)
            return 0;
    }
    return 1;
}

long long IrLowerCallUserProd(int ci, long long *Regs)
{
    long long args[IR_LIN_CALL_ARGS];
    long long result;
    struct Value *FV;
    FnCodeSlot *Caller = FnActive;
    int ai, na;
    if ((!IrLowerCallUserSafeProd(ci) && !IrLowerCallAggregatePureProd(ci))
        || Regs == NULL || LinParser == NULL)
        return 0;
    FV = CallFnTab[ci];
    na = CallArgN[ci];
    /* Both admission predicates require a non-variadic callee with
     * na == NumParams, so every slot is typed by the prototype (FixedTypes)
     * and no per-argument ABI types are needed. Enforce that invariant. */
    if (FV->Val->FuncDef.VarArgs || na != FV->Val->FuncDef.NumParams)
        ProgramFail(LinParser, "internal: untyped variadic call arguments");
    for (ai = 0; ai < na; ai++)
        args[ai] = Regs[CallArgReg[ci][ai]];
    result = IrCallUserFuncCached(LinParser, FV, args, NULL, na);
    /* A nested body installs its own Call/Glob/Idx/WideImm side tables.
     * The caller's bytecode still refers to its original call-slot indices;
     * restore those tables before the next caller instruction is dispatched. */
    if (Caller != NULL && FnActive != Caller)
        FnCodeInstall(Caller);
    return result;
}



/* Conservative linear liveness check used by memory-copy fusion.  It rejects
 * control-flow crossings and any unknown instruction before the source value
 * is overwritten, so removing the load cannot remove a live register value. */
int IrLinRegDeadAfter(const IrLinInst *Code, int N, int Start, int Reg)
{
    int i;
    for (i = Start; i < N; i++)
    {
        const IrLinInst *In = &Code[i];
        int reads = 0, writes = 0, a;
        switch (In->Op)
        {
            case IrLinImm: case IrLinLoadLocal: case IrLinLoadGlobal:
            case IrLinAddLocal:
                writes = (In->Dest == Reg); break;
            case IrLinAddStoreLocal:
                reads = (In->B == Reg); writes = (In->Dest == Reg); break;
            case IrLinLoadIndex: case IrLinLoadMemberArray:
                reads = (In->A == Reg); writes = (In->Dest == Reg); break;
            case IrLinAddI:
                reads = (In->A == Reg); writes = (In->Dest == Reg); break;
            case IrLinAddRegLocal:
                reads = (In->A == Reg); writes = (In->Dest == Reg || In->A == Reg); break;
            case IrLinBin:
                reads = (In->A == Reg || In->B == Reg); writes = (In->Dest == Reg); break;
            case IrLinUnary:
                reads = (In->A == Reg); writes = (In->Dest == Reg); break;
            case IrLinCopyIndex:
                reads = (In->A == Reg || In->B == Reg); writes = (In->Dest == Reg); break;
            case IrLinStoreLocal: case IrLinStoreGlobal:
                reads = (In->A == Reg); break;
            case IrLinStoreIndex: case IrLinStoreMemberArray: case IrLinStoreDeref:
                reads = (In->A == Reg || In->B == Reg); break;
            case IrLinCall:
                writes = (In->Dest == Reg);
                { int ci = (int)In->Imm;
                  if (ci >= 0 && ci < CallN)
                      for (a = 0; a < CallArgN[ci] && a < IR_LIN_CALL_ARGS; a++)
                          if (CallArgReg[ci][a] == Reg) reads = 1; }
                break;
            case IrLinLine: break;
            case IrLinJmp: case IrLinBrZ: case IrLinBrLT: case IrLinBrCmp: case IrLinRet:
                return 0; /* control flow makes straight-line liveness ambiguous */
            default:
                return 0;
        }
        if (reads) return 0;
        if (writes) return 1;
    }
    return 1;
}

/* Peephole: shrink hot patterns for fib-like bodies. */
/* IrLinPeephole → extracted */



/* Ultra-light body runner: handles locals, globals, self-calls without Step. */


/* LightRunBody / LightCallSelf / IrTryKernelForLight → ir_lin_light.c */



/* Copy sz bytes into owned struct/union LocalVal[slot]; mark init map full. */
void IrLinStructAssignBlob(IrCallFrame *Fr, int slot, void *Src, int sz)
{
    void *Dst;
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS || Src == NULL || sz <= 0)
        return;
    Dst = (void *)(intptr_t)Fr->LocalVal[slot];
    if (Dst == NULL || Dst == Src)
        return;
    memcpy(Dst, Src, (size_t)sz);
    Fr->LocalInit[slot] = 1;
    if (Fr->LocalInitMap[slot] != NULL && IrFrameInitMapLen(Fr, slot) >= sz)
        memset(Fr->LocalInitMap[slot], 1, (size_t)sz);
}

char IrLinStructRetBuf[512];

/* Step → ir_lin_exec.c */

/* Narrow bridge for the compact VM: reuse the canonical kernel executor so
 * local-array declarations install exactly the same ownership/type/init-map
 * metadata as the linear interpreter. Caller must preflight a side-effect-free
 * scalar array declaration before entering this bridge. */
int IrLowerVmExecArrayDecl(const IrLinInst *In, long long *Regs, long long *Locals, int NLocals)
{
    int ip = 0, saved_abort = LinStepAbort, saved_div0 = LinDiv0;
    int ok;
    if (In == NULL || In->Op != IrLinKernel
        || (In->Tok != IR_K_DECL_ARRAY && In->Tok != IR_K_DECL_STRUCT)
        || Regs == NULL || Locals == NULL || NLocals < 0)
        return 0;
    LinStepAbort = 0;
    LinDiv0 = 0;
    Step(In, Regs, Locals, NLocals, &ip);
    ok = !LinStepAbort && !LinDiv0;
    LinStepAbort = saved_abort;
    LinDiv0 = saved_div0;
    return ok;
}


/* Check/mark direct pointer accesses against frame-owned initialization maps.
 * This is shared by the parameter-index fast path, which otherwise bypasses
 * Step() and therefore would skip the same checks used by ordinary loads. */
/* IrLinCheckOwnedPointerAccess → extracted */

#if defined(_MSC_VER)
#define IR_PARAM_INDEX_INLINE __forceinline
#elif defined(__GNUC__)
#define IR_PARAM_INDEX_INLINE inline __attribute__((always_inline))
#else
#define IR_PARAM_INDEX_INLINE inline
#endif
/* LinTryParamIndexedAccess → extracted */
#undef IR_PARAM_INDEX_INLINE

/* Execute the fused indexed copy with the same direct-access helper as the
 * original load/store pair. If either access cannot use the direct path (for
 * example a null pointer requiring the normal diagnostic path), fall back to
 * Step for that synthetic original instruction. */
/* LinExecIndexCopy → ir_lin_exec.c */

long long IrExecLin(struct ParseState *Parser, const IrLinInst *Code, int N, int Dest)
{
    long long R[IR_LIN_REGS];
    long long Locals[IR_CALL_MAX_LOCALS];
    int i, ip, steps;
    long long ret;
    (void)Parser;
    for (i = 0; i < IR_LIN_REGS; i++)
    {
        R[i] = 0;
        LinRegU[i] = 0;
    }
    for (i = 0; i < IR_CALL_MAX_LOCALS; i++)
        Locals[i] = 0;
    if (IrCallDepth > 0)
        memcpy(Locals, IrCallStack[IrCallDepth - 1]->LocalVal, sizeof(Locals));
    LinExecDepth++; BabycLinExecDepth = LinExecDepth;
    for (ip = 0, steps = 0; ip >= 0 && ip < N && steps < 500000000; steps++)
        Step(&Code[ip], R, Locals, IR_CALL_MAX_LOCALS, &ip);
    LinExecDepth--; BabycLinExecDepth = LinExecDepth;
    if (Dest >= 0 && Dest < IR_LIN_REGS)
        ret = R[Dest];
    else
        ret = 0;
    return ret;
}

/* IrExecLinLocals → ir_lin_exec.c */

void IrPrintLowerStats(void)
{
    const char *e = getenv("BABYC_COV");
    unsigned long long total, lin = BabycLinInstExec;
    unsigned long long tree = BabycTreeStmtExec + BabycTreeExprExec;
    if (e == NULL || e[0] == '\0' || e[0] == '0')
        return;
    total = lin + tree;
    /* A Tree-body fallback is itself meaningful coverage even if no
     * statement/expression counter was ticked. Never hide it from the
     * BABYC_COV no-fallback gate just because all other counters are zero. */
    if (lin == 0 && tree == 0 && BabycFnCodeBodyRuns == 0
        && BabycTreeBodyRuns == 0)
        return;
    (void)total;
    fprintf(stderr,
        "[babyc-cov] lin_inst=%llu tree_stmt=%llu tree_expr=%llu "
        "fncode_body=%llu tree_body=%llu\n",
        (unsigned long long)lin, (unsigned long long)BabycTreeStmtExec,
        (unsigned long long)BabycTreeExprExec,
        (unsigned long long)BabycFnCodeBodyRuns,
        (unsigned long long)BabycTreeBodyRuns);
    if (IrVmProfileOn())
    {
        fprintf(stderr,
            "[babyc-exec-profile] attempts=%llu body_runs=%llu body_run_fail=%llu "
            "reject_depth=%llu reject_ptr=%llu reject_small=%llu "
            "compile_fail=%llu ineligible=%llu tiny_tree=%llu "
            "run_nested=%llu run_top=%llu ptr_index_admit=%llu local_add_fuse=%llu index_copy_fuse=%llu\n",
            VmProfAttempts, VmProfRuns, VmProfRunFail,
            VmProfRejectDepth, VmProfRejectPtr, VmProfRejectSmall,
            VmProfCompileFail, VmProfIneligible, VmProfTinyTree,
            VmProfRunNested, VmProfRunTop, VmProfPtrIndexedAdmit, VmProfFuseLocalAdd, VmProfFuseIndexCopy);
    }
}


int IrHookOn;
#if defined(__GNUC__)
__attribute__((constructor))
#endif
void IrHookInit(void) {
    IrLowerEnabledInit();
    /* Hook intercepts EVERY stmt — costly on sieve hot loops.
     * Enable only when shadow/compare (legacy EXEC=both), explicit BABYC_LOWER=1,
     * or debug. Linear/bytecode prod uses body-level lower at IrCall entry. */
    const char *x = getenv("BABYC_EXEC");
    const char *e = getenv("BABYC_LOWER");
    int lower1 = (e != NULL && e[0] == '1' && e[1] == '\0');
    int both = (x != NULL && strcmp(x, "both") == 0);
    IrHookOn = (lower1 || both || BabycDebugEnabled()) ? 1 : 0;
}

long long IrEvalExprHook(struct ParseState *Parser, IrExpr *E)
{
    int n = 0, dest = 0, ok = 0, top;
    long long tree, lin = 0;
    /* Hot path: lower off → pure tree, no large stack arrays.
     * Nested calls from linear/VM Step must not re-enter lower (CallN tables).
     * EXEC=vm: expr-level linear disabled — stmt-level VM owns the body;
     * residual tree walks must not Step LoadIndex with half-initialized frames. */
    if (!IrLowerEnabled() || LinExecDepth > 0 || IrExecIsVmProd()
        || (IrExecIsBoth() && (IrExprContainsCall(E, 0)
                               || IrExprHasSideEffects(E, 0))))
    {
        /* Shadow mode must not evaluate an expression containing a call twice:
         * calls may perform I/O or mutate state even when the lowerer only
         * partially models them. Keep the tree execution as the single owner. */
        BabycTreeExprExec++;
        return IrEvalExprTree(Parser, E);
    }
    {
    IrLinInst Code[IR_LIN_MAX];
    long long Before[IR_CALL_MAX_LOCALS];
    top = (ShadowDepth == 0);
    if (top && E != NULL)
    {
        LinParser = Parser;
        ok = IrLowerExpr(E, Code, IR_LIN_MAX, &n, &dest);
    }
    if (top && ok)
    {
        if (IrExecIsVmProd())
        {
            /* EXEC=vm: run full lowered code (index/global included).
             * IrVmExecProd / IrExecLin implement Load|Store Index+Global.
             * Abort/div0 → fall through to tree. */
            IrVmProdNotice();
            LinParser = Parser;
            LinStepAbort = 0;
            LinDiv0 = 0;
            lin = IrExecLin(Parser, Code, n, dest);
            if (!LinStepAbort && !LinDiv0)
            {
                LinLowered++;
                LinMatch++;
                return lin;
            }
            ok = 0;
        }
        else
        {
            /* EXEC=both: skip linear for exprs with Load/Store Index/Global.
             * Same residual as stmt gate — Step still SEGV on some local-array
             * *(a+i) / a[i] paths (uchar scale, partial Idx). Tree is source
             * of truth for the shadow compare. */
            if (IrLinCodeHasMemOps(Code, n)
                || (IrExecIsBoth() && IrLinCodeHasCalls(Code, n)))
                ok = 0;
            else
            {
                if (IrCallDepth > 0)
                    memcpy(Before, IrCallStack[IrCallDepth - 1]->LocalVal, sizeof(Before));
                LinParser = Parser;
                lin = IrExecLin(Parser, Code, n, dest);
                IrLowerRestoreGlobs();
                if (IrCallDepth > 0)
                    memcpy(IrCallStack[IrCallDepth - 1]->LocalVal, Before, sizeof(Before));
            }
        }
    }
    ShadowDepth++;
    tree = IrEvalExprTree(Parser, E);
    ShadowDepth--;
    if (top && ok)
    {
        LinLowered++;
        /* Pointer/blob address identity differs between linear and tree;
         * both non-null large values → soft-match (not a value bug). */
        {
            /* Structural soft-match: prefer expression shape over value heuristics.
             * Value-size fallback only for unresolved address/blob identity. */
            int soft_ok = (lin == tree);
            if (!soft_ok && E != NULL)
            {
                IrExpr *R = E->Right;
                int addr_like = IrExprYieldsPointer(Parser, E)
                    || (E->TypeFlags & IR_TF_PTR)
                    || (E->Kind == IrExUnary && (E->Op == TokenAmpersand
                        || E->Op == TokenAsterisk));
                if (E->Kind == IrExCall)
                    soft_ok = 1;
                else if (addr_like)
                    soft_ok = 1;
                /* Index/Member assign: linear StoreIndex mutates real blob then
                 * IdxSave restores; tree re-eval can see a different intermediate
                 * expression value while final program state agrees (seed 71). */
                else if (E->Kind == IrExAssign && E->Left != NULL
                         && (E->Left->Kind == IrExIndex
                             || E->Left->Kind == IrExMember
                             || (E->Left->Kind == IrExUnary
                                 && E->Left->Op == TokenAsterisk)))
                    soft_ok = 1;
                else if (E->Kind == IrExAssign && R != NULL
                         && (IrExprYieldsPointer(Parser, R)
                             || (R->TypeFlags & IR_TF_PTR)
                             || R->Kind == IrExTernary
                             || R->Kind == IrExCall
                             || R->Kind == IrExUnary
                             || R->Kind == IrExIndex
                             || R->Kind == IrExMember))
                    soft_ok = 1;
                else if (E->Kind == IrExBinOp
                         && (E->Op == TokenMinus || E->Op == TokenPlus)
                         && ((E->Left && (E->Left->TypeFlags & IR_TF_PTR))
                             || (E->Right && (E->Right->TypeFlags & IR_TF_PTR))))
                    soft_ok = 1;
                /* Last resort: host pointer identity (blob base differs). */
                else if (((unsigned long long)llabs(lin) > 0xFFFFULL
                          && (unsigned long long)llabs(tree) > 0xFFFFULL)
                         || ((lin == 0) != (tree == 0)
                             && ((unsigned long long)llabs(lin) > 0xFFFFULL
                                 || (unsigned long long)llabs(tree) > 0xFFFFULL)))
                    soft_ok = 1;
            }
            if (soft_ok)
                LinMatch++;
            else {
                LinMismatch++;
                if (getenv("BABYC_TRACE_MIS"))
                {
                    const char *lid = NULL, *rid = NULL;
                    int lsl = -1, rsl = -1, rkind = -1;
                    if (E && E->Left && E->Left->Kind == IrExIdent)
                    { lid = E->Left->Ident; lsl = E->Left->LocalSlot; }
                    if (E && E->Right)
                    {
                        rkind = (int)E->Right->Kind;
                        if (E->Right->Kind == IrExIdent)
                        { rid = E->Right->Ident; rsl = E->Right->LocalSlot; }
                    }
                    fprintf(stderr,
                        "[mis] lin=%lld tree=%lld k=%d op=%d line=%d "
                        "L=%s#%d Rk=%d R=%s#%d\n",
                        lin, tree, E?(int)E->Kind:-1, E?(int)E->Op:-1,
                        E ? E->SourceLine : -1,
                        lid ? lid : "?", lsl, rkind,
                        rid ? rid : "?", rsl);
                }
            }
        }
    }
    return tree;
    } /* end lower stack frame */
}

void IrExecuteStmtHook(struct ParseState *Parser, IrStmt *S)
{
    int n = 0, ok = 0, top, i, same, depth0;
    /* Hot path: tree only — no large stack frames.
     * 1) lower/debug off, or nested linear Step
     * 2) already inside a stmt (StmtDepth>0): under EXEC=vm, lower is only
     *    attempted on the outermost body. Nested For/Expr in sieve hot loops
     *    must NOT allocate Code[IR_LIN_STMT_MAX] (~16KB+) per statement —
     *    that was the VM-only sieve regression (~130ms → 210ms+). */
    top = IrStmtIsTop();
    if ((!BabycLowerOn && !BabycDebugOn) || LinExecDepth > 0
        || StmtDepth > 0 || !top)
    {
        StmtDepth++;
        BabycTreeStmtExec++;
        IrExecuteStmtTree(Parser, S);
        StmtDepth--;
        return;
    }
    {
    /* rare: only outermost body once per function */
    IrLinInst Code[IR_LIN_STMT_MAX];
    long long Copy[IR_CALL_MAX_LOCALS];
    long long Before[IR_CALL_MAX_LOCALS];
    /* Lower only outermost body of the current function frame. */
    depth0 = IrCallDepth;
    if (IrLowerEnabled() && S != NULL && IrCallDepth > 0
        /* Shadow-only lowering is speculative: reject effectful statement
         * trees before lowering, because even compiler bookkeeping can touch
         * the active lexical frame (notably for-init declarations). */
        && !(IrExecIsBoth() && IrStmtContainsCall(S, 0))
        && Parser->Mode == RunModeRun
        /* In shadow mode, lowering a for-init declaration can perturb the
         * parser's live lexical scope even when execution uses a local copy.
         * Keep that construct tree-owned until declaration lowering is fully
         * side-effect-free; this is a conservative eligibility gate. */
        && !(IrExecIsBoth() && S->Kind == IrStFor && S->InitStmt != NULL
             && S->InitStmt->Kind == IrStDecl)
        && (S->Kind == IrStIf || S->Kind == IrStWhile || S->Kind == IrStBlock
            || S->Kind == IrStFor || S->Kind == IrStDo
            || S->Kind == IrStExpr || S->Kind == IrStSwitch
            || S->Kind == IrStGoto || S->Kind == IrStReturn))
    {
        LinParser = Parser;
        CallN = 0;
        KernelN = 0;
        LinSkippedCall = 0;
        /* Isolate tables: failed Hook lower must not leave Idx/Glob dirty. */
        IdxN = 0;
        GlobN = 0;
        memset(GlobVal, 0, sizeof(GlobVal));
        memset(GlobRaw, 0, sizeof(GlobRaw));
        memset(IdxGlobBase, 0, sizeof(IdxGlobBase));
        /* Speculative Hook lower must not mutate live Fr (Decl side-effects). */
        LinCompileActive = 1;
        ok = IrLowerStmt(S, Code, IR_LIN_STMT_MAX, &n);
        LinCompileActive = 0;
        /* Same caps as FnCodeCompile: large/complex blocks residual under
         * linear (wrong mid-body exit on 059/231). Force tree. */
        if (ok && (n > 384 || CallN > 8 || LinSkippedCall))
            ok = 0;
        /* EXEC=both is a comparison aid, not an effect-replay engine. A
         * statement subtree containing any call may perform I/O or mutate
         * external state, so let the canonical tree executor own it once. */
        if (ok && IrExecIsBoth() && IrStmtContainsCall(S, 0))
            ok = 0;
        if (!ok)
        {
            IdxN = 0;
            GlobN = 0;
            CallN = 0;
            KernelN = 0;
            memset(IdxGlobBase, 0, sizeof(IdxGlobBase));
        }
        /* EXEC=both + Load/Store Index/Global: Step/IrLoadPtrElem still SEGV
         * on several array paths (array_sum/isort/dfs/matmul). Until FnCode
         * owns memops, refuse linear under both → pure tree (no crash).
         * EXEC=vm keeps its residual gates separately. */
        if (ok && !IrExecIsVmProd()
            && (IrLinCodeHasMemOps(Code, n)
                || (IrExecIsBoth() && IrLinCodeHasCalls(Code, n))))
            ok = 0;
    }
    if (top && ok)
    {
        if (IrExecIsVmProd())
        {
            /* EXEC=vm: full lowered stmt path. Index/global mem ops are
             * implemented in IrVmExecProd + IrExecLinLocals. Only skip when
             * a host/printf call was omitted (LinSkippedCall). */
            IrVmProdNotice();
            if (LinSkippedCall)
                ok = 0; /* force pure tree; do not mix */
            if (!LinSkippedCall)
            {
                LinRetSeen = 0;
                LinStepAbort = 0;
                LinDiv0 = 0;
                if (!IrVmExecProd(Code, n, IrCallStack[IrCallDepth - 1]->LocalVal,
                                  IR_CALL_MAX_LOCALS))
                    IrExecLinLocals(Code, n, IrCallStack[IrCallDepth - 1]->LocalVal,
                                    IR_CALL_MAX_LOCALS);
                if (!LinStepAbort && !LinDiv0)
                {
                    if (LinRetSeen && IrCallDepth > 0)
                    {
                        IrCallStack[IrCallDepth - 1]->ReturnVal = LinRetVal;
                        IrCallStack[IrCallDepth - 1]->HasReturn = 1;
                    }
                    ShadowOwned = 1;
                    StLowered++;
                    StMatch++;
                    if (LinRetSeen && Parser->pc && Parser->pc->TopStackFrame
                        && Parser->pc->TopStackFrame->ReturnValue
                        && Parser->pc->TopStackFrame->ReturnValue->Typ
                        && Parser->pc->TopStackFrame->ReturnValue->Typ->Base != TypeVoid
                        && Parser->pc->TopStackFrame->ReturnValue->Val)
                    {
                        struct Value *RV = Parser->pc->TopStackFrame->ReturnValue;
                        long long V = LinRetVal;
                        switch (RV->Typ->Base)
                        {
                            case TypeInt: RV->Val->Integer = (int)V; break;
                            case TypeShort: RV->Val->ShortInteger = (short)V; break;
                            case TypeChar: RV->Val->Character = (char)V; break;
                            case TypeLong: RV->Val->LongInteger = (long)V; break;
                            case TypeLongLong: RV->Val->LongLongInteger = V; break;
                            case TypeUnsignedInt: RV->Val->UnsignedInteger = (unsigned)V; break;
                            case TypePointer:
                            case TypeArray:
                                RV->Val->Pointer = (void *)(intptr_t)V;
                                break;
                            default: RV->Val->Integer = (int)V; break;
                        }
                        RV->IsInitialized = TRUE;
                    }
                    /* Only a real return stmt of this frame should flip Mode.
                     * Nested callee Ret must not abort the caller (059/231). */
                    if (LinRetSeen && S != NULL && S->Kind == IrStReturn)
                        Parser->Mode = RunModeReturn;
                    else if (LinRetSeen && IrCallDepth > 0)
                    {
                        /* keep frame return value; do not change Parser->Mode */
                        IrCallStack[IrCallDepth - 1]->ReturnVal = LinRetVal;
                        IrCallStack[IrCallDepth - 1]->HasReturn = 1;
                    }
                    return;
                }
            }
            /* host-call skip / abort / div0 -> pure tree below */
        }
        else
        {
            /* EXEC=both: linear on a copy, then tree compares. */
            memcpy(Before, IrCallStack[IrCallDepth - 1]->LocalVal, sizeof(Before));
            memcpy(Copy, Before, sizeof(Copy));
            IrExecLinLocals(Code, n, Copy, IR_CALL_MAX_LOCALS);
            IrLowerRestoreGlobs();
            IrVmShadow(Code, n, Before, IrCallDepth);
            ShadowOwned = 1;
        }
    }
        /* vm_strict: a top body we tried to lower and could not must not silently
         * fall back to tree. Nested stmts (StmtDepth>0) are owned by the linear
         * walk and are not a fallback. */
        if (IrVmStrictOn() && top && !ShadowOwned && IrExecIsVmProd()
            && S != NULL && Parser->Mode == RunModeRun)
            IrVmStrictTreeFallback(Parser, "body");
        /* Debug on tree path when linear did not own this walk (nested funcs included). */
    if (!ShadowOwned && IrDebugOn() && S != NULL && S->SourceLine > 0)
    {
        static int SeenDeep, FinishDepth, PrevLine, PrevDepth;
        if (S->SourceLine == PrevLine && IrCallDepth == PrevDepth)
            goto nested_done;
        PrevLine = S->SourceLine; PrevDepth = IrCallDepth;
        if (getenv("BABYC_FINISH") != NULL && IrCallDepth > FinishDepth)
        { SeenDeep = 1; FinishDepth = IrCallDepth; }
        fprintf(stderr, "[babyc-debug] line %d depth %d\n", S->SourceLine, IrCallDepth);
        if (getenv("BABYC_BT") != NULL)
            fprintf(stderr, "[babyc-debug] bt #%d %s %s:%d\n", IrCallDepth,
                (IrCallDepth > 0 && IrCallDepth < IR_CALL_MAX_DEPTH && IrDebugFunc[IrCallDepth - 1]) ? IrDebugFunc[IrCallDepth - 1] : "?",
                Parser->FileName ? Parser->FileName : "?", S->SourceLine);
        if (getenv("BABYC_FINISH") != NULL && SeenDeep && IrCallDepth < FinishDepth)
            fprintf(stderr, "[babyc-debug] finish depth %d\n", IrCallDepth);
nested_done: ;
    }
    StmtDepth++;
    IrExecuteStmtTree(Parser, S);
    StmtDepth--;
    if (top)
        ShadowOwned = 0;
    if (top && ok && IrCallDepth == depth0 && IrCallDepth > 0)
    {
        IrVmCommit(IrCallDepth);
        same = 1;
        if (NLab > 0 && Parser->Mode == RunModeGoto && n <= 256)
        {
            memcpy(GotoCode, Code, (size_t)n * sizeof(Code[0]));
            memcpy(GotoCopy, Copy, sizeof(GotoCopy));
            GotoN = n;
            GotoDepth = IrCallDepth;
            GotoPending = 1;
            StLowered++;
            StGoto++;
        }
        else
        {
            if (NLab == 0 && !LinDiv0 && !LinStepAbort && !LinSkippedCall)
            {
                for (i = 0; i < IR_CALL_MAX_LOCALS; i++)
                    if (!CallSkip[i] && Copy[i] != IrCallStack[IrCallDepth - 1]->LocalVal[i]) {
                        same = 0;
                        if (getenv("BABYC_TRACE_SLOT"))
                            fprintf(stderr, "[slot] i=%d lin=%lld tree=%lld name=%s\n", i,
                                Copy[i], IrCallStack[IrCallDepth - 1]->LocalVal[i],
                                IrCallStack[IrCallDepth - 1]->LocalName[i]
                                    ? IrCallStack[IrCallDepth - 1]->LocalName[i] : "?");
                    }
                if (LinRetSeen && IrCallStack[IrCallDepth - 1]->ReturnVal != LinRetVal)
                    same = 0;
            }
            /* In-place array/struct stores mutate shared blobs; LocalVal-only
             * restore cannot undo them → false soft-mm (e.g. isort 707). */
            {
                int has_mem = 0, k;
                for (k = 0; k < n; k++)
                    if (Code[k].Op == IrLinStoreIndex
                        || Code[k].Op == IrLinStoreMemberArray
                        || Code[k].Op == IrLinStoreDeref
                        || Code[k].Op == IrLinStoreGlobal)
                        has_mem = 1;
                if (has_mem && !same)
                    same = -1; /* signal skip */
            }
            StLowered++;
            if (NLab > 0 || LinDiv0 || LinStepAbort || LinSkippedCall || same < 0)
                StGoto++;
            else if (same)
                StMatch++;
            else
                StMismatch++;
        }
    }
    } /* end lower/debug stack frame */
}

void IrLowerGotoFinished(struct ParseState *Parser)
{
    int i, same;
    (void)Parser;
    if (!GotoPending)
        return;
    GotoPending = 0;
    if (IrCallDepth != GotoDepth || IrCallDepth <= 0)
        return;
    if (!IrExecLinLocals(GotoCode, GotoN, GotoCopy, IR_CALL_MAX_LOCALS))
        return;
    same = 1;
    for (i = 0; i < IR_CALL_MAX_LOCALS; i++)
        if (GotoCopy[i] != IrCallStack[IrCallDepth - 1]->LocalVal[i])
            same = 0;
    if (same)
        StMatch++;
    else
        StMismatch++;
}

int IrLowerCallSkipped(int i) { return i >= 0 && i < 128 && CallSkip[i]; }
int IrLowerLinSkipped(void) { return LinSkippedCall; }

IrExpr *IrLowerGlobalAt(int i) { return (i >= 0 && i < GlobN) ? GlobTab[i] : NULL; }

struct ParseState *IrLowerParser(void) { return LinParser; }

IrExpr *IrLowerIndexAt(int i) { return (i >= 0 && i < IdxN) ? IdxTab[i] : NULL; }

long long IrLinWideImmAt(int i)
{
    if (i >= 0 && i < IR_LIN_STMT_MAX && LinWideOn[i])
        return LinWideImm[i];
    return 0;
}
int IrLinWideImmOn(int i)
{
    return (i >= 0 && i < IR_LIN_STMT_MAX && LinWideOn[i]) ? 1 : 0;
}

