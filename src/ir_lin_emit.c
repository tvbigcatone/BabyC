/* ir_lin_emit.c — LowerRec + Emit/AllocReg extracted from ir_lower.c */
#include "picoc.h"
#include "ir_lin_emit.h"
#include "ir_lin_tabs.h"
#include "ir_lin_ptr.h"
#include "ir_lin_compile.h"
#include "ir_fncode.h"
#include "ir_lin_light.h"
#include "ir_runtime.h"
#include "ir_storage.h"
#include "ir_lin.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

extern struct ParseState *LinParser;
extern int LinCompileActive;
extern int LowerDepth;
extern int LinExecDepth;
extern unsigned char CallSkip[128];
extern int LinSkippedCall;
extern long LinSkipCallCount;
extern struct ValueType *CompLocalFromType[IR_CALL_MAX_LOCALS];
extern long long LinWideImm[IR_LIN_STMT_MAX];
extern unsigned char LinWideOn[IR_LIN_STMT_MAX];
extern int LinWideMax;
extern long long GlobSave[32];
extern int GlobSaved;
extern unsigned GlobDirty;
extern const char *CallNameTab[IR_LIN_CALL_SLOTS];

struct Value *IrGlobalIdentSoft(struct ParseState *Parser, const char *name);
/* Global identifier naming a function (qsort(a, n, sz, cmp)). */
static int IrLinIdentIsFuncDesignator(IrExpr *A)
{
    struct Value *TV;
    if (A == NULL || A->Ident == NULL || LinParser == NULL)
        return 0;
    TV = IrGlobalIdentSoft(LinParser, A->Ident);
    return TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction;
}


extern const char *LinHostCallName;
extern char IrLinStructRetBuf[512];
extern unsigned char LinRegU[IR_LIN_REGS];

void CompileStampName(int slot, const char *name);
void CompileStampPtrSlot(int slot, struct ValueType *FT, int is_fptr);
void IdxMetaPush(IrExpr *E);
void IrLiveMarkInit(IrCallFrame *Fr, int slot);
int IrDebugOn(void);
int IrExecIsVmProd(void);
int IrLinLocalIndexedType(IrCallFrame *Fr, int Slot);
void IrLinStructAssignBlob(IrCallFrame *Fr, int slot, void *Src, int sz);
int LinTok2Unsigned(const IrLinInst *In);
void IrLinCaretOnLastBracket(struct ParseState *P, IrExpr *IX);
int IrSafeMemberAddr(struct ParseState *Parser, IrExpr *E, void **Addr, int *Es, int *IsUnsigned);
struct ValueType *IrLowerCallLocalType(int slot);
int IrFnBodyIntegerEligible(IrStmt *Body);
FnCodeSlot *FnCodeFind(IrStmt *Body);
FnCodeSlot *FnCodeFindByFunc(struct Value *FV);
void FnCodeInstall(FnCodeSlot *S);
void FnCodeSnapshot(FnCodeSlot *S);
int LowerCallEnabled(void);
int IrLiveInitStampOk(void);
int IrLinLocalPointeeUnsigned(IrExpr *Base);
extern unsigned long long VmProfFuseLocalAdd, VmProfFuseIndexCopy;
void LinCseReset(void);
int IrLinRegDeadAfter(const IrLinInst *Code, int N, int Start, int Reg);

/* Resolve a local's type with the active function template taking precedence.
 * The shared accessor then supplies legacy compile metadata / live-frame state.
 * Keep this helper at lowering time; it adds no runtime VM work. */
static inline struct ValueType *IrLinEmitLocalType(int slot)
{
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return NULL;
    if (LinCompileActive && CompileSlot != NULL
        && CompileSlot->SlotFromType[slot] != NULL)
        return CompileSlot->SlotFromType[slot];
    return IrCompLocalFromTypeOrFrame(slot);
}

/* Resolve the declared value type of a source expression using the active
 * function's lowering metadata, including nested aggregate members and index
 * expressions. This is compile-time-only and is shared by address lowering so
 * member-array decay and member lvalues use the same type/layout source. */
static struct ValueType *LinExprValueType(IrExpr *E, int depth);
static int LinTypeObjectSize(struct ValueType *T);

/* Pointer type to T for lowering-time type queries (cold path; derived types
 * are shared through TypeGetMatching, as the front end builds them). */
static struct ValueType *LinPointerTo(struct ValueType *T)
{
    if (T == NULL || LinParser == NULL || LinParser->pc == NULL)
        return NULL;
    return TypeGetMatching(LinParser->pc, LinParser, T, TypePointer, 0,
                           LinParser->pc->StrEmpty, TRUE);
}

/* Type designated by `*local` under the lowered local-slot storage model.
 * The slot type (IrStorageDesc.FromType) is the element type for an array
 * local and the *pointee* type for a pointer local (scalar T *p records T;
 * multi-level T **p records the first-level pointee T *). The exception is a
 * struct/union pointee, which is kept even for T **p so member lookup works;
 * the recorded first-level pointee size tells the two apart. Returns NULL
 * when the declaration metadata does not prove the type. */
static struct ValueType *LinLocalDerefType(int slot)
{
    struct ValueType *T;
    if (slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return NULL;
    T = IrLinEmitLocalType(slot);
    if (IrCompLocalNDimsOrFrame(slot) > 0
        || (IrCallDepth > 0 && IrCallStack[IrCallDepth - 1]->LocalArraySize[slot] > 0))
    {
        /* *a is a[0]: a row of a multidimensional array is itself an
         * array; an element of T *a[N] is a pointer. */
        if (IrCompLocalNDimsOrFrame(slot) >= 2)
            return NULL;
        if (IrCompLocalElemIsPtrOrFrame(slot))
        {
            if (T != NULL && T->Base != TypePointer && T->Base != TypeArray
                && T->Base != TypeFunction)
                return LinPointerTo(T);
            return (LinParser != NULL && LinParser->pc != NULL)
                ? LinParser->pc->VoidPtrType : NULL;
        }
        return T;
    }
    if (T == NULL)
        return NULL;
    if (T->Base == TypeStruct || T->Base == TypeUnion)
    {
        int ps = IrCompLocalPointeeSizeOrFrame(slot);
        int os = LinTypeObjectSize(T);
        if (ps > 0 && os > 0 && ps == os)
            return T;                 /* T *p */
        if (ps == (int)sizeof(void *) && os > 0 && os != ps)
            return LinPointerTo(T);   /* T **p: first level is a pointer */
        return NULL;                  /* sizes do not disambiguate */
    }
    return T;
}

/* Pointee type of a pointer-valued expression P (the type of *P). Pointer
 * arithmetic and increments keep the pointer operand's type. */
static struct ValueType *LinExprPointeeType(IrExpr *P, int depth)
{
    struct ValueType *T;
    if (P == NULL || depth > 32)
        return NULL;
    if (P->Kind == IrExBinOp && (P->Op == TokenPlus || P->Op == TokenMinus))
    {
        IrExpr *Q = NULL;
        /* Slot metadata deliberately stores a pointer's pointee rather than
         * its full pointer type. Use the declaration-derived pointee query as
         * well as IR_TF_PTR, so commutative integer + pointer is typed too. */
        if (P->Left != NULL
            && ((P->Left->TypeFlags & IR_TF_PTR)
                || LinExprPointeeType(P->Left, depth + 1) != NULL))
            Q = P->Left;
        else if (P->Op == TokenPlus && P->Right != NULL
                 && ((P->Right->TypeFlags & IR_TF_PTR)
                     || LinExprPointeeType(P->Right, depth + 1) != NULL))
            Q = P->Right;
        return (Q != NULL) ? LinExprPointeeType(Q, depth + 1) : NULL;
    }
    if (P->Kind == IrExUnary
        && (P->Op == TokenIncrement || P->Op == TokenDecrement))
        return LinExprPointeeType(P->Left, depth + 1);
    if (P->Kind == IrExIdent && P->LocalSlot >= 0
        && P->LocalSlot < IR_CALL_MAX_LOCALS)
        return LinLocalDerefType(P->LocalSlot);
    T = LinExprValueType(P, depth + 1);
    if (T != NULL && (T->Base == TypePointer || T->Base == TypeArray))
        return T->FromType;
    return NULL;
}

/* *E designates a pointer object (T **p style): ++/-- must step by the
 * size of the pointed-to type, not by one. */
static int LinExprPointeeIsPointer(IrExpr *Deref)
{
    struct ValueType *T = LinExprValueType(Deref, 0);
    return T != NULL && T->Base == TypePointer;
}

static struct ValueType *LinExprValueType(IrExpr *E, int depth)
{
    struct ValueType *T;
    struct Value *V = NULL, *M = NULL;
    if (E == NULL || depth > 32)
        return NULL;
    if (E->Kind == IrExIdent)
    {
        if (E->LocalSlot >= 0 && E->LocalSlot < IR_CALL_MAX_LOCALS)
            return IrLinEmitLocalType(E->LocalSlot);
        if (E->ParamSlot >= 0 && E->ParamSlot < IR_CALL_MAX_PARAMS
            && IrCallDepth > 0)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            if (Fr != NULL && E->ParamSlot < Fr->NumParams
                && Fr->ParamTyp[E->ParamSlot] != NULL)
                return Fr->ParamTyp[E->ParamSlot];
        }
        if (E->Ident != NULL && LinParser != NULL)
        {
            V = IrGlobalIdentSoft(LinParser, E->Ident);
            if (V != NULL) return V->Typ;
        }
        return NULL;
    }
    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL)
    {
        T = LinExprValueType(E->Left, depth + 1);
        if (T == NULL) return NULL;
        if (E->Op == TokenArrow)
        {
            if ((T->Base == TypePointer || T->Base == TypeArray) && T->FromType != NULL)
                T = T->FromType;
            else if ((T->Base == TypeStruct || T->Base == TypeUnion)
                     && (E->Left->TypeFlags & IR_TF_PTR))
            {
                /* Local storage metadata deliberately records the pointee
                 * aggregate for an owned/address-backed pointer slot. The AST
                 * still carries PTR and `->`; in this representation T is
                 * already the designated struct/union, not Pointer(T). */
            }
            else
                return NULL;
        }
        if (T->Base != TypeStruct && T->Base != TypeUnion)
            return NULL;
        if (T->Members == NULL || !TableGet(T->Members, E->Ident, &M, NULL, NULL, NULL)
            || M == NULL)
            return NULL;
        return M->Typ;
    }
    if (E->Kind == IrExIndex && E->Left != NULL)
    {
        T = LinExprValueType(E->Left, depth + 1);
        /* Linear local metadata stores arrays as dimensions + leaf type rather
         * than a synthesized TypeArray tree. Recover the array element's
         * declared type for expressions such as v[i].member. */
        if ((T == NULL || (T->Base != TypePointer && T->Base != TypeArray)
             || T->FromType == NULL)
            && E->Left->Kind == IrExIdent
            && E->Left->LocalSlot >= 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
            && IrCompLocalNDimsOrFrame(E->Left->LocalSlot) > 0)
            return T;
        if ((T == NULL || (T->Base != TypePointer && T->Base != TypeArray)
             || T->FromType == NULL) && E->PointeeType != NULL)
            return E->PointeeType;
        if (T == NULL || (T->Base != TypePointer && T->Base != TypeArray))
            return NULL;
        return T->FromType;
    }
    if (E->Kind == IrExUnary && E->Left != NULL)
    {
        if (E->Op == TokenAsterisk)
            return LinExprPointeeType(E->Left, depth + 1);
        T = LinExprValueType(E->Left, depth + 1);
        if (T == NULL) return NULL;
        if (E->Op == TokenAmpersand)
            return T; /* only the referred-to aggregate type is needed here */
        return T;
    }
    if (E->Kind == IrExCall)
        return IrLowerCallReturnType(E);
    if (E->Kind == IrExTernary && E->Left != NULL)
        return LinExprValueType(E->Left, depth + 1);
    return NULL;
}

static int LinTypeObjectSize(struct ValueType *T)
{
    if (T == NULL) return 0;
    if (T->Base == TypeFP) return TypeFPStorageSize(T);
    if ((T->Base == TypeStruct || T->Base == TypeUnion) && T->Sizeof <= 0)
        return IrLowerInferAggregateSize(T);
    if (T->Sizeof > 0) return (int)T->Sizeof;
    if (T->Base == TypeChar || T->Base == TypeUnsignedChar) return 1;
    if (T->Base == TypeShort || T->Base == TypeUnsignedShort) return 2;
    if (T->Base == TypePointer || T->Base == TypeFunction) return (int)sizeof(void *);
    if (T->Base == TypeInt || T->Base == TypeUnsignedInt || T->Base == TypeLong
        || T->Base == TypeUnsignedLong || T->Base == TypeEnum) return (int)sizeof(int);
    if (T->Base == TypeLongLong || T->Base == TypeUnsignedLongLong) return 8;
    return 0;
}

static int LinEmitAddressOf(IrExpr *Target, IrLinInst *Code, int Cap,
                            int *N, int *Next, int *Dest)
{
    IrExpr Addr;
    if (Target == NULL) return 0;
    /* C defines &*p as p without an intervening object access. */
    if (Target->Kind == IrExUnary && Target->Op == TokenAsterisk && Target->Left != NULL)
        return LowerRec(Target->Left, Code, Cap, N, Next, Dest);
    memset(&Addr, 0, sizeof(Addr));
    Addr.Kind = IrExUnary;
    Addr.Op = TokenAmpersand;
    Addr.Left = Target;
    Addr.TypeFlags = IR_TF_PTR;
    /* This address node is synthesized after body annotation. Stamp its
     * complete canonical pointer type at construction rather than asking the
     * emitter to infer it from the target. */
    Addr.CanonicalBase = TypePointer;
    Addr.CanonicalFlags = IR_CF_VALID | IR_CF_PTR;
    Addr.CanonicalOpBase = TypePointer;
    Addr.CanonicalOpFlags = IR_CF_VALID | IR_CF_PTR | IR_CF_UNSIGNED
        | (BABYC_VALUE_BITS_POINTER > BABYC_VALUE_BITS_INT ? IR_CF_WIDE : 0);
    Addr.SourceLine = Target->SourceLine;
    Addr.CharacterPos = Target->CharacterPos;
    return LowerRec(&Addr, Code, Cap, N, Next, Dest);
}

static inline int IrLinEmitTypeIsPointerOrArray(int slot)
{
    struct ValueType *T = IrLinEmitLocalType(slot);
    return T != NULL && (T->Base == TypePointer || T->Base == TypeArray);
}

static inline int IrLinEmitPointerHasSizedPointee(int slot)
{
    struct ValueType *T = IrLinEmitLocalType(slot);
    return T != NULL && T->Base == TypePointer && T->FromType != NULL
        && T->FromType->Sizeof > 0;
}


int GlobPush(IrExpr *E)
{
    int i;
    struct Value *V = NULL;
    if (E != NULL && E->CachedVal != NULL && !E->CachedVal->OutOfScope)
        V = E->CachedVal;
    else if (LinParser != NULL && E != NULL)
        /* Lowering visits both sides of runtime conditionals. Keep unresolved
         * identifiers as deferred global references so an unreachable branch
         * cannot fail early; IrLinLoadGlobal performs the hard lookup if the
         * instruction actually executes, matching the tree/token path. */
        V = IrGlobalValSoft(LinParser, E);
    /* Dedup: same Value* or same Ident → same slot (enables IncG peephole). */
    for (i = 0; i < GlobN; i++)
    {
        if (V != NULL && GlobVal[i] == V)
            return i;
        if (E != NULL && GlobTab[i] != NULL && E->Ident != NULL
            && GlobTab[i]->Ident == E->Ident)
        {
            if (GlobVal[i] == NULL && V != NULL)
                GlobVal[i] = V;
            return i;
        }
    }
    if (GlobN >= 32) return -1;
    GlobTab[GlobN] = E;
    GlobVal[GlobN] = V;
    GlobRaw[GlobN] = NULL;
    if (V != NULL && V->Val != NULL && V->Typ != NULL)
    {
        enum BaseType b = V->Typ->Base;
        if (b == TypeLongLong || b == TypeUnsignedLongLong)
            GlobRaw[GlobN] = &V->Val->LongLongInteger;
        else if (b == TypeInt || b == TypeUnsignedInt)
            GlobRaw[GlobN] = NULL; /* keep Value* path for narrow */
    }
    return GlobN++;
}


int IsRel(enum LexToken Op)
{
    return Op == TokenEqual || Op == TokenNotEqual
        || Op == TokenLessThan || Op == TokenGreaterThan
        || Op == TokenLessEqual || Op == TokenGreaterEqual;
}


extern int LinCseRegIsPinned(int reg);
extern void LinCsePinReg(int reg);
int AllocReg(int *Next)
{
    /* Per-statement temporaries may reuse any register whose last use passed.
     * Registers referenced by a live CSE entry are pinned until invalidation. */
    while (*Next < IR_LIN_REGS && LinCseRegIsPinned(*Next))
        ++(*Next);
    if (*Next >= IR_LIN_REGS)
        return -1;
    return (*Next)++;
}


int Emit(IrLinInst *Code, int Cap, int *N, IrLinInst In)
{
    if (*N >= Cap)
        return 0;
    Code[(*N)++] = In;
    return 1;
}


int EmitLine(IrLinInst *Code, int Cap, int *N, int Line)
{
    IrLinInst Ln;
    if (Line <= 0)
        return 1;
    memset(&Ln, 0, sizeof(Ln));
    Ln.Op = IrLinLine;
    Ln.Imm = Line;
    return Emit(Code, Cap, N, Ln);
}


int LowerRec(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next, int *Dest);
/* LowerLocalStructArrayAddress → ir_lin_ptr.h */
/* LowerGlobalStructArrayAddress → ir_lin_ptr.h */

/* Pointer +/- integer operand classification (one rule for both sides; was
 * duplicated inline). Only genuine pointers scale: slot type / SlotIsPtr /
 * array dims, &x, pointer-typed Index/Member, T* calls, and nested pointer
 * arithmetic ((text + 5) - 2: the inner sum is a pointer, so the outer
 * subtraction must be the 64-bit pointer form, not a narrow int Bin). */
static int LinOperandIsGenuinePtr(IrExpr *X, int depth)
{
    if (X == NULL || depth > 8)
        return 0;
    if (X->Kind == IrExBinOp && (X->Op == TokenPlus || X->Op == TokenMinus))
    {
        int lp = LinOperandIsGenuinePtr(X->Left, depth + 1);
        int rp = LinOperandIsGenuinePtr(X->Right, depth + 1);
        return (X->Op == TokenPlus) ? (lp != rp) : (lp && !rp);
    }
        if (X->Kind == IrExIdent && X->LocalSlot >= 0
            && X->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            int ls = X->LocalSlot;
            /* The active compile slot is authoritative while compiling a
             * function body; CompLocalFromType is only the legacy fallback
             * and may describe a previous slot owner. */
            struct ValueType *CT = IrLinEmitLocalType(ls);
            /* Compile-time SlotIsPtr / array dims (178: a+N decay). */
            if (LinCompileActive && CompileSlot != NULL && CompileSlot->SlotIsPtr[ls])
                return 1;
            else if (IrCompLocalNDimsOrFrame(ls) > 0 || IrCompLocalLeafEsOrFrame(ls) > 0)
                return 1; /* owned array decays to pointer */
            else if (CT != NULL && (CT->Base == TypePointer || CT->Base == TypeArray))
                return 1;
            else if (!LinCompileActive && IrCallDepth > 0
                && (IrCallStack[IrCallDepth - 1]->LocalIsPtr[ls]
                    || IrCallStack[IrCallDepth - 1]->LocalArraySize[ls] > 0))
                return 1;
            /* 625: scalar CT must not inherit caller LocalIsPtr — already gated */
        }
        else if (X->Kind == IrExIdent
                 && (X->TypeFlags & IR_TF_PTR))
            return 1;
        else if (X->Kind == IrExUnary && X->Op == TokenAmpersand)
            return 1;
        else if (X->Kind == IrExIndex
                 && ((X->TypeFlags & IR_TF_PTR) || X->PointeeType != NULL))
            return 1; /* a[i] of T *a[N] (declared pointee) */
        else if (X->Kind == IrExMember
                 && ((X->TypeFlags & IR_TF_PTR) || X->ElemSize > 0))
            return 1;
        else if (X->Kind == IrExVaArg)
            return X->VaType != NULL && X->VaType->Base == TypePointer;
        else if (X->Kind == IrExCall)
        {
            /* Integer self-calls (fib(n-1)+fib(n-2)) may carry stale TF_PTR
             * from annotate; only a true pointer return type scales. */
            struct ValueType *RT = IrLowerCallReturnType(X);
            if (RT != NULL && RT->Base == TypePointer)
                return 1;
        }
    return 0;
}

/* Declared stride of a pointer operand when the IR knows it better than
 * ElemSize: a[i] of T *a[N] (PointeeType; the row's ElemSize is the slot
 * size), and nested p + n / p - n (the pointer side's stride). 0 = unknown,
 * callers keep their historical ElemSize fallbacks. Shared by ptr - ptr and
 * ptr +/- int. */
static int LinPtrOperandStride(IrExpr *X, int depth)
{
    if (X == NULL || depth > 8)
        return 0;
    if (X->Kind == IrExIndex && X->PointeeType != NULL && X->PointeeType->Sizeof > 0)
        return (int)X->PointeeType->Sizeof;
    if (X->Kind == IrExVaArg && X->VaType != NULL && X->VaType->Base == TypePointer
        && X->VaType->FromType != NULL)
        return (X->VaType->FromType->Base == TypePointer) ? (int)sizeof(void *)
             : (int)X->VaType->FromType->Sizeof;
    if (X->Kind == IrExBinOp && (X->Op == TokenPlus || X->Op == TokenMinus))
    {
        if (LinOperandIsGenuinePtr(X->Left, depth + 1))
            return LinPtrOperandStride(X->Left, depth + 1);
        if (X->Op == TokenPlus && LinOperandIsGenuinePtr(X->Right, depth + 1))
            return LinPtrOperandStride(X->Right, depth + 1);
        return 0;
    }
    return 0;
}

static int LinExprHasFpValue(const IrExpr *E)
{
    if (E == NULL) return 0;
    if (E->Kind == IrExConst && E->Op == TokenFPConstant) return 1;
    /* IR annotations currently propagate FP through some integer-valued
     * predicates. Determine the expression's value type before trusting the
     * annotation, so (x < 1.5) remains an int while its operands are FP. */
    if (E->Kind == IrExBinOp)
    {
        if (IsRel(E->Op) || E->Op == TokenLogicalAnd || E->Op == TokenLogicalOr)
            return 0;
        if (E->Op == TokenComma)
            return LinExprHasFpValue(E->Right);
    }
    if (E->Kind == IrExTernary)
        return LinExprHasFpValue(E->Right) || LinExprHasFpValue(E->Args[0]);
    if (E->Kind == IrExUnary && E->Op == TokenCast)
        return E->I32 == TypeFP;
    if (E->TypeFlags & IR_TF_FP) return 1;
    return E->I32 == TypeFP;
}

static int LinEmitIntToFp(int *Reg, IrLinInst *Code, int Cap,
                          int *N, int *Next)
{
    IrLinInst In;
    int cv;
    if (Reg == NULL || *Reg < 0) return 0;
    cv = AllocReg(Next);
    if (cv < 0) return 0;
    In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
    In.A = (unsigned char)*Reg; In.B = 0;
    In.Tok = TokenCast; In.Imm = 2; /* integer value -> IEEE double bits */
    if (!Emit(Code, Cap, N, In)) return 0;
    *Reg = cv;
    return 1;
}

static int LinEmitFpToInt(int *Reg, IrLinInst *Code, int Cap,
                          int *N, int *Next)
{
    IrLinInst In;
    int cv;
    if (Reg == NULL || *Reg < 0) return 0;
    cv = AllocReg(Next);
    if (cv < 0) return 0;
    In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
    In.A = (unsigned char)*Reg; In.B = 0;
    In.Tok = TokenCast; In.Imm = 1; /* IEEE double bits -> integer, truncate */
    if (!Emit(Code, Cap, N, In)) return 0;
    *Reg = cv;
    return 1;
}

int IrLinEmitScalarConversion(enum BaseType TargetBase, IrExpr *Source,
                              int *Reg, IrLinInst *Code, int Cap,
                              int *N, int *Next)
{
    int source_fp;
    if (!IrVmLinearFeaturesOn() || Source == NULL || Reg == NULL || *Reg < 0)
        return 1;
    source_fp = LinExprHasFpValue(Source);
    if (TargetBase == TypeFP && !source_fp)
        return LinEmitIntToFp(Reg, Code, Cap, N, Next);
    if (source_fp && ((TargetBase >= TypeInt && TargetBase <= TypeUnsignedLongLong)
                      || TargetBase == TypeEnum))
        return LinEmitFpToInt(Reg, Code, Cap, N, Next);
    return 1;
}

static int LinCanonicalIntegerType(enum BaseType B)
{
    return B == TypeInt || B == TypeShort || B == TypeChar || B == TypeLong
        || B == TypeLongLong || B == TypeUnsignedInt || B == TypeUnsignedShort
        || B == TypeUnsignedChar || B == TypeUnsignedLong || B == TypeUnsignedLongLong;
}

static int LinEmitCanonicalArmCast(int *Reg, IrExpr *Arm, enum BaseType Target,
                                   IrLinInst *Code, int Cap, int *N, int *Next)
{
    IrLinInst In;
    int cv;
    if (Reg == NULL || !LinCanonicalIntegerType(Target)) return 1;
    /* Do not materialize a redundant cast when the arm already has the common
     * canonical type; mixed ternaries convert only the arm that needs it. */
    if (Arm != NULL && (Arm->CanonicalFlags & IR_CF_VALID)
        && Arm->CanonicalBase == (int)Target) return 1;
    cv = AllocReg(Next);
    if (cv < 0) return 0;
    In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
    In.A = (unsigned char)*Reg; In.B = 0;
    In.Tok = TokenCast; In.Imm = 0x100 + (long long)Target;
    if (!Emit(Code, Cap, N, In)) return 0;
    *Reg = cv;
    return 1;
}

/* Aggregate store `D = V` where D is a struct/union member, dereference,
 * index or global designator: V lowers to the source object's address (any
 * aggregate value form) and D to its object address; IrLinStoreIndex Tok=3
 * copies sizeof(type) bytes and maintains owned init maps. The expression's
 * value is the destination object. */
static int LinLowerAggregateStore(IrExpr *E, IrLinInst *Code, int Cap,
                                  int *N, int *Next, int *Dest)
{
    IrLinInst In;
    int src_reg = -1, dst_reg = -1;
    int nbytes = E->Left->CanonicalType->Sizeof;
    if (nbytes <= 0 || nbytes > 4096)
        return 0;
    if (!LowerRec(E->Right, Code, Cap, N, Next, &src_reg) || src_reg < 0)
        return 0;
    if (!LinEmitAddressOf(E->Left, Code, Cap, N, Next, &dst_reg) || dst_reg < 0)
        return 0;
    { extern void LinCseInvalAll(void); LinCseInvalAll(); }
    memset(&In, 0, sizeof(In));
    In.Op = IrLinStoreIndex; In.Dest = 0;
    In.A = (unsigned char)src_reg; In.B = (unsigned char)dst_reg;
    In.Tok = 3; In.Imm = nbytes;
    if (!Emit(Code, Cap, N, In)) return 0;
    *Dest = dst_reg;
    return 1;
}

static int LowerRecImpl(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next, int *Dest)
{
    IrLinInst In;
    int ld, rd;
    if (E == NULL)
        return 0;
    if (!(E->CanonicalFlags & IR_CF_VALID)) IrCanonicalCheckNode(E);
    if (E->Kind == IrExVaArg)
    {
        int CursorReg, OutReg;
        if (E->Left == NULL || E->VaType == NULL
            || !LowerRec(E->Left, Code, Cap, N, Next, &CursorReg))
            return 0;
        OutReg = AllocReg(Next);
        if (OutReg < 0) return 0;
        In.Op = IrLinVaArg; In.Dest = (unsigned char)OutReg;
        In.A = (unsigned char)CursorReg; In.B = 0;
        In.Tok = 0; In.Imm = (long long)(intptr_t)E->VaType; /* exact requested type */
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = OutReg;
        return 1;
    }
    /* Aggregate store through a member / dereference / global designator.
     * (Index targets keep the array-of-struct copy path below and reach
     * LinLowerAggregateStore at not_struct_assign for other sources.) */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Right != NULL
        && E->Left->Kind != IrExIndex && E->Left->CanonicalType != NULL
        && IrExprDesignatesAggregateObject(E->Left))
        return LinLowerAggregateStore(E, Code, Cap, N, Next, Dest);
    /* FP handling is a Linear semantic feature, not a STRICT policy.
     * STRICT controls whether a failed lowering may fall back to Tree; the
     * same FP forms must lower identically in Linear non-STRICT. */
    if (E->TypeFlags & IR_TF_FP)
    {
        if (!IrVmLinearFeaturesOn())
            return 0;
        /* Prefix -x is BinOp(0 - FPConst) in ir_compile — fold later.
         * Scalar FP + - * / lower to IR_LIN_F_FP in the Linear path. */
        if (E->Kind == IrExBinOp
            && (E->Op == TokenPlus || E->Op == TokenMinus
                || E->Op == TokenAsterisk || E->Op == TokenSlash))
            ; /* allow FP ALU */
        else if (E->Kind == IrExUnary
                 && (E->Op == TokenCast || E->Op == TokenUnaryNot
                     || E->Op == TokenMinus
                     || E->Op == TokenAmpersand || E->Op == TokenAsterisk))
            ; /* allow &fp / *fp (scanf %f/%lf, address of scalar FP) */
        else if (E->Kind == IrExBinOp && IsRel(E->Op)
                 && E->Left != NULL && E->Right != NULL
                 && (LinExprHasFpValue(E->Left) || LinExprHasFpValue(E->Right)))
            ; /* Mixed/FP comparison returns an integer predicate after FP promotion. */
        else if (E->Kind == IrExBinOp && E->Op == TokenComma)
            ; /* comma sequences side effects and takes the right operand's type */
        else if (E->Kind == IrExConst || E->Kind == IrExIdent
                 || E->Kind == IrExAssign || E->Kind == IrExCall
                 || E->Kind == IrExIndex || E->Kind == IrExMember
                 || E->Kind == IrExTernary || E->Kind == IrExVaArg)
            ; /* FP conditional arms are converted to their common value type below. */
        else
            return 0;
    }
    /* & and * handled below for Ident/Index; other forms fall through / refuse later. */
    /* PTR gate: only refuse pointer *arithmetic forms* we cannot scale.
     * Ident load, Call, Cast, &x, string const Imm, Ternary (p?q:r) are value forms. */
    if ((E->TypeFlags & IR_TF_PTR)
        && E->Kind != IrExIdent
        && E->Kind != IrExCall
        && E->Kind != IrExConst
        && E->Kind != IrExAssign   /* n->next = head / p = q */
        && E->Kind != IrExMember  /* p->next load */
        && E->Kind != IrExIndex   /* p[i] on int* */
        && E->Kind != IrExTernary /* p ? *p : 99 / p ? q : r */
        && !(E->Kind == IrExUnary
            && (E->Op == TokenAmpersand || E->Op == TokenCast
                || E->Op == TokenUnaryNot || E->Op == TokenAsterisk
                || E->Op == TokenIncrement || E->Op == TokenDecrement))
        && !(E->Kind == IrExBinOp
             && (E->Op == TokenPlus || E->Op == TokenMinus
                 || E->Op == TokenComma)))
        return 0;
    /* C comma operator: execute the left operand for its side effects, then
     * yield the right operand. Drop CSE facts between them because either
     * side may write through an alias or call another function. */
    if (E->Kind == IrExBinOp && E->Op == TokenComma
        && E->Left != NULL && E->Right != NULL)
    {
        int ignored, next_mark = *Next;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ignored)) return 0;
        LinCseReset();
        *Next = next_mark;
        return LowerRec(E->Right, Code, Cap, N, Next, Dest);
    }
    /* Index load: Ident base, or any supported nested array depth with an
     * Ident root. Flattening below computes the row-major scalar offset. */
    if (E->Kind == IrExIndex)
    {
        int es = E->ElemSize;
        IrExpr *Root = E->Left;
        if (Root == NULL) return 0;
        /* A cast-to-pointer is still a pointer expression: compute the
         * effective address from the cast value and the declared indexed
         * element size, then use the ordinary checked pointer load.  This
         * covers any `((T *)p)[i]` shape rather than special-casing a test or
         * requiring the pointer to be an Ident/Call/Member node. */
        if (Root->Kind == IrExUnary && Root->Op == TokenCast
            && E->Right != NULL)
        {
            struct ValueType *PT = E->PointeeType;
            int ptr_reg, ix_reg, scale_reg, off_reg, addr_reg;
            int uns = (E->CanonicalFlags & IR_CF_UNSIGNED) != 0;
            if (es != 1 && es != 2 && es != 4 && es != 8)
                es = Root->ElemSize;
            if ((es != 1 && es != 2 && es != 4 && es != 8) && Root->PointeeType != NULL)
                es = LinTypeObjectSize(Root->PointeeType->FromType != NULL
                    ? Root->PointeeType->FromType : Root->PointeeType);
            if (es != 1 && es != 2 && es != 4 && es != 8)
                es = LinTypeObjectSize(PT);
            if (es != 1 && es != 2 && es != 4 && es != 8)
                return 0;
            if (!LowerRec(Root, Code, Cap, N, Next, &ptr_reg)
                || !LowerRec(E->Right, Code, Cap, N, Next, &ix_reg))
                return 0;
            scale_reg = AllocReg(Next);
            off_reg = AllocReg(Next);
            addr_reg = AllocReg(Next);
            if (scale_reg < 0 || off_reg < 0 || addr_reg < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)scale_reg;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
            In.A = (unsigned char)ix_reg; In.B = (unsigned char)scale_reg;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)addr_reg;
            In.A = (unsigned char)ptr_reg; In.B = (unsigned char)off_reg;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr_reg; In.B = (unsigned char)(uns ? 1 : 0);
            In.Tok = 2; In.Imm = es;
            if (E->SourceLine > 0 && E->SourceLine <= 65535
                && E->CharacterPos > 0 && E->CharacterPos <= 65535)
                In.Imm = ((long long)E->SourceLine << 32)
                       | ((long long)E->CharacterPos << 16) | es;
            return Emit(Code, Cap, N, In);
        }
        while (Root != NULL && Root->Kind == IrExIndex)
            Root = Root->Left;
        /* s.arr[i] / (*p).arr[i]: Root is Member (possibly nested) — specialized later. */
        if (Root != NULL && Root->Kind == IrExMember && Root->Ident != NULL
            && Root->Left != NULL
            && (Root->Left->Kind == IrExIdent || Root->Left->Kind == IrExMember
                || (Root->Left->Kind == IrExUnary && Root->Left->Op == TokenAsterisk
                    && Root->Left->Left != NULL && Root->Left->Left->Kind == IrExIdent)
                || (Root->Left->Kind == IrExIndex && Root->Left->Left != NULL
                    && Root->Left->Left->Kind == IrExIdent)))
        {
            /* fall through to Index-of-Member path */
        }
        else if (Root != NULL && Root->Kind == IrExCall)
        {
            /* f()[i] uses the Index-of-Call path whenever Linear is active. */
            if (!IrVmLinearFeaturesOn()) return 0;
        }
        else if (Root == NULL || Root->Kind != IrExIdent
            || (Root->LocalSlot < 0 && Root->Ident == NULL))
            return 0;
        /* multi-dim: outer Index may carry row ElemSize (N*4); allow any >0
         * or 0 (ResolvePtr). Scalar 1/2/4/8 still preferred. */
        if (es != 0 && es != 1 && es != 2 && es != 4 && es != 8
            && es != (int)sizeof(int) && es != (int)sizeof(long)
            && es < 0)
            return 0;
        /* EXEC=vm: p[i] on LocalIsPtr allowed — base from LocalVal, es from LocalElemSize. */
        /* fall through to Index lower */
    }
    /* FP constant: IEEE bits in ConstVal → wide Imm (full 64-bit). */
    if (E->Kind == IrExConst && E->Op == TokenFPConstant)
    {
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)*Dest; In.A = In.B = 0;
        if (*N >= 0 && *N < IR_LIN_STMT_MAX)
        {
            LinWideImm[*N] = E->ConstVal;
            LinWideOn[*N] = 1;
            if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
            In.Tok = 2; /* wide */
            In.Imm = *N;
        }
        else
        {
            In.Tok = 0;
            In.Imm = (int)E->ConstVal;
        }
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExConst && E->Op != TokenFPConstant)
    {
        /* String constants: Imm holds pointer bits (ConstVal). Tok=1 marks
         * pointer so host-call Step packs IrPackPtrArg.
         * Values outside int32: LinWideOn[*N] keeps full ConstVal for Step. */
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)*Dest; In.A = In.B = 0;
        if (E->Op == TokenStringConstant)
        {
            /* Always wide: string pointers are 64-bit on LP64. Tok=1 with
             * truncated Imm zero-extends to a bad userland address (SEGV on
             * n ? "x" : "" call args / assigns under soft+strict). */
            if (*N >= 0 && *N < IR_LIN_STMT_MAX)
            {
                LinWideImm[*N] = E->ConstVal;
                LinWideOn[*N] = 1;
                if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
                In.Tok = 3; /* string + wide */
                In.Imm = *N;
            }
            else
            {
                In.Tok = 1;
                In.Imm = (int)E->ConstVal;
            }
        }
        else if (E->ConstVal != (long long)(int)E->ConstVal
                 && *N >= 0 && *N < IR_LIN_STMT_MAX)
        {
            LinWideImm[*N] = E->ConstVal;
            LinWideOn[*N] = 1; if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
            In.Tok = 2; /* wide int */
            In.Imm = *N; /* slot into LinWideImm */
        }
        else
        {
            In.Tok = 0;
            In.Imm = (int)E->ConstVal;
        }
        return Emit(Code, Cap, N, In);
    }
    /* &ident / &a[i] → address bits in reg */
    if (E->Kind == IrExUnary && E->Op == TokenAmpersand && E->Left != NULL)
    {
        IrExpr *T = E->Left;
        if (T->Kind == IrExIdent && T->LocalSlot >= 0)
        { extern void LinMarkAddrTaken(int); LinMarkAddrTaken(T->LocalSlot); }
        if (T->Kind == IrExIdent && T->LocalSlot >= 0
            && T->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* Array/ptr: LocalVal holds heap/stack blob address.
             * Scalar: need &LocalVal[slot] — Tok=4 Imm=slot, resolved at Step. */
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            /* Array/struct blob: LocalVal holds the address -> LoadLocal.
             * Scalar/pointer cell: Tok=4 = &LocalVal[slot].
             * CRITICAL: at FnCodeCompile IrCallDepth is often 0 so live-frame
             * LocalOwned/LocalArraySize are unavailable — use compile-time
             * CompLocal / SlotFromType / SlotIsPtr so &s for owned struct is
             * never Tok=4 (that made Graph* point at LocalVal and dfs
             * stores smashed the caller frame; 619 SEGV). */
            {
                int ls = T->LocalSlot;
                int is_blob = 0, is_ptr_cell = 0;
                struct ValueType *CT = NULL;
                if (ls >= 0 && ls < IR_CALL_MAX_LOCALS)
                {
                    /* Multi-dim only — LeafEs alone is not blob: pointer
                     * scalars stamp ElemSize=sizeof(void*) and need Tok=4. */
                    if (IrCompLocalNDimsOrFrame(ls) > 0)
                        is_blob = 1;
                    if (CompileSlot != NULL && CompileSlot->SlotFromType[ls] != NULL)
                        CT = CompileSlot->SlotFromType[ls];
                    if (CT == NULL)
                        CT = CompLocalFromType[ls];
                    if (CT != NULL && (CT->Base == TypeStruct || CT->Base == TypeUnion
                                      || CT->Base == TypeArray))
                        is_blob = 1;
                    if (CompileSlot != NULL && CompileSlot->SlotIsPtr[ls])
                        is_ptr_cell = 1;
                    if (CT != NULL && CT->Base == TypePointer)
                        is_ptr_cell = 1;
                    if (IrCallDepth > 0)
                    {
                        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                        if (Fr->LocalArraySize[ls] > 0 || Fr->LocalOwned[ls])
                            is_blob = 1;
                        if (Fr->LocalIsPtr[ls] && Fr->LocalArraySize[ls] <= 0
                            && !Fr->LocalOwned[ls])
                            is_ptr_cell = 1;
                    }
                    if (is_ptr_cell)
                        is_blob = 0;
                }
                if (is_blob)
                {
                    In.Op = IrLinLoadLocal; In.Dest = (unsigned char)*Dest;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = ls;
                }
                else if (is_ptr_cell && (T->TypeFlags & IR_TF_PTR))
                {
                    /* pointer variable: &p is address of the pointer cell */
                    In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
                    In.A = In.B = 0; In.Tok = 4;
                    In.Imm = ls;
                }
                else
                {
                    /* Scalar &x : address of the local slot itself */
                    In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
                    In.A = In.B = 0; In.Tok = 4;
                    In.Imm = ls;
                }
            }
            return Emit(Code, Cap, N, In);
        }
        /* &ret()->m: Call returns T*, linear Call + member offset → address. */
        if (T->Kind == IrExMember && T->Left != NULL && T->Ident != NULL
            && T->Left->Kind == IrExCall)
        {
            int pr, off = 0, es = 4, eb = TypeInt;
            struct ValueType *ST = IrLowerCallReturnStruct(T->Left);
            if (ST == NULL || !IrLowerMemberOffEs(ST, T->Ident, &off, &es, &eb))
                return 0;
            if (T->BitWidth > 0) return 0;
            if (!LowerRec(T->Left, Code, Cap, N, Next, &pr)) return 0;
            if (off == 0)
            {
                *Dest = pr;
                return 1;
            }
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinAddI; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)pr; In.B = 0;
            In.Tok = 0; In.Imm = off;
            return Emit(Code, Cap, N, In);
        }
        /* General address of a member lvalue: form the parent object address
         * recursively, then add the shared resolved member offset. This covers
         * direct, nested and indexed aggregate bases rather than duplicating a
         * single-level `s.m` / `v[i].m` implementation. */
        if (T->Kind == IrExMember && T->Left != NULL && T->Ident != NULL)
        {
            struct ValueType *PT = LinExprValueType(T->Left, 0);
            struct Value *MV = NULL;
            int off = 0, es = 4, eb = TypeInt;
            int base_reg, bit_width = T->BitWidth;
            if (PT != NULL && PT->Members != NULL
                && TableGet(PT->Members, T->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->BitWidth > 0)
                bit_width = MV->BitWidth;
            if (bit_width > 0)
            {
                if (LinParser != NULL)
                {
                    if (T->SourceLine > 0) LinParser->Line = T->SourceLine;
                    if (T->Ident != NULL) ParserSetCaretOnIdent(LinParser, T->Ident);
                    ProgramFail(LinParser, "cannot take address of bitfield");
                }
                return 0;
            }
            if (PT == NULL) return 0;
            if (T->Op == TokenArrow)
            {
                if (!LowerRec(T->Left, Code, Cap, N, Next, &base_reg)) return 0;
            }
            else
            {
                if (T->Left->Kind == IrExCall) return 0; /* non-lvalue temporary */
                if (!LinEmitAddressOf(T->Left, Code, Cap, N, Next, &base_reg)) return 0;
            }
            if (!IrLowerMemberOffEs(PT, T->Ident, &off, &es, &eb)) return 0;
            if (off != 0)
            {
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)base_reg; In.B = 0;
                In.Tok = 0; In.Imm = off;
                return Emit(Code, Cap, N, In);
            }
            *Dest = base_reg;
            (void)MV;
            return 1;
        }
        /* Address of an indexed array/pointer member, e.g. &s.values[i],
         * &n.item.values[i], or p->values[i]. Resolve every stride from the
         * declared member type, including row strides for multidimensional
         * members. Pointer-valued members are evaluated as pointers; array
         * members decay from the address of the field itself. */
        if (T->Kind == IrExIndex && T->Right != NULL)
        {
            IrExpr *Chain[IR_MAX_ARRAY_DIMS];
            IrExpr *Root = T;
            struct ValueType *MemberType, *CurType;
            int nc = 0, base_reg = -1, d;
            while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
            {
                Chain[nc++] = Root;
                Root = Root->Left;
            }
            if (Root != NULL && Root->Kind == IrExMember && Root->Left != NULL
                && Root->Ident != NULL && nc > 0)
            {
                struct ValueType *ParentType = LinExprValueType(Root->Left, 0);
                int member_off = 0, member_es = 4, member_eb = TypeInt;
                MemberType = LinExprValueType(Root, 0);
                if (ParentType == NULL || MemberType == NULL) return 0;
                if (MemberType->Base == TypePointer)
                {
                    if (!LowerRec(Root, Code, Cap, N, Next, &base_reg)) return 0;
                }
                else if (MemberType->Base == TypeArray)
                {
                    if (Root->Op == TokenArrow)
                    {
                        if (!LowerRec(Root->Left, Code, Cap, N, Next, &base_reg)) return 0;
                    }
                    else if (!LinEmitAddressOf(Root->Left, Code, Cap, N, Next, &base_reg))
                        return 0;
                    if (!IrLowerMemberOffEs(ParentType, Root->Ident,
                                            &member_off, &member_es, &member_eb))
                        return 0;
                    if (member_off != 0)
                    {
                        int shifted = AllocReg(Next);
                        if (shifted < 0) return 0;
                        In.Op = IrLinAddI; In.Dest = (unsigned char)shifted;
                        In.A = (unsigned char)base_reg; In.B = 0;
                        In.Tok = 0; In.Imm = member_off;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        base_reg = shifted;
                    }
                }
                else return 0;
                CurType = MemberType;
                /* Chain is outermost-to-innermost; process source indices in
                 * order (left-to-right) while peeling one array level each time. */
                for (d = nc - 1; d >= 0; --d)
                {
                    struct ValueType *ElemType;
                    int stride, ix_reg, scale_reg, off_reg, addr_reg;
                    if ((CurType->Base != TypeArray && CurType->Base != TypePointer)
                        || CurType->FromType == NULL) return 0;
                    ElemType = CurType->FromType;
                    stride = LinTypeObjectSize(ElemType);
                    if (stride <= 0) stride = Chain[d]->ElemSize;
                    if (stride <= 0) return 0;
                    if (Chain[d]->Right == NULL
                        || !LowerRec(Chain[d]->Right, Code, Cap, N, Next, &ix_reg)) return 0;
                    scale_reg = AllocReg(Next);
                    off_reg = AllocReg(Next);
                    addr_reg = AllocReg(Next);
                    if (scale_reg < 0 || off_reg < 0 || addr_reg < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)scale_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = stride;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
                    In.A = (unsigned char)ix_reg; In.B = (unsigned char)scale_reg;
                    In.Tok = TokenAsterisk; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)addr_reg;
                    In.A = (unsigned char)base_reg; In.B = (unsigned char)off_reg;
                    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr_reg;
                    CurType = ElemType;
                }
                *Dest = base_reg;
                return 1;
            }
        }
        /* Address of an element in a global multidimensional array.
         * Build the row-major byte offset from the declared array type rather
         * than relying on the leaf ElemSize stamped on the outer Index node.
         * This handles &g[i][j], &g[i][j][k], and deeper declared arrays with
         * the same rule: each index is scaled by sizeof(the remaining subarray).
         */
        if (T->Kind == IrExIndex && T->Right != NULL)
        {
            IrExpr *Root = T;
            IrExpr *Chain[IR_MAX_ARRAY_DIMS];
            int nc = 0, d, base_reg;
            struct Value *GV = NULL;
            struct ValueType *CurType;
            int gi;
            while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
            {
                Chain[nc++] = Root;
                Root = Root->Left;
            }
            if (Root != NULL && Root->Kind == IrExIndex)
                return 0; /* do not silently truncate an over-deep index chain */
            if (Root != NULL && Root->Kind == IrExIdent && Root->LocalSlot < 0
                && Root->ParamSlot < 0 && Root->Ident != NULL && nc > 0
                && LinParser != NULL)
            {
                GV = IrGlobalVal(LinParser, Root);
                if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray)
                {
                    CurType = GV->Typ;
                    /* Chain is outermost-to-innermost; reverse it so each
                     * index is evaluated while CurType names its array level. */
                    for (d = 0; d < nc / 2; d++)
                    {
                        IrExpr *tmp = Chain[d];
                        Chain[d] = Chain[nc - 1 - d];
                        Chain[nc - 1 - d] = tmp;
                    }
                    gi = GlobPush(Root);
                    if (gi < 0) return 0;
                    base_reg = AllocReg(Next);
                    if (base_reg < 0) return 0;
                    In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base_reg;
                    In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    for (d = 0; d < nc; d++)
                    {
                        int ix_reg, stride, scale_reg, off_reg, addr_reg;
                        struct ValueType *ElemType;
                        if (CurType == NULL || CurType->Base != TypeArray
                            || CurType->FromType == NULL || Chain[d]->Right == NULL)
                            return 0;
                        ElemType = CurType->FromType;
                        stride = LinTypeObjectSize(ElemType);
                        if (stride <= 0 && ElemType->Base == TypeArray
                            && ElemType->ArraySize > 0 && ElemType->FromType != NULL)
                        {
                            int leaf = LinTypeObjectSize(ElemType->FromType);
                            if (leaf > 0 && ElemType->ArraySize <= 2147483647 / leaf)
                                stride = ElemType->ArraySize * leaf;
                        }
                        if (stride <= 0 || !LowerRec(Chain[d]->Right, Code, Cap,
                                                      N, Next, &ix_reg))
                            return 0;
                        scale_reg = AllocReg(Next);
                        off_reg = AllocReg(Next);
                        addr_reg = AllocReg(Next);
                        if (scale_reg < 0 || off_reg < 0 || addr_reg < 0) return 0;
                        In.Op = IrLinImm; In.Dest = (unsigned char)scale_reg;
                        In.A = In.B = 0; In.Tok = 0; In.Imm = stride;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
                        In.A = (unsigned char)ix_reg; In.B = (unsigned char)scale_reg;
                        In.Tok = TokenAsterisk; In.Imm = 0;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        In.Op = IrLinBin; In.Dest = (unsigned char)addr_reg;
                        In.A = (unsigned char)base_reg; In.B = (unsigned char)off_reg;
                        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        base_reg = addr_reg;
                        CurType = ElemType;
                    }
                    *Dest = base_reg;
                    return 1;
                }
            }
        }
        /* &a[i] or &m[i][j]... local array (1D or multi-dim flatten). */
        if (T->Kind == IrExIndex && T->Right != NULL)
        {
            IrExpr *Root = T;
            IrExpr *Chain[IR_MAX_ARRAY_DIMS];
            int nc = 0, d, nd = 0, dims[IR_MAX_ARRAY_DIMS];
            int idx_regs[IR_MAX_ARRAY_DIMS];
            int idx, base, sc, off, es, flat_reg, tmp_reg, leaf_es = 4;
            IrLinInst Bin;
            while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
            {
                Chain[nc++] = Root;
                Root = Root->Left;
            }
            if (Root != NULL && Root->Kind == IrExIdent && Root->LocalSlot >= 0
                && Root->LocalSlot < IR_CALL_MAX_LOCALS && nc >= 1)
            {
                if (nc == 1)
                {
                    /* es = element stride: scalar 1/2/4/8 OR struct sizeof (aos). */
                    es = T->ElemSize > 0 ? T->ElemSize : 0;
                    if (es <= 0 && IrCallDepth > 0
                        && IrCallStack[IrCallDepth - 1]->LocalElemSize[Root->LocalSlot] > 0)
                        es = (int)IrCallStack[IrCallDepth - 1]->LocalElemSize[Root->LocalSlot];
                    if (es <= 0 && IrCompLocalLeafEsOrFrame(Root->LocalSlot) > 0)
                        es = IrCompLocalLeafEsOrFrame(Root->LocalSlot);
                    if (es <= 0) es = 4;
                    if (!LowerRec(T->Right, Code, Cap, N, Next, &idx)) return 0;
                    base = AllocReg(Next);
                    if (base < 0) return 0;
                    In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = Root->LocalSlot;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    sc = AllocReg(Next);
                    if (sc < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = es;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    off = AllocReg(Next);
                    if (off < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)off;
                    In.A = (unsigned char)idx; In.B = (unsigned char)sc;
                    In.Tok = TokenAsterisk; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    *Dest = AllocReg(Next);
                    if (*Dest < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
                    In.A = (unsigned char)base; In.B = (unsigned char)off;
                    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                    return Emit(Code, Cap, N, In);
                }
                /* Multi-dim: same flatten as a[i][j]++ (outer→inner, dims[1..]). */
                for (d = 0; d < nc / 2; d++)
                {
                    IrExpr *tmp = Chain[d];
                    Chain[d] = Chain[nc - 1 - d];
                    Chain[nc - 1 - d] = tmp;
                }
                for (d = 0; d < IR_MAX_ARRAY_DIMS; d++) dims[d] = 0;
                if (IrCallDepth > 0)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    nd = (int)Fr->LocalNDims[Root->LocalSlot];
                    leaf_es = Fr->LocalElemSize[Root->LocalSlot] > 0
                        ? (int)Fr->LocalElemSize[Root->LocalSlot] : 4;
                    for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                        dims[d] = Fr->LocalDims[Root->LocalSlot][d];
                }
                if (nd < 2 && IrCompLocalNDimsOrFrame(Root->LocalSlot) >= 2)
                {
                    int slot = Root->LocalSlot;
                    nd = (int)IrCompLocalNDimsOrFrame(slot);
                    leaf_es = IrCompLocalLeafEsOrFrame(slot) > 0 ? IrCompLocalLeafEsOrFrame(slot) : 4;
                    for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                        dims[d] = IrCompLocalDimOrFrame(slot, d);
                }
                if (nd == nc && nd >= 2)
                {
                    int ok_dims = 1;
                    for (d = 1; d < nd; d++)
                        if (dims[d] <= 0) ok_dims = 0;
                    if (ok_dims)
                    {
                        for (d = 0; d < nc; d++)
                            if (!LowerRec(Chain[d]->Right, Code, Cap, N, Next, &idx_regs[d]))
                                return 0;
                        flat_reg = idx_regs[0];
                        for (d = 1; d < nd; d++)
                        {
                            int dim_reg = AllocReg(Next);
                            if (dim_reg < 0) return 0;
                            Bin.Op = IrLinImm; Bin.Dest = (unsigned char)dim_reg;
                            Bin.A = Bin.B = 0; Bin.Tok = 0; Bin.Imm = dims[d];
                            if (!Emit(Code, Cap, N, Bin)) return 0;
                            tmp_reg = AllocReg(Next);
                            if (tmp_reg < 0) return 0;
                            Bin.Op = IrLinBin; Bin.Dest = (unsigned char)tmp_reg;
                            Bin.A = (unsigned char)flat_reg; Bin.B = (unsigned char)dim_reg;
                            Bin.Tok = TokenAsterisk; Bin.Imm = 0;
                            if (!Emit(Code, Cap, N, Bin)) return 0;
                            flat_reg = AllocReg(Next);
                            if (flat_reg < 0) return 0;
                            Bin.Op = IrLinBin; Bin.Dest = (unsigned char)flat_reg;
                            Bin.A = (unsigned char)tmp_reg; Bin.B = (unsigned char)idx_regs[d];
                            Bin.Tok = TokenPlus; Bin.Imm = 0;
                            if (!Emit(Code, Cap, N, Bin)) return 0;
                        }
                        if (leaf_es != 1 && leaf_es != 2 && leaf_es != 4 && leaf_es != 8)
                            leaf_es = 4;
                        base = AllocReg(Next);
                        if (base < 0) return 0;
                        In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base;
                        In.A = In.B = 0; In.Tok = 0; In.Imm = Root->LocalSlot;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        sc = AllocReg(Next);
                        if (sc < 0) return 0;
                        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                        In.A = In.B = 0; In.Tok = 0; In.Imm = leaf_es;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        off = AllocReg(Next);
                        if (off < 0) return 0;
                        In.Op = IrLinBin; In.Dest = (unsigned char)off;
                        In.A = (unsigned char)flat_reg; In.B = (unsigned char)sc;
                        In.Tok = TokenAsterisk; In.Imm = 0;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        *Dest = AllocReg(Next);
                        if (*Dest < 0) return 0;
                        In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
                        In.A = (unsigned char)base; In.B = (unsigned char)off;
                        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                        return Emit(Code, Cap, N, In);
                    }
                }
            }
        }
        /* &g[i] global array: load global base + i*es (scalar or aos stride). */
        if (T->Kind == IrExIndex && T->Left && T->Left->Kind == IrExIdent
            && T->Left->LocalSlot < 0 && T->Left->ParamSlot < 0
            && T->Left->Ident != NULL && T->Right != NULL
            && LinParser != NULL && IrExecIsVmProd())
        {
            int idx, base, sc, off, es, gi;
            struct Value *GV = IrGlobalVal(LinParser, T->Left);
            if (GV == NULL || GV->Typ == NULL) return 0;
            es = T->ElemSize > 0 ? T->ElemSize : 0;
            if (es <= 0 && GV->Typ->Base == TypeArray && GV->Typ->FromType != NULL
                && GV->Typ->FromType->Sizeof > 0)
                es = (int)GV->Typ->FromType->Sizeof;
            if (es <= 0) es = 4;
            if (!LowerRec(T->Right, Code, Cap, N, Next, &idx)) return 0;
            gi = GlobPush(T->Left);
            if (gi < 0) return 0;
            base = AllocReg(Next);
            if (base < 0) return 0;
            In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base;
            In.A = In.B = 0; In.Tok = 1;
            In.Imm = gi;
            if (!Emit(Code, Cap, N, In)) return 0;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            off = AllocReg(Next);
            if (off < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off;
            In.A = (unsigned char)idx; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)base; In.B = (unsigned char)off;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            return Emit(Code, Cap, N, In);
        }
        /* &g: address of global scalar/array payload */
        if (T->Kind == IrExIdent && T->LocalSlot < 0 && T->ParamSlot < 0
            && T->Ident != NULL && LinParser != NULL)
        {
            struct Value *GV = IrGlobalIdentSoft(LinParser, T->Ident);
            int gi;
            if (GV == NULL)
                return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            /* A function designator is represented by its Value identity in
             * indirect-call slots.  Its Value payload is function metadata,
             * not a data address; &fn must therefore use the same Tok=5
             * encoding as direct fptr stores and call dispatch. */
            if (GV->Typ != NULL && GV->Typ->Base == TypeFunction)
            {
                In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
                In.A = In.B = 0; In.Tok = 5;
                In.Imm = (long long)(intptr_t)GV;
            }
            else if (GV->Val == NULL)
                return 0;
            /* For array globals LoadGlobal Tok=1 already returns payload addr.
             * For scalar, Imm of (intptr_t)GV->Val is the address of the cell. */
            else if (GV->Typ != NULL && GV->Typ->Base == TypeArray)
            {
                gi = GlobPush(T);
                if (gi < 0) return 0;
                In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)*Dest;
                In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
            }
            else
            {
                In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
                In.A = In.B = 0; In.Tok = 0;
                In.Imm = (long long)(intptr_t)(void *)GV->Val;
            }
            return Emit(Code, Cap, N, In);
        }
        return 0;
    }
    if (E->Kind == IrExUnary && E->Op == TokenAsterisk && E->Left != NULL)
    {
        /* Nested **pp / ***ppp: each Unary* lowers Left to a pointer reg then
         * Tok=2 load with this level's ElemSize (sizeof(void*) for intermediate
         * pointer cells, sizeof(T) for the final scalar). Provenance is the
         * address value carried in the register — same model as single *p.
         * Forms that mix & into the deref chain (****&pppx) need the full
         * pointer-type chain on DeclFromType; without it intermediate es is
         * wrong on LP64 and SEGV. Refuse that shape so tree remains oracle. */
        if (IrExecIsVmProd() && E->Left != NULL)
        {
            IrExpr *t = E->Left;
            int nest = 0;
            while (t != NULL && t->Kind == IrExUnary && t->Op == TokenAsterisk)
            {
                nest++;
                t = t->Left;
            }
            /* **f() allowed: annotate sizes each * from Call return type
             * (same model as **pp). nest/Call no longer refuse. */
        }
        /* *p / **pp / *f(...): load pointee. When result is pointer (TF_PTR)
         * (multi-level), es = sizeof(void*) so the pointer bits are loaded.
         * Avoids re-eval of Call in Step (safe under EXEC=both). */
        if (E->Left->Kind == IrExCall)
        {
            int pr, es;
            if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
            es = E->ElemSize > 0 ? E->ElemSize : 1;
            if (es != 1 && es != 2 && es != 4 && es != 8)
                es = 1;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)pr; In.B = 0;
            In.Tok = 2; /* Tok=2: pointer in R[A], es in Imm */
            In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            /* CSE store for *p where p is a local Ident. */
            if (E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                extern int LinCseN;
                extern int LinCseSlot[];
                extern unsigned char LinCseSlotPinned[];
                extern int LinCseReg[];
                int key = 1000 + E->Left->LocalSlot;
                if (LinCseN < IR_CALL_MAX_LOCALS)
                { LinCseSlot[LinCseN] = key; LinCseReg[LinCseN] = *Dest; LinCsePinReg(*Dest); LinCseSlotPinned[LinCseN] = 1; LinCseN++; }
            }
            return 1;
        }
        /* *p / *o.ip / *(p+i) / *a[i]: lower pointer expr to reg, Tok=2 load.
         * Member base (o.ip) lowers via Tok=1 to pointer bits, then Tok=2. */
        if (E->Left->Kind == IrExIdent
            || E->Left->Kind == IrExBinOp
            || E->Left->Kind == IrExUnary
            || E->Left->Kind == IrExIndex
            || E->Left->Kind == IrExMember)
        {
            int pr, es;
            /* CSE for *p where p is a local Ident. */
            if (E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                extern int LinCseN;
                extern int LinCseSlot[];
                extern int LinCseReg[];
                int si;
                int key = 1000 + E->Left->LocalSlot; /* distinct from LoadLocal */
                for (si = 0; si < LinCseN; si++)
                    if (LinCseSlot[si] == key && LinCseReg[si] >= 0)
                    { *Dest = LinCseReg[si]; return 1; }
            }
            IrExpr *Base = E->Left;
            if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
            es = E->ElemSize > 0 ? E->ElemSize : 0;
            if (E->Left->Kind == IrExIndex)
            {
                IrExpr *Root = E->Left;
                while (Root != NULL && Root->Kind == IrExIndex)
                    Root = Root->Left;
                if (es <= 0 && Root != NULL && Root->Kind == IrExIdent
                    && IrCallDepth > 0 && Root->LocalSlot >= 0
                    && Root->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    int slot = Root->LocalSlot;
                    int ps = IrCompLocalPointeeSizeOrFrame(slot);
                    if (ps > 0)
                        es = ps;
                    else if (Fr->LocalArraySize[slot] > 0
                        && Fr->LocalElemSize[slot] == (int)sizeof(void *))
                        es = 4;
                }
            }
            if (es <= 0)
                es = E->Left->ElemSize;
            /* Nested *(*(pp)): intermediate Unary* yields a pointer cell —
             * load sizeof(void*) unless this level already knows a scalar size. */
            if (es <= 0 && E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk)
            {
                if (E->TypeFlags & IR_TF_PTR)
                    es = (int)sizeof(void *);
                else if (E->Left->TypeFlags & IR_TF_PTR)
                    es = E->Left->ElemSize > 0 ? E->Left->ElemSize : (int)sizeof(void *);
            }
            /* Walk BinOp (a+i)/(p+k) to the array/pointer Ident so local
             * uchar/short arrays get es=1/2 under EXEC=both (otherwise
             * default 4 → unaligned/OOB IrLoadPtrElem SEGV). */
            while (Base != NULL && Base->Kind == IrExBinOp
                   && (Base->Op == TokenPlus || Base->Op == TokenMinus)
                   && Base->Left != NULL)
            {
                if (Base->Left->Kind == IrExIdent
                    || (Base->Left->TypeFlags & IR_TF_PTR)
                    || Base->Left->ElemSize > 0)
                    Base = Base->Left;
                else if (Base->Right != NULL
                         && (Base->Right->Kind == IrExIdent
                             || (Base->Right->TypeFlags & IR_TF_PTR)))
                    Base = Base->Right;
                else
                    break;
            }
            if (es <= 0 && Base != NULL && Base->ElemSize > 0)
                es = Base->ElemSize;
            if (es <= 0 && Base != NULL && Base->Kind == IrExIdent
                && IrCallDepth > 0 && Base->LocalSlot >= 0
                && Base->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (Fr->LocalElemSize[Base->LocalSlot] > 0)
                    es = Fr->LocalElemSize[Base->LocalSlot];
                else if (Fr->LocalArraySize[Base->LocalSlot] > 0
                         && Fr->LocalFromType[Base->LocalSlot] != NULL
                         && Fr->LocalFromType[Base->LocalSlot]->Sizeof > 0
                         && Fr->LocalFromType[Base->LocalSlot]->Sizeof < 256)
                    es = (int)Fr->LocalFromType[Base->LocalSlot]->Sizeof;
            }
            if (es <= 0 && Base != NULL && Base->Kind == IrExIdent
                && Base->ParamSlot >= 0
                && IrCallDepth > 0
                && Base->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
            {
                struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[Base->ParamSlot];
                if (PT && PT->Base == TypePointer && PT->FromType
                    && PT->FromType->Sizeof > 0)
                    es = (int)PT->FromType->Sizeof;
            }
            /* char* / array decay often ElemSize=0 at lower — default 1 */
            if (es <= 0 && Base != NULL && Base->Kind == IrExIdent
                && ((Base->TypeFlags & IR_TF_PTR) || Base->ElemSize <= 0))
                es = 1;
            /* Multi-level *pp: result is pointer → load pointer-sized value. */
            if (E->TypeFlags & IR_TF_PTR)
                es = (int)sizeof(void *);
            if (es != 1 && es != 2 && es != 4 && es != 8)
                es = 4;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)pr; In.B = 0;
            /* B=1: pointee is unsigned (zero-extend). */
            if (E->CanonicalFlags & IR_CF_UNSIGNED)
                In.B = 1;
            else if (Base != NULL && Base->Kind == IrExIdent
                     && Base->LocalSlot >= 0 && IrCallDepth > 0
                     && Base->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fru = IrCallStack[IrCallDepth - 1];
                struct ValueType *PT = Fru->LocalTyp[Base->LocalSlot];
                if (PT == NULL) PT = Fru->LocalFromType[Base->LocalSlot];
                if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL
                    && IrTypeIsUnsignedBase(PT->FromType->Base))
                    In.B = 1;
            }
            else if (Base != NULL && Base->Kind == IrExIdent && Base->ParamSlot >= 0
                     && Base->ParamSlot < IR_CALL_MAX_PARAMS)
                In.B = (unsigned char)(2 + Base->ParamSlot);
            In.Tok = 2; In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
        if (IrExecIsVmProd())
            return 0;
        if (IdxN >= 32) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = 0; In.B = 0; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(E);
        return Emit(Code, Cap, N, In);
    }
    /* ret()->m: Call returns T*, lower Call once, add member offset, Tok=2 load.
     * Pure linear — no re-eval of Call at Step. */
    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->Left->Kind == IrExCall)
    {
        int pr, addr, off = 0, es = 4, eb = TypeInt;
        struct ValueType *ST = IrLowerCallReturnStruct(E->Left);
        if (ST == NULL || !IrLowerMemberOffEs(ST, E->Ident, &off, &es, &eb))
            return 0;
        if (E->BitWidth > 0) return 0; /* bitfield via call: keep tree */
        if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        if (off != 0)
        {
            In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)pr; In.B = 0;
            In.Tok = 0; In.Imm = off;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        else
            addr = pr;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)addr; In.B = 0;
        In.Tok = 2; In.Imm = es;
        return Emit(Code, Cap, N, In);
    }
    /* gp->m / (*gp).m: global T* Ident. Pure linear LoadGlobal + offset. */
    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && LinParser != NULL)
    {
        IrExpr *GPtr = NULL;
        if (E->Left->Kind == IrExIdent
            && E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0
            && E->Left->Ident != NULL)
            GPtr = E->Left;
        else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                 && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
                 && E->Left->Left->LocalSlot < 0 && E->Left->Left->ParamSlot < 0
                 && E->Left->Left->Ident != NULL)
            GPtr = E->Left->Left; /* (*gp).m */
        if (GPtr != NULL)
        {
        struct Value *GV = IrGlobalValSoft(LinParser, GPtr);
        struct ValueType *ST = NULL;
        int pr, addr, off = 0, es = 4, eb = TypeInt, gi;
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypePointer
            && GV->Typ->FromType != NULL)
            ST = GV->Typ->FromType;
        else if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeStruct)
            ST = GV->Typ; /* global struct: global.m */
        /* Array member decay first: gp->name → address (not load first bytes). */
        if (ST != NULL && ST->Members != NULL)
        {
            struct Value *MV = NULL;
            if (TableGet(ST->Members, E->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->Typ != NULL && MV->Typ->Base == TypeArray
                && MV->Val != NULL)
            {
                if (!IrResolveMemberOffsetValue(MV, &off))
                    return 0;
                gi = GlobPush(GPtr);
                if (gi < 0) return 0;
                pr = AllocReg(Next);
                if (pr < 0) return 0;
                In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)pr;
                In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                if (!Emit(Code, Cap, N, In)) return 0;
                if (off == 0)
                {
                    *Dest = pr;
                    return 1;
                }
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)pr; In.B = 0;
                In.Tok = 0; In.Imm = off;
                return Emit(Code, Cap, N, In);
            }
        }
        /* Scalar/pointer gp->m: load pointee field. */
        if (ST != NULL && IrLowerMemberOffEs(ST, E->Ident, &off, &es, &eb)
            && E->BitWidth <= 0)
        {
            gi = GlobPush(GPtr);
            if (gi < 0) return 0;
            pr = AllocReg(Next);
            if (pr < 0) return 0;
            In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)pr;
            In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (off != 0)
            {
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)pr; In.B = 0;
                In.Tok = 0; In.Imm = off;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else
                addr = pr;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr; In.B = 0;
            In.Tok = 2; In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
        } /* GPtr != NULL */
    }
    /* adj[i]->next->m: nested Member through pointer field of T* array elem. */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->Left->Kind == IrExMember
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIndex
        && E->Left->Left->Left != NULL && E->Left->Left->Left->Kind == IrExIdent
        && E->Left->Left->Left->LocalSlot >= 0
        && E->BitWidth <= 0
        /* GUARD: dual-read array-of-pointer metadata */
        && IrCompLocalElemIsPtrOrFrame(E->Left->Left->Left->LocalSlot))
    {
        int pr, addr, off1 = 0, off2 = 0, es1 = 4, es2 = 4, eb1 = TypeInt, eb2 = TypeInt;
        int slot = E->Left->Left->Left->LocalSlot;
        struct ValueType *ST = NULL, *ST2 = NULL;
        if (slot >= 0 && slot < IR_CALL_MAX_LOCALS)
        {
            ST = IrLinEmitLocalType(slot);
            if (ST != NULL)
            {
                if (ST->Base == TypeArray && ST->FromType
                    && ST->FromType->Base == TypePointer
                    && ST->FromType->FromType)
                    ST = ST->FromType->FromType;
                else if (ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
            if (ST == NULL && IrCallDepth > 0)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                ST = Fr->LocalFromType[slot];
                if (ST != NULL && ST->Base == TypeArray && ST->FromType
                    && ST->FromType->Base == TypePointer && ST->FromType->FromType)
                    ST = ST->FromType->FromType;
                else if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
        }
        if (ST != NULL
            && IrLowerMemberOffEs(ST, E->Left->Ident, &off1, &es1, &eb1)
            && es1 == (int)sizeof(void *))
        {
            /* load adj[i]->next as pointer, then ->m */
            if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
            if (IrLowerMemberOffEs(ST, E->Left->Ident, &off1, &es1, &eb1)
                && ST->Members != NULL)
            {
                struct Value *MV = NULL;
                if (TableGet(ST->Members, E->Left->Ident, &MV, NULL, NULL, NULL)
                    && MV != NULL && MV->Typ != NULL
                    && MV->Typ->Base == TypePointer && MV->Typ->FromType)
                    ST2 = MV->Typ->FromType;
            }
            if (ST2 != NULL && IrLowerMemberOffEs(ST2, E->Ident, &off2, &es2, &eb2))
            {
                /* pr already holds adj[i]->next pointer value from LowerRec Member */
                if (off2 != 0)
                {
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)pr; In.B = 0;
                    In.Tok = 0; In.Imm = off2;
                    if (!Emit(Code, Cap, N, In)) return 0;
                }
                else
                    addr = pr;
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)addr; In.B = 0;
                In.Tok = 2; In.Imm = es2;
                return Emit(Code, Cap, N, In);
            }
        }
    }
    /* adj[i]->m: Index of local T* array then Member.
     * Load pointer element, add member offset, Tok=2 load. */
    /* aop[i]->m only when element is pointer — NOT struct with Sizeof==void*. */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && E->Left->Left->LocalSlot >= 0
        && E->Left->Right != NULL && E->BitWidth <= 0
        /* GUARD: dual-read array-of-pointer metadata */
        && IrCompLocalElemIsPtrOrFrame(E->Left->Left->LocalSlot))
    {
        int pr, addr, off = 0, es = 4, eb = TypeInt, slot;
        struct ValueType *ST = NULL;
        slot = E->Left->LocalSlot >= 0 ? E->Left->LocalSlot : E->Left->Left->LocalSlot;
        /* Element type: pointee of T* array. */
        if (slot >= 0 && slot < IR_CALL_MAX_LOCALS)
        {
            if (IrCallDepth > 0)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (Fr->LocalElemIsPtr[slot] && Fr->LocalFromType[slot] != NULL)
                {
                    ST = Fr->LocalFromType[slot];
                    if (ST->Base == TypePointer && ST->FromType)
                        ST = ST->FromType;
                    else if (ST->Base == TypeArray && ST->FromType
                             && ST->FromType->Base == TypePointer
                             && ST->FromType->FromType)
                        ST = ST->FromType->FromType;
                }
            }
            if (ST == NULL && IrLinEmitLocalType(slot) != NULL)
            {
                ST = IrLinEmitLocalType(slot);
                if (ST->Base == TypeArray && ST->FromType
                    && ST->FromType->Base == TypePointer
                    && ST->FromType->FromType)
                    ST = ST->FromType->FromType;
                else if (ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
        }
        if (ST != NULL && IrLowerMemberOffEs(ST, E->Ident, &off, &es, &eb))
        {
            int idx_r, base_r, sc, ioff, paddr;
            int wes = (int)sizeof(void *);
            /* Explicit word load of adj[i] — avoid generic Index es=4 trap. */
            if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_r)) return 0;
            base_r = AllocReg(Next);
            if (base_r < 0) return 0;
            In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_r;
            In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
            if (!Emit(Code, Cap, N, In)) return 0;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = wes;
            if (!Emit(Code, Cap, N, In)) return 0;
            ioff = AllocReg(Next);
            if (ioff < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)ioff;
            In.A = (unsigned char)idx_r; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            paddr = AllocReg(Next);
            if (paddr < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)paddr;
            In.A = (unsigned char)base_r; In.B = (unsigned char)ioff;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            pr = AllocReg(Next);
            if (pr < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)pr;
            In.A = (unsigned char)paddr; In.B = 0;
            In.Tok = 2; In.Imm = wes; /* load pointer word */
            if (!Emit(Code, Cap, N, In)) return 0;
            if (off != 0)
            {
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)pr; In.B = 0;
                In.Tok = 0; In.Imm = off;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else
                addr = pr;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr; In.B = 0;
            In.Tok = 2; In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
        /* Array member adj[i]->name: address = ptr + off. */
        if (ST != NULL && ST->Members != NULL)
        {
            struct Value *MV = NULL;
            if (TableGet(ST->Members, E->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->Typ != NULL && MV->Typ->Base == TypeArray
                && MV->Val != NULL)
            {
                if (!IrResolveMemberOffsetValue(MV, &off))
                    return 0;
                if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
                if (off == 0) { *Dest = pr; return 1; }
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)pr; In.B = 0;
                In.Tok = 0; In.Imm = off;
                return Emit(Code, Cap, N, In);
            }
        }
    }
    /* s.arr[i]->m / p->arr[i]->m: Index of array-of-pointer Member then field.
     * Element is T*; load pointer word via Index, then Tok=2 field load.
     * Distinct from aos[i].m (Tok=1 IrSafeMemberAddr) which treats Index as
     * address of a struct element — wrong for pointer arrays (silent garbage). */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExMember
        && E->Left->Left->Ident != NULL
        && E->Left->Right != NULL && E->BitWidth <= 0)
    {
        int pr, addr, off = 0, es = 4, eb = TypeInt;
        struct ValueType *ST = NULL;
        struct Value *MV = NULL;
        IrExpr *ArrM = E->Left->Left; /* the Member that is the array-of-ptr */
        IrExpr *Base = ArrM->Left;
        IrExpr *Chain[8];
        int nc = 0, ci, star_peels = 0;
        /* Resolve type of ArrM: walk from root Ident/Param/Global through nested Members. */
        while (Base != NULL && Base->Kind == IrExMember && nc < 8)
        {
            Chain[nc++] = Base;
            Base = Base->Left;
        }
        while (Base != NULL && Base->Kind == IrExUnary && Base->Op == TokenAsterisk
               && Base->Left != NULL)
        {
            star_peels++;
            Base = Base->Left;
        }
        if (Base != NULL && Base->Kind == IrExIdent)
        {
            if (Base->LocalSlot >= 0 && Base->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                /* GUARD: dual-read FromType */
                ST = IrCompLocalFromTypeOrFrame(Base->LocalSlot);
                /* local struct blob: ST is the struct type directly */
                if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType; /* T* local holding struct* */
            }
            else if (Base->ParamSlot >= 0 && Base->ParamSlot < IR_CALL_MAX_PARAMS)
            {
                if (IrCallDepth > 0 && Base->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                {
                    ST = IrCallStack[IrCallDepth - 1]->ParamTyp[Base->ParamSlot];
                    if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                        ST = ST->FromType;
                }
                if (ST == NULL && CompileSlot != NULL && CompileSlot->Func != NULL
                    && CompileSlot->Func->Val != NULL
                    && CompileSlot->Func->Val->FuncDef.ParamType != NULL
                    && Base->ParamSlot < CompileSlot->Func->Val->FuncDef.NumParams)
                {
                    ST = CompileSlot->Func->Val->FuncDef.ParamType[Base->ParamSlot];
                    if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                        ST = ST->FromType;
                }
            }
            else if (Base->Ident != NULL && LinParser != NULL)
            {
                struct Value *GV = IrGlobalValSoft(LinParser, Base);
                if (GV != NULL && GV->Typ != NULL)
                {
                    ST = GV->Typ;
                    if (ST->Base == TypePointer && ST->FromType)
                        ST = ST->FromType;
                }
            }
            /* Walk nested Member chain to reach the struct that owns ArrM. */
            for (ci = nc - 1; ci >= 0 && ST != NULL; ci--)
            {
                struct Value *CM = NULL;
                if (ST->Members == NULL) { ST = NULL; break; }
                if (!TableGet(ST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                    || CM == NULL || CM->Typ == NULL)
                { ST = NULL; break; }
                ST = CM->Typ;
                if (ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
            /* ST is now the struct type owning ArrM; ArrM is array-of-ptr member. */
            if (ST != NULL && ST->Members != NULL
                && TableGet(ST->Members, ArrM->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->Typ != NULL && MV->Typ->Base == TypeArray
                && MV->Typ->FromType != NULL
                && MV->Typ->FromType->Base == TypePointer
                && MV->Typ->FromType->FromType != NULL)
            {
                /* Element pointee = Edge (for Edge *adj[]). */
                ST = MV->Typ->FromType->FromType;
                if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                    && IrLowerMemberOffEs(ST, E->Ident, &off, &es, &eb))
                {
                    /* Lower Index of array-of-ptr → pointer value in pr. */
                    if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
                    if (off != 0)
                    {
                        addr = AllocReg(Next);
                        if (addr < 0) return 0;
                        In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                        In.A = (unsigned char)pr; In.B = 0;
                        In.Tok = 0; In.Imm = off;
                        if (!Emit(Code, Cap, N, In)) return 0;
                    }
                    else
                        addr = pr;
                    *Dest = AllocReg(Next);
                    if (*Dest < 0) return 0;
                    In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                    In.A = (unsigned char)addr; In.B = 0;
                    In.Tok = 2; In.Imm = es;
                    return Emit(Code, Cap, N, In);
                }
            }
        }
    }
    /* p->arr: Param T* array member — pure linear when ParamType known. */
    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot >= 0 && E->Left->ParamSlot < IR_CALL_MAX_PARAMS)
    {
        struct ValueType *ST = NULL;
        struct Value *MV = NULL, *CF = NULL;
        int pr, off = 0, ps = E->Left->ParamSlot;
        if (IrCallDepth > 0 && ps < IrCallStack[IrCallDepth - 1]->NumParams)
        {
            ST = IrCallStack[IrCallDepth - 1]->ParamTyp[ps];
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        if (ST == NULL && CompileSlot != NULL && CompileSlot->Func != NULL)
            CF = CompileSlot->Func;
        if (ST == NULL && CF == NULL && FnActive != NULL)
            CF = FnActive->Func;
        if (ST == NULL && CF != NULL && CF->Val != NULL
            && CF->Val->FuncDef.ParamType != NULL
            && ps < CF->Val->FuncDef.NumParams)
        {
            ST = CF->Val->FuncDef.ParamType[ps];
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        if (ST != NULL && ST->Members != NULL
            && TableGet(ST->Members, E->Ident, &MV, NULL, NULL, NULL)
            && MV != NULL && MV->Typ != NULL && MV->Typ->Base == TypeArray
            && MV->Val != NULL)
        {
            if (!IrResolveMemberOffsetValue(MV, &off))
                return 0;
            pr = AllocReg(Next);
            if (pr < 0) return 0;
            In.Op = IrLinLoadLocal; In.Dest = (unsigned char)pr;
            In.A = In.B = 0; In.Tok = 0; In.Imm = -(ps + 1);
            if (!Emit(Code, Cap, N, In)) return 0;
            if (off == 0) { *Dest = pr; return 1; }
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinAddI; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)pr; In.B = 0;
            In.Tok = 0; In.Imm = off;
            return Emit(Code, Cap, N, In);
        }
    }
    /* ((T*)p)->m / (*(T*)p).m — cast carries pointee struct in CachedAddr
     * (same stamp the tree path uses). Pure linear: lower the pointer,
     * add member offset, Tok=2 load. Covers qsort comparators on rec*. */
    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->BitWidth <= 0)
    {
        IrExpr *Cast = NULL;
        if (E->Left->Kind == IrExUnary && E->Left->Op == TokenCast
            && E->Left->CachedAddr != NULL)
            Cast = E->Left;                          /* ((T*)p)->m */
        else if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                 && E->Left->Left != NULL
                 && E->Left->Left->Kind == IrExUnary
                 && E->Left->Left->Op == TokenCast
                 && E->Left->Left->CachedAddr != NULL)
            Cast = E->Left->Left;                    /* (*(T*)p).m */
        if (Cast != NULL)
        {
            struct ValueType *ST = (struct ValueType *)Cast->CachedAddr;
            int pr, addr, off = 0, es = 4, eb = TypeInt;
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
            if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                && IrLowerMemberOffEs(ST, E->Ident, &off, &es, &eb))
            {
                /* Pointer value is the cast operand (cast peels). */
                if (!LowerRec(Cast->Left, Code, Cap, N, Next, &pr))
                    return 0;
                if (off != 0)
                {
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)pr; In.B = 0;
                    In.Tok = 0; In.Imm = off;
                    if (!Emit(Code, Cap, N, In)) return 0;
                }
                else
                    addr = pr;
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)addr; In.B = 0;
                In.Tok = 2; In.Imm = es;
                return Emit(Code, Cap, N, In);
            }
        }
    }
    /* Pointer-producing base → field load (no IdxMeta).
     * Roots: (*p)/(**pp) with stars>=1, or Call returning T* (f()->m /
     * f()->next->m). Owned s.m excluded (stars==0 and not Call). */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->BitWidth <= 0
        && (E->Left->Kind == IrExUnary
            || E->Left->Kind == IrExMember
            || E->Left->Kind == IrExCall))
    {
        IrExpr *Root = E->Left;
        int stars = 0;
        struct ValueType *ST = NULL;
        int pr, addr, off = 0, es = 4, eb = TypeInt;
        IrExpr *Chain[8];
        int nc = 0, ci;
        while (Root != NULL && Root->Kind == IrExMember && nc < 8)
        {
            Chain[nc++] = Root;
            Root = Root->Left;
        }
        while (Root != NULL && Root->Kind == IrExUnary && Root->Op == TokenAsterisk)
        {
            stars++;
            Root = Root->Left;
        }
        if (Root != NULL && Root->Kind == IrExCall)
        {
            /* f()->… : return type is full (often T*). */
            if (Root->CachedFunc != NULL && Root->CachedFunc->Val != NULL)
                ST = Root->CachedFunc->Val->FuncDef.ReturnType;
            if (ST == NULL)
                ST = IrLowerCallReturnStruct(Root);
            else if (ST->Base == TypePointer && ST->FromType != NULL)
                ST = ST->FromType; /* T* → T for first arrow */
            /* else IrLowerCallReturnStruct already peels */
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        else if (stars >= 1 && Root != NULL && Root->Kind == IrExIdent
                 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* GUARD: dual-read */
            ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            {
                int si;
                for (si = 0; si < stars && ST != NULL; si++)
                {
                    if (ST->Base == TypePointer && ST->FromType)
                        ST = ST->FromType;
                    else
                        break;
                }
                if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
        }
        else if (stars == 0 && nc == 0 && Root != NULL && Root->Kind == IrExIdent
                 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* p->m: local T* Ident. Prefer pure Tok=2 over Tok=1/IrSafeMemberAddr
             * so base is LoadLocal (Locals[]) not possibly-stale LocalVal alone. */
            /* GUARD: dual-read */
            ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
            else if (LinCompileActive && CompileSlot != NULL
                     && CompileSlot->SlotIsPtr[Root->LocalSlot]
                     && CompileSlot->SlotFromType[Root->LocalSlot] != NULL)
            {
                ST = CompileSlot->SlotFromType[Root->LocalSlot];
                if (ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
            else
                ST = NULL;
        }
        else if (stars == 0 && nc >= 1 && Root != NULL && Root->Kind == IrExIdent
                 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* o.in->v: local struct, pointer member chain, no leading *. */
            /* GUARD: dual-read */
            ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType; /* local is T* */
            /* Require at least one pointer step in the chain (else owned s.m). */
            {
                int has_ptr_step = 0, ci2;
                struct ValueType *T2 = ST;
                for (ci2 = nc - 1; ci2 >= 0 && T2 != NULL; ci2--)
                {
                    struct Value *CM = NULL;
                    if (T2->Members == NULL) break;
                    if (!TableGet(T2->Members, Chain[ci2]->Ident, &CM, NULL, NULL, NULL)
                        || CM == NULL || CM->Typ == NULL) break;
                    if (CM->Typ->Base == TypePointer)
                        has_ptr_step = 1;
                    T2 = CM->Typ;
                    if (T2->Base == TypePointer && T2->FromType)
                        T2 = T2->FromType;
                }
                if (!has_ptr_step)
                    ST = NULL;
            }
        }
        /* Intermediate members: head()->next → type of next (Node*) → Node */
        for (ci = nc - 1; ci >= 0 && ST != NULL; ci--)
        {
            struct Value *CM = NULL;
            if (ST->Members == NULL) { ST = NULL; break; }
            if (!TableGet(ST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                || CM == NULL || CM->Typ == NULL)
            { ST = NULL; break; }
            ST = CM->Typ;
            if (ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        if (ST != NULL && IrLowerMemberOffEs(ST, E->Ident, &off, &es, &eb))
        {
            /* (*p).m (Dot): address is p. (*pp)->m (Arrow): address is *pp. */
            if (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                && E->Op == TokenDot && E->Left->Left != NULL)
            {
                if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &pr)) return 0;
            }
            else if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
            if (off != 0)
            {
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)pr; In.B = 0;
                In.Tok = 0; In.Imm = off;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else
                addr = pr;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr; In.B = 0;
            In.Tok = 2; In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
    }
    /* Initialization-aware direct path for v[i].in.a / v[i].m.
     * Tok=2 validates the final effective address against the owning frame's
     * LocalInitMap before loading, preserving diagnostics without a second
     * load or an extra IR instruction. */
    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && E->BitWidth <= 0)
    {
        IrExpr *Chain[8];
        int nm = 0;
        IrExpr *p = E;
        IrExpr *idxE = NULL, *idE = NULL;
        while (p != NULL && p->Kind == IrExMember && nm < 8)
        {
            Chain[nm++] = p;
            p = p->Left;
        }
        {
            IrExpr *IdxChain[IR_MAX_ARRAY_DIMS];
            int ni = 0, dix;
            int dims_loc[IR_MAX_ARRAY_DIMS];
            int nd_loc = 0;
            while (p != NULL && p->Kind == IrExIndex && ni < IR_MAX_ARRAY_DIMS)
            {
                IdxChain[ni++] = p;
                p = p->Left;
            }
            if (p != NULL && p->Kind == IrExIdent
                && ((p->LocalSlot >= 0 && p->LocalSlot < IR_CALL_MAX_LOCALS)
                    || (p->LocalSlot < 0 && p->ParamSlot < 0 && p->Ident != NULL)))
                idE = p;
            if (ni >= 1)
                idxE = IdxChain[0];
            for (dix = 0; dix < ni / 2; dix++)
            {
                IrExpr *tmpx = IdxChain[dix];
                IdxChain[dix] = IdxChain[ni - 1 - dix];
                IdxChain[ni - 1 - dix] = tmpx;
            }
        /* Multidim v[i][j].m (nm>=1,ni>=2) or nested v[i].in.a (nm>=2). */
        if (idE != NULL && ni >= 1 && idxE != NULL && idxE->Right != NULL && IdxN < 32
            && (nm >= 2 || ni >= 2))
        {
            struct ValueType *ST = NULL;
            int elem_sz = 0, total_off = 0, es = 4, eb = TypeInt;
            int idx_reg = 0, base_reg = 0, sc_reg = 0, off_reg = 0, addr = 0;
            int ci, ok = 1;
            int is_global = (idE->LocalSlot < 0 && idE->Ident != NULL);
            if (!is_global && idE->LocalSlot >= 0 && idE->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                /* GUARD: dual-read */
                ST = IrCompLocalFromTypeOrFrame(idE->LocalSlot);
            }
            else if (is_global && LinParser != NULL)
            {
                struct Value *GV = IrGlobalIdentSoft(LinParser, idE->Ident);
                if (GV != NULL && GV->Typ != NULL)
                    ST = GV->Typ;
            }
            {
                struct ValueType *Twalk = ST;
                if (!is_global && idE->LocalSlot >= 0)
                {
                    /* GUARD: dual-read */
                    Twalk = IrCompLocalFromTypeOrFrame(idE->LocalSlot);
                }
                while (Twalk != NULL && Twalk->Base == TypeArray && nd_loc < IR_MAX_ARRAY_DIMS)
                {
                    dims_loc[nd_loc++] = Twalk->ArraySize > 0 ? Twalk->ArraySize : 0;
                    Twalk = Twalk->FromType;
                }
                if (!is_global && nd_loc < 2 && idE->LocalSlot >= 0)
                {
                    /* GUARD: dimensions may live only in the active frame. */
                    int meta_nd = IrCompLocalNDimsOrFrame(idE->LocalSlot);
                    if (meta_nd >= 2)
                    {
                        nd_loc = meta_nd;
                        for (dix = 0; dix < nd_loc && dix < IR_MAX_ARRAY_DIMS; dix++)
                            dims_loc[dix] = IrCompLocalDimOrFrame(idE->LocalSlot, dix);
                    }
                }
                if (Twalk != NULL && (Twalk->Base == TypeStruct || Twalk->Base == TypeUnion))
                    ST = Twalk;
            }
            ST = IrTypePeelArray(ST);
            if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                && ST->Sizeof > 0)
                elem_sz = (int)ST->Sizeof;
            if (elem_sz <= 0)
            {
                /* GUARD: dual-read leaf element size */
                elem_sz = IrCompLocalLeafEsOrFrame(idE->LocalSlot);
            }
            /* Walk Member chain outer→inner: Chain[nm-1] is first after Index
             * (e.g. .in), Chain[0] is the leaf (.a). Accumulate offsets. */
            if (ST != NULL && elem_sz > 0)
            {
                struct ValueType *Cur = ST;
                for (ci = nm - 1; ci >= 0 && Cur != NULL; ci--)
                {
                    int off = 0, mes = 4, meb = TypeInt;
                    struct Value *CM = NULL;
                    if (Cur->Members == NULL
                        || !TableGet(Cur->Members, Chain[ci]->Ident, &CM,
                                     NULL, NULL, NULL)
                        || CM == NULL || CM->Val == NULL)
                    { ok = 0; break; }
                    /* Bitfields: keep Tok=1 + IrBitExtract path. */
                    if (CM->BitWidth > 0)
                    { ok = 0; break; }
                    if (!IrLowerMemberOffEs(Cur, Chain[ci]->Ident, &off, &mes, &meb))
                    { ok = 0; break; }
                    total_off += off;
                    if (ci == 0)
                    { es = mes; eb = meb; }
                    else
                    {
                        if (CM->Typ == NULL)
                        { ok = 0; break; }
                        Cur = CM->Typ;
                        if (Cur->Base == TypeArray && Cur->FromType)
                            Cur = Cur->FromType;
                    }
                }
            }
            else
                ok = 0;
            if (ok && elem_sz > 0)
            {
                /* Load blob base (local slot or global array payload). */
                base_reg = AllocReg(Next);
                if (base_reg < 0) return 0;
                if (is_global)
                {
                    int gi = GlobPush(idE);
                    if (gi < 0) return 0;
                    In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base_reg;
                    In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                }
                else
                {
                    In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = idE->LocalSlot;
                }
                if (!Emit(Code, Cap, N, In)) return 0;
                /* Multidim flatten or 1D index * elem_sz */
                if (ni >= 2)
                {
                    int flat_reg = 0, d2, tmp_reg, dim_reg;
                    int idx_regs[IR_MAX_ARRAY_DIMS];
                    IrLinInst Bin;
                    for (d2 = 0; d2 < ni; d2++)
                    {
                        if (IdxChain[d2] == NULL || IdxChain[d2]->Right == NULL)
                            return 0;
                        if (!LowerRec(IdxChain[d2]->Right, Code, Cap, N, Next, &idx_regs[d2]))
                            return 0;
                    }
                    flat_reg = idx_regs[0];
                    for (d2 = 1; d2 < ni; d2++)
                    {
                        int stride = (d2 < nd_loc && dims_loc[d2] > 0) ? dims_loc[d2] : 1;
                        dim_reg = AllocReg(Next);
                        if (dim_reg < 0) return 0;
                        Bin.Op = IrLinImm; Bin.Dest = (unsigned char)dim_reg;
                        Bin.A = Bin.B = 0; Bin.Tok = 0; Bin.Imm = stride;
                        if (!Emit(Code, Cap, N, Bin)) return 0;
                        tmp_reg = AllocReg(Next);
                        if (tmp_reg < 0) return 0;
                        Bin.Op = IrLinBin; Bin.Dest = (unsigned char)tmp_reg;
                        Bin.A = (unsigned char)flat_reg; Bin.B = (unsigned char)dim_reg;
                        Bin.Tok = TokenAsterisk; Bin.Imm = 0;
                        if (!Emit(Code, Cap, N, Bin)) return 0;
                        flat_reg = AllocReg(Next);
                        if (flat_reg < 0) return 0;
                        Bin.Op = IrLinBin; Bin.Dest = (unsigned char)flat_reg;
                        Bin.A = (unsigned char)tmp_reg; Bin.B = (unsigned char)idx_regs[d2];
                        Bin.Tok = TokenPlus; Bin.Imm = 0;
                        if (!Emit(Code, Cap, N, Bin)) return 0;
                    }
                    sc_reg = AllocReg(Next);
                    if (sc_reg < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)sc_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = elem_sz;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    off_reg = AllocReg(Next);
                    if (off_reg < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
                    In.A = (unsigned char)flat_reg; In.B = (unsigned char)sc_reg;
                    In.Tok = TokenAsterisk; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)base_reg; In.B = (unsigned char)off_reg;
                    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr;
                }
                else if (idxE->Right->Kind == IrExConst)
                {
                    long long ix = idxE->Right->ConstVal;
                    if (ix != 0)
                    {
                        addr = AllocReg(Next);
                        if (addr < 0) return 0;
                        In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                        In.A = (unsigned char)base_reg; In.B = 0;
                        In.Tok = 0; In.Imm = (long long)ix * elem_sz;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        base_reg = addr;
                    }
                }
                else
                {
                    if (!LowerRec(idxE->Right, Code, Cap, N, Next, &idx_reg))
                        return 0;
                    sc_reg = AllocReg(Next);
                    if (sc_reg < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)sc_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = elem_sz;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    off_reg = AllocReg(Next);
                    if (off_reg < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
                    In.A = (unsigned char)idx_reg; In.B = (unsigned char)sc_reg;
                    In.Tok = TokenAsterisk; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)base_reg; In.B = (unsigned char)off_reg;
                    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr;
                }
                if (total_off != 0)
                {
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)base_reg; In.B = 0;
                    In.Tok = 0; In.Imm = total_off;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr;
                }
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                /* Keep the original member AST for the same source caret as
                 * the safe path. Tok=7 is a direct pointer load whose B field
                 * indexes IdxTab; Step still performs the owned-frame map
                 * check before reading. */
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)base_reg;
                In.B = (unsigned char)IdxN;
                IdxMetaPush(E);
                In.Tok = 7; In.Imm = es;
                return Emit(Code, Cap, N, In);
            }
        }
        } /* IdxChain multidim load block */
    }

    if (E->Kind == IrExMember && E->Left != NULL && E->Ident != NULL
        && (E->Left->Kind == IrExIdent
            || (E->Left->Kind == IrExMember && IrVmLinearFeaturesOn())  /* nested o.in.x */
            /* (*pp)->m / (*pp).m: same Tok=1 + IrSafeMemberAddr (already peels *). */
            || (E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
                && E->Left->Left != NULL
                && (E->Left->Left->Kind == IrExIdent
                    || (E->Left->Left->Kind == IrExUnary
                        && E->Left->Left->Op == TokenAsterisk)))
            || (E->Left->Kind == IrExIndex && E->Left->Left != NULL
                && E->Left->Right != NULL
                /* local/global aos OR G.pts[i].x (Index of array Member) */
                && (E->Left->Left->Kind == IrExIdent
                    || E->Left->Left->Kind == IrExIndex
                    || (IrVmLinearFeaturesOn() && E->Left->Left->Kind == IrExMember))
                && (E->Left->Left->Kind != IrExIdent
                    || E->Left->Left->LocalSlot >= 0
                    || (E->Left->Left->LocalSlot < 0
                        && E->Left->Left->ParamSlot < 0
                        && E->Left->Left->Ident != NULL)))))
    {
        /* Member load: Tok=1. Ident / arr[const] via IrSafeMemberAddr;
         * arr[var] carries index in R[A] for Step. Local or global array.
         * Refuse array-typed members (char m[N]): need address decay for
         * strcpy/printf %s — partial load of first bytes would SEGV (691). */
        int idx_reg = 0;
        if (IdxN >= 32) return 0;
        if (LinParser != NULL && E->Ident != NULL)
        {
            /* Resolve member type if base type is known at lower time. */
            struct ValueType *ST = NULL;
            struct Value *MV = NULL;
            IrExpr *Base = E->Left;
            IrExpr *Chain[8];
            int nc = 0, ci;
            int star_peels = 0;
            /* Multidim aos: grid[i][j].m — peel full Index chain (H10). */
            while (Base != NULL && Base->Kind == IrExIndex)
                Base = Base->Left;
            while (Base != NULL && Base->Kind == IrExMember && nc < 8)
            {
                Chain[nc++] = Base;
                Base = Base->Left;
            }
            /* (*pp)->m: peel * to the Ident whose LocalFromType is the var type. */
            while (Base != NULL && Base->Kind == IrExUnary
                   && Base->Op == TokenAsterisk && Base->Left != NULL)
            {
                star_peels++;
                Base = Base->Left;
            }
            if (Base != NULL && Base->Kind == IrExIdent)
            {
                if (Base->LocalSlot >= 0 && Base->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    /* GUARD: dual-read */
                    ST = IrCompLocalFromTypeOrFrame(Base->LocalSlot);
                    /* Type of (*…*p) after star_peels, then arrow pointee. */
                    {
                        int si;
                        for (si = 0; si < star_peels && ST != NULL; si++)
                        {
                            if (ST->Base == TypePointer && ST->FromType)
                                ST = ST->FromType;
                            else
                                break;
                        }
                        if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                            ST = ST->FromType;
                    }
                }
                else if (Base->ParamSlot >= 0 && Base->ParamSlot < IR_CALL_MAX_PARAMS
                         && IrCallDepth > 0)
                {
                    /* Param Node *p: do not IrGlobalVal (would 'p' undefined). */
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Base->ParamSlot < Fr->NumParams)
                    {
                        ST = Fr->ParamTyp[Base->ParamSlot];
                        if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                            ST = ST->FromType;
                    }
                }
                else if (Base->Ident != NULL)
                {
                    struct Value *GV = IrGlobalValSoft(LinParser, Base);
                    if (GV != NULL && GV->Typ != NULL)
                        ST = GV->Typ;
                    /* Global T* gp: peel to pointee for gp->m / gp->arr. */
                    if (ST != NULL && ST->Base == TypePointer && ST->FromType != NULL)
                        ST = ST->FromType;
                }
                ST = IrTypePeelArray(ST);
                /* Walk nested Member types: o.in → type of in */
                for (ci = nc - 1; ci >= 0 && ST != NULL; ci--)
                {
                    struct Value *CM = NULL;
                    if (ST->Members == NULL) { ST = NULL; break; }
                    if (!TableGet(ST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                        || CM == NULL || CM->Typ == NULL)
                    { ST = NULL; break; }
                    ST = CM->Typ;
                    if (ST->Base == TypePointer && ST->FromType)
                        ST = ST->FromType;
                }
                if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                    && ST->Members != NULL
                    && TableGet(ST->Members, E->Ident, &MV, NULL, NULL, NULL)
                    && MV != NULL && MV->Typ != NULL && MV->Typ->Base == TypeArray)
                {
                    /* Array member decay: emit address of s.arr / w.u.c (Tok=5). */
                    if (E->Left->Kind == IrExIndex && E->Left->Right != NULL
                        && E->Left->Right->Kind != IrExConst
                        && !LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_reg))
                        return 0;
                    *Dest = AllocReg(Next);
                    if (*Dest < 0) return 0;
                    In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                    In.A = (unsigned char)idx_reg; In.B = 0; In.Tok = 5; In.Imm = IdxN;
                    IdxMetaPush(E);
                    return Emit(Code, Cap, N, In);
                }
            }
        }
        if (E->Left->Kind == IrExIndex
            && E->Left->Right != NULL
            && E->Left->Right->Kind != IrExConst)
        {
            if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_reg))
                return 0;
        }
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)idx_reg; In.B = 0; In.Tok = 1; In.Imm = IdxN;
        IdxMetaPush(E);
        return Emit(Code, Cap, N, In);
    }
    /* (*gp).vals[i] / gp->vals[i]: global T* array member index — pure linear. */
    if (E->Kind == IrExIndex && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Left->Ident != NULL && E->Right != NULL && LinParser != NULL)
    {
        IrExpr *GPtr = NULL;
        IrExpr *MB = E->Left->Left;
        if (MB != NULL && MB->Kind == IrExIdent
            && MB->LocalSlot < 0 && MB->ParamSlot < 0 && MB->Ident != NULL)
            GPtr = MB;
        else if (MB != NULL && MB->Kind == IrExUnary && MB->Op == TokenAsterisk
                 && MB->Left != NULL && MB->Left->Kind == IrExIdent
                 && MB->Left->LocalSlot < 0 && MB->Left->ParamSlot < 0
                 && MB->Left->Ident != NULL)
            GPtr = MB->Left;
        if (GPtr != NULL)
        {
            struct Value *GV = IrGlobalValSoft(LinParser, GPtr);
            struct ValueType *ST = NULL;
            struct Value *MV = NULL;
            int pr, base_reg, idx_reg, sc, off, addr, es = 4, gi, moff = 0;
            if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypePointer
                && GV->Typ->FromType != NULL)
                ST = GV->Typ->FromType;
            if (ST != NULL && ST->Members != NULL
                && TableGet(ST->Members, E->Left->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->Typ != NULL && MV->Typ->Base == TypeArray
                && MV->Val != NULL)
            {
                if (!IrResolveMemberOffsetValue(MV, &moff))
                    return 0;
                if (E->ElemSize > 0 && E->ElemSize <= 8) es = E->ElemSize;
                else if (MV->Typ->FromType != NULL && MV->Typ->FromType->Sizeof > 0
                         && MV->Typ->FromType->Sizeof <= 8)
                    es = (int)MV->Typ->FromType->Sizeof;
                else es = 4;
                if (!LowerRec(E->Right, Code, Cap, N, Next, &idx_reg)) return 0;
                gi = GlobPush(GPtr);
                if (gi < 0) return 0;
                pr = AllocReg(Next);
                if (pr < 0) return 0;
                In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)pr;
                In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                if (!Emit(Code, Cap, N, In)) return 0;
                if (moff != 0)
                {
                    base_reg = AllocReg(Next);
                    if (base_reg < 0) return 0;
                    In.Op = IrLinAddI; In.Dest = (unsigned char)base_reg;
                    In.A = (unsigned char)pr; In.B = 0;
                    In.Tok = 0; In.Imm = moff;
                    if (!Emit(Code, Cap, N, In)) return 0;
                }
                else
                    base_reg = pr;
                sc = AllocReg(Next);
                if (sc < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                In.A = In.B = 0; In.Tok = 0; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                off = AllocReg(Next);
                if (off < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)off;
                In.A = (unsigned char)idx_reg; In.B = (unsigned char)sc;
                In.Tok = TokenAsterisk; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)base_reg; In.B = (unsigned char)off;
                In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)addr; In.B = 0;
                In.Tok = 2; In.Imm = es;
                return Emit(Code, Cap, N, In);
            }
        }
    }
    /* s.arr[i] where arr is array member: base = &s.arr, load base[i].
     * Member alone of array type is refused (address decay); Index uses it. */
    if (E->Kind == IrExIndex && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Left->Ident != NULL && E->Right != NULL)
    {
        int idx_reg, es = 0, base_reg, sc, off, addr;
        /* es from annotate or default char */
        if (E->ElemSize > 0 && E->ElemSize <= 8)
            es = E->ElemSize;
        else if (E->Left->ElemSize > 0 && E->Left->ElemSize <= 8)
            es = E->Left->ElemSize;
        if (es != 1 && es != 2 && es != 4 && es != 8)
            es = 1;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &idx_reg)) return 0;
        if (IdxN >= 31) return 0;
        {
            int outer_reg = 0;
            IrExpr *MB = E->Left->Left;
            if (MB != NULL && MB->Kind == IrExIndex && MB->Right != NULL
                && MB->Right->Kind != IrExConst)
            {
                if (!LowerRec(MB->Right, Code, Cap, N, Next, &outer_reg))
                    return 0;
            }
            base_reg = AllocReg(Next);
            if (base_reg < 0) return 0;
            /* Tok=5: &s.arr / &aos[var].arr (outer idx in A when non-const). */
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)base_reg;
            In.A = (unsigned char)outer_reg; In.B = 0;
            In.Tok = 5; In.Imm = IdxN;
            IdxMetaPush(E->Left);
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        sc = AllocReg(Next);
        if (sc < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
        In.A = In.B = 0; In.Tok = 0; In.Imm = es;
        if (!Emit(Code, Cap, N, In)) return 0;
        off = AllocReg(Next);
        if (off < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)off;
        In.A = (unsigned char)idx_reg; In.B = (unsigned char)sc;
        In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)addr;
        In.A = (unsigned char)base_reg; In.B = (unsigned char)off;
        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        /* Tok=6: load *addr with es=B, meta Imm=IdxN for uninit (Index of Member). */
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)addr; In.B = (unsigned char)es; In.Tok = 6;
        In.Imm = IdxN;
        IdxMetaPush(E); /* Index expr, not just Member */
        return Emit(Code, Cap, N, In);
    }
    /* Nested multi-dim a[i][j] / a[i][j][k]: flatten to 1D index at lower time.
     * flat = ((i0*d1 + i1)*d2 + …) + i_{n-1}; LoadIndex(root, flat, leaf_es).
     * Dims from LocalDims (local) or TypeArray chain (global). Enables 061 on VM.
     * Skip when Left is array-of-ptr element (ElemSize==sizeof(void*)): that is
     * ptrs[i][j] → dedicated aop path below, not 2D flatten (nd=1). */
    if (E->Kind == IrExIndex && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Right != NULL
        && !IrLowerIndexRowIsPointer(E->Left))
    {
        IrExpr *Root = E;
        IrExpr *Chain[IR_MAX_ARRAY_DIMS];
        int nc = 0, d, dims[IR_MAX_ARRAY_DIMS];
        int nd = 0, leaf_es = 4;
        int idx_regs[IR_MAX_ARRAY_DIMS];
        int flat_reg, tmp_reg;
        IrLinInst Bin;
        while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
        {
            Chain[nc++] = Root;
            Root = Root->Left;
        }
        if (Root == NULL || Root->Kind != IrExIdent)
            return 0;
        if (nc < 2 || nc > IR_MAX_ARRAY_DIMS)
            return 0;
        for (d = 0; d < nc / 2; d++)
        {
            IrExpr *tmp = Chain[d];
            Chain[d] = Chain[nc - 1 - d];
            Chain[nc - 1 - d] = tmp;
        }
        for (d = 0; d < IR_MAX_ARRAY_DIMS; d++) dims[d] = 0;
        if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
            && IrCallDepth > 0)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            nd = (int)Fr->LocalNDims[Root->LocalSlot];
            leaf_es = Fr->LocalElemSize[Root->LocalSlot] > 0
                ? (int)Fr->LocalElemSize[Root->LocalSlot] : 4;
            for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                dims[d] = Fr->LocalDims[Root->LocalSlot][d];
        }
        if (nd < 2 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
            && IrCompLocalNDimsOrFrame(Root->LocalSlot) >= 2)
        {
            int slot = Root->LocalSlot;
            nd = (int)IrCompLocalNDimsOrFrame(slot);
            leaf_es = IrCompLocalLeafEsOrFrame(slot) > 0 ? IrCompLocalLeafEsOrFrame(slot) : leaf_es;
            for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                dims[d] = IrCompLocalDimOrFrame(slot, d);
        }
        else if (Root->Ident != NULL && LinParser != NULL)
        {
            struct Value *GV = IrGlobalValSoft(LinParser, Root);
            if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray)
            {
                struct ValueType *T = GV->Typ;
                nd = 0;
                while (T != NULL && T->Base == TypeArray && nd < IR_MAX_ARRAY_DIMS)
                {
                    dims[nd++] = T->ArraySize > 0 ? T->ArraySize : 0;
                    if (T->FromType != NULL && T->FromType->Base != TypeArray)
                        leaf_es = T->FromType->Sizeof > 0
                            ? (int)T->FromType->Sizeof : 4;
                    T = T->FromType;
                }
            }
        }
        /* Array parameters decay to pointer-to-row. Recover the fixed row
         * extent from int (*)[N] when local declaration dimensions are absent. */
        if (nd < 2 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
            && IrCallDepth > 0)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            struct ValueType *PT = Fr->LocalFromType[Root->LocalSlot]
                ? Fr->LocalFromType[Root->LocalSlot] : Fr->LocalTyp[Root->LocalSlot];
            if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL
                && PT->FromType->Base == TypeArray && PT->FromType->ArraySize > 0)
            {
                struct ValueType *Leaf = PT->FromType;
                nd = 2; dims[0] = 0; dims[1] = PT->FromType->ArraySize;
                while (Leaf != NULL && Leaf->Base == TypeArray && Leaf->FromType != NULL)
                    Leaf = Leaf->FromType;
                if (Leaf != NULL && Leaf->Sizeof > 0) leaf_es = (int)Leaf->Sizeof;
            }
        }
        if (nd != nc || nd < 2)
            return 0;
        for (d = 1; d < nd; d++)
            if (dims[d] <= 0) return 0;
        for (d = 0; d < nc; d++)
        {
            if (!LowerRec(Chain[d]->Right, Code, Cap, N, Next, &idx_regs[d]))
                return 0;
        }
        flat_reg = idx_regs[0];
        for (d = 1; d < nd; d++)
        {
            int dim_reg = AllocReg(Next);
            if (dim_reg < 0) return 0;
            Bin.Op = IrLinImm; Bin.Dest = (unsigned char)dim_reg;
            Bin.A = Bin.B = 0; Bin.Tok = 0; Bin.Imm = dims[d];
            if (!Emit(Code, Cap, N, Bin)) return 0;
            tmp_reg = AllocReg(Next);
            if (tmp_reg < 0) return 0;
            Bin.Op = IrLinBin; Bin.Dest = (unsigned char)tmp_reg;
            Bin.A = (unsigned char)flat_reg; Bin.B = (unsigned char)dim_reg;
            Bin.Tok = TokenAsterisk; Bin.Imm = 0;
            if (!Emit(Code, Cap, N, Bin)) return 0;
            flat_reg = AllocReg(Next);
            if (flat_reg < 0) return 0;
            Bin.Op = IrLinBin; Bin.Dest = (unsigned char)flat_reg;
            Bin.A = (unsigned char)tmp_reg; Bin.B = (unsigned char)idx_regs[d];
            Bin.Tok = TokenPlus; Bin.Imm = 0;
            if (!Emit(Code, Cap, N, Bin)) return 0;
        }
        if (IdxN >= 32) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        {
            IrLinInst Ld;
            Ld.Op = IrLinLoadIndex; Ld.Dest = (unsigned char)*Dest;
            Ld.A = (unsigned char)flat_reg; Ld.B = 0; Ld.Tok = 0; Ld.Imm = IdxN;
            IdxMetaPush(E);
            return Emit(Code, Cap, N, Ld);
        }
    }
    /* f()[i]: Call returns T*, base pointer in reg, scale index, Tok=2 load.
     * Same model as p[i] when p is already a pointer value. */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExIndex && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExCall)
    {
        int base, idx, sc, off, addr, es = 4;
        struct ValueType *RT = NULL;
        if (E->Left->CachedFunc != NULL && E->Left->CachedFunc->Val != NULL)
            RT = E->Left->CachedFunc->Val->FuncDef.ReturnType;
        if (RT == NULL && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent)
            RT = NULL; /* IrLookup not in scope — use ElemSize */
        if (E->ElemSize > 0 && E->ElemSize <= 8)
            es = E->ElemSize;
        else if (E->Left->ElemSize > 0 && E->Left->ElemSize <= 8)
            es = E->Left->ElemSize;
        else if (RT != NULL && RT->Base == TypePointer && RT->FromType != NULL
                 && RT->FromType->Sizeof > 0 && RT->FromType->Sizeof <= 8)
            es = (int)RT->FromType->Sizeof;
        else
            es = 4;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &base)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &idx)) return 0;
        sc = AllocReg(Next);
        if (sc < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
        In.A = In.B = 0; In.Tok = 0; In.Imm = es;
        if (!Emit(Code, Cap, N, In)) return 0;
        off = AllocReg(Next);
        if (off < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)off;
        In.A = (unsigned char)idx; In.B = (unsigned char)sc;
        In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)addr;
        In.A = (unsigned char)base; In.B = (unsigned char)off;
        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)addr; In.B = 0;
        In.Tok = 2; In.Imm = es;
        return Emit(Code, Cap, N, In);
    }
    /* Param p[i] pure Tok=2: base = ParamVal bits, addr = base + i*es.
     * Avoids IdxMetaKind=2 dual-slot collision when a body has both dst[i]
     * and src[i] (formerly a strict-path copy-loop failure). Same model as f()[i]. */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExIndex && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot >= 0 && E->Left->ParamSlot < IR_CALL_MAX_PARAMS
        && E->Left->LocalSlot < 0
        && E->BitWidth <= 0)
    {
        int base, idx, sc, off, addr, es = 0;
        int ps = E->Left->ParamSlot;
        int is_ptr_ptr = 0;
        /* Element size: prefer annotate ElemSize; else ParamTyp pointee. */
        if (IrCallDepth > 0 && ps < IrCallStack[IrCallDepth - 1]->NumParams)
        {
            struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[ps];
            if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL)
            {
                if (PT->FromType->Base == TypePointer
                    || PT->FromType->Base == TypeFunction)
                    is_ptr_ptr = 1;
                else if (PT->FromType->Sizeof > 0 && PT->FromType->Sizeof <= 8)
                    es = (int)PT->FromType->Sizeof;
            }
        }
        if (E->ElemSize > 0 && E->ElemSize <= 8 && !is_ptr_ptr)
            es = E->ElemSize;
        if (es <= 0 && !is_ptr_ptr)
            es = 1; /* char* default; never 4 (int-width) for unknown */
        /* Refuse pointer-to-pointer here — kind-4 path handles argv[i]. */
        if ((E->TypeFlags & IR_TF_PTR) || is_ptr_ptr
            || E->ElemSize == (int)sizeof(void *))
        {
            /* Leave argv[i] / T** to existing kind-4 IdxMeta path. */
        }
        else
        {
            /* Load param pointer bits: Imm negative encodes ParamSlot. */
            base = AllocReg(Next);
            if (base < 0) return 0;
            In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base;
            In.A = In.B = 0; In.Tok = 0;
            In.Imm = -(ps + 1);
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &idx)) return 0;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            off = AllocReg(Next);
            if (off < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off;
            In.A = (unsigned char)idx; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            addr = AllocReg(Next);
            if (addr < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)base; In.B = (unsigned char)off;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr;
            /* Preserve source coordinates on this strict p[i] load. Tok=2 keeps
             * B for parameter signedness; encode line/column above Imm's low
             * width nibble. Ordinary Tok=2 loads still use Imm=1/2/4/8. */
            In.B = (unsigned char)(2 + ps);
            In.Tok = 2; In.Imm = es;
            if (E->SourceLine > 0 && E->SourceLine <= 65535
                && E->CharacterPos > 0 && E->CharacterPos <= 65535)
                In.Imm = ((long long)E->SourceLine << 32)
                       | ((long long)E->CharacterPos << 16) | es;
            return Emit(Code, Cap, N, In);
        }
    }
    /* Local array a[i] / a[expr] */
    /* a[i][j] on local array-of-pointers: load a[i] (void*), then [j] at pointee es.
     * TF_PTR may be absent (Body-IR 675 keeps INT on Index); on the Linear
     * path use ElemSize==sizeof(void*) as aop signal. */
    if (E->Kind == IrExIndex && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && ((E->Left->Left->LocalSlot >= 0
             && E->Left->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            || IrLowerPtrRowPointee(E->Left) != NULL) /* global T *g[N] */
        && IrLowerIndexRowIsPointer(E->Left))
    {
        int pr, rd, es;
        struct ValueType *PTy;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &pr)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        /* Pointee size for ptrs[i][j] — not sizeof(void*). The declared
         * pointee (IrLowerPtrRowPointee) is authoritative when known. */
        PTy = IrLowerPtrRowPointee(E->Left);
        if (PTy != NULL && PTy->Base == TypeFP && PTy->Sizeof != 8)
            return 0; /* float element: not a register-bits load */
        es = (PTy != NULL && PTy->Sizeof > 0 && PTy->Sizeof <= 8) ? (int)PTy->Sizeof
             : (E->ElemSize > 0 && E->ElemSize != (int)sizeof(void *)
                ? E->ElemSize : 0);
        if (es <= 0 && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
            && IrCallDepth > 0 && E->Left->Left->LocalSlot >= 0
            && E->Left->Left->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            int slot = E->Left->Left->LocalSlot;
            {
                int ps = IrCompLocalPointeeSizeOrFrame(slot);
                if (ps > 0) es = ps;
            }
        }
        if (es <= 0) es = 4;
        if (es != 1 && es != 2 && es != 4 && es != 8) es = 4;
        /* scale: addr = pr + rd*es, load */
        {
            int sc, off, addr;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            off = AllocReg(Next);
            if (off < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off;
            In.A = (unsigned char)rd; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            addr = AllocReg(Next);
            if (addr < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)pr; In.B = (unsigned char)off;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr; In.B = 0;
            if (IrLinLocalPointeeUnsigned(E->Left)
                || (PTy != NULL && IrTypeIsUnsignedBase(PTy->Base)))
                In.B = 1; /* unsigned char *q; q[i] */
            In.Tok = 2; /* pointer in R[A], es in Imm */
            In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
    }
    if (E->Kind == IrExIndex && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        int rd, slot = E->Left->LocalSlot;
        int nd = 0, leaf_es = 4, di, stride;
        /* Multidim row decay: m[i] on int m[A][B] → &m[i][0], not load element.
         * Nested m[i][j] is handled by the flatten path above. */
        /* GUARD: dual-read dimensions */
        nd = IrCompLocalNDimsOrFrame(slot);
        if (nd >= 2)
        {
            if (IrCompLocalLeafEsOrFrame(slot) > 0)
                leaf_es = IrCompLocalLeafEsOrFrame(slot);
            else if (IrCallDepth > 0
                     && IrCallStack[IrCallDepth - 1]->LocalElemSize[slot] > 0)
                leaf_es = IrCallStack[IrCallDepth - 1]->LocalElemSize[slot];
            else if (E->ElemSize > 0 && E->ElemSize <= 8)
                leaf_es = E->ElemSize;
            else
                leaf_es = 4;
            stride = leaf_es;
            for (di = 1; di < nd; di++)
            {
                /* GUARD: dual-read dimension value */
                int d = IrCompLocalDimOrFrame(slot, di);
                if (d <= 0) d = 1;
                stride *= d;
            }
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
            {
                int base, sc, off, addr;
                base = AllocReg(Next);
                if (base < 0) return 0;
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base;
                In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
                if (!Emit(Code, Cap, N, In)) return 0;
                sc = AllocReg(Next);
                if (sc < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                In.A = In.B = 0; In.Tok = 0; In.Imm = stride;
                if (!Emit(Code, Cap, N, In)) return 0;
                off = AllocReg(Next);
                if (off < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)off;
                In.A = (unsigned char)rd; In.B = (unsigned char)sc;
                In.Tok = TokenAsterisk; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)base; In.B = (unsigned char)off;
                In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = addr; /* pointer bits, not load */
                return 1;
            }
        }
        /* Global array-of-fptr load: ops[i] → Value* word. */
        if (E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0
            && E->Left->Ident != NULL && LinParser != NULL)
        {
            struct Value *GV = IrGlobalIdentSoft(LinParser, E->Left->Ident);
            if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray
                && GV->Typ->FromType != NULL
                && ((GV->Typ->FromType->Base == TypePointer
                     && GV->Typ->FromType->FromType
                     && GV->Typ->FromType->FromType->Base == TypeFunction)
                    || GV->Typ->FromType->Base == TypeFunction))
            {
                int base_r, sc, off, addr, gi, es = (int)sizeof(void *);
                if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
                gi = GlobPush(E->Left);
                if (gi < 0) return 0;
                base_r = AllocReg(Next);
                if (base_r < 0) return 0;
                In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base_r;
                In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                if (!Emit(Code, Cap, N, In)) return 0;
                sc = AllocReg(Next);
                if (sc < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                In.A = In.B = 0; In.Tok = 0; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                off = AllocReg(Next);
                if (off < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)off;
                In.A = (unsigned char)rd; In.B = (unsigned char)sc;
                In.Tok = TokenAsterisk; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)base_r; In.B = (unsigned char)off;
                In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)addr; In.B = 0;
                In.Tok = 2; In.Imm = es;
                return Emit(Code, Cap, N, In);
            }
        }
        /* Array-of-fptr / array-of-ptr: word-sized load (not IdxMeta default 4). */
        if (IrCompLocalLeafEsOrFrame(E->Left->LocalSlot) == (int)sizeof(void *)
            || (CompileSlot && CompileSlot->SlotIsFptr[E->Left->LocalSlot])
            || (IrCallDepth > 0
                && IrCallStack[IrCallDepth - 1]->LocalIsPtr[E->Left->LocalSlot]
                && IrCompLocalPointeeSizeOrFrame(E->Left->LocalSlot) > 0)
            || IrLinEmitPointerHasSizedPointee(E->Left->LocalSlot))
        {
            int base_r, sc, off, addr, es = (int)sizeof(void *);
            int slot = E->Left->LocalSlot;
            int aop = (IrCompLocalLeafEsOrFrame(slot) == (int)sizeof(void *)
                || IrCompLocalElemIsPtrOrFrame(slot)
                || (IrCompLocalNDimsOrFrame(slot) > 0
                    && IrLinEmitTypeIsPointerOrArray(slot)
                    && IrLinEmitLocalType(slot)->Base == TypePointer));
            /* Array-of-pointer (aop): element is void*-sized. Do NOT use
             * LocalPointeeSize (that is for scalar T* p; p[i] stride).
             * 269: FromType of T* is T (char) Sizeof=1 — must not become es. */
            if (!aop)
            {
                if (CompileSlot != NULL && CompileSlot->SlotFromType[slot] != NULL
                    && CompileSlot->SlotFromType[slot]->Base == TypePointer
                    && CompileSlot->SlotFromType[slot]->FromType != NULL
                    && CompileSlot->SlotFromType[slot]->FromType->Sizeof > 0)
                    es = (int)CompileSlot->SlotFromType[slot]->FromType->Sizeof;
                else if (CompLocalFromType[slot] != NULL
                    && CompLocalFromType[slot]->Base == TypePointer
                    && CompLocalFromType[slot]->FromType != NULL
                    && CompileSlot == NULL
                    && CompLocalFromType[slot]->FromType->Sizeof > 0
                    && IrCompLocalNDimsOrFrame(slot) <= 0)
                    es = (int)CompLocalFromType[slot]->FromType->Sizeof;
                if (IrCallDepth > 0
                    && IrCallStack[IrCallDepth - 1]->LocalIsPtr[slot]
                    && IrCallStack[IrCallDepth - 1]->LocalArraySize[slot] <= 0
                    && IrCompLocalPointeeSizeOrFrame(slot) > 0)
                    es = IrCompLocalPointeeSizeOrFrame(slot);
            }
            else
                es = (int)sizeof(void *);
            if (getenv("BABYC_FP_TRACE"))
                fprintf(stderr, "[fp] Index word-load slot=%d leafes=%d\n",
                    slot, IrCompLocalLeafEsOrFrame(slot));
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
            base_r = AllocReg(Next);
            if (base_r < 0) return 0;
            In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_r;
            In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
            if (!Emit(Code, Cap, N, In)) return 0;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            off = AllocReg(Next);
            if (off < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off;
            In.A = (unsigned char)rd; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            addr = AllocReg(Next);
            if (addr < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)base_r; In.B = (unsigned char)off;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr; In.B = 0;
            if (IrLinLocalPointeeUnsigned(E->Left)) In.B = 1;
            In.Tok = 2; In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
        if (IdxN >= 32) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)rd; In.B = 0; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(E);
        return Emit(Code, Cap, N, In);
    }
    /* Param pointer p[i] */
    if (E->Kind == IrExIndex && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot >= 0 && E->Left->ParamSlot < IR_CALL_MAX_PARAMS)
    {
        int rd;
        if (IdxN >= 32) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)rd; In.B = 0; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(E);
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExIndex && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0
        && E->Left->Ident != NULL && LinParser != NULL)
    {
        struct Value *GV = IrGlobalIdentSoft(LinParser, E->Left->Ident);
        /* m[i] for a global multidimensional array is an array-to-pointer
         * decay: produce the address of the selected row, not the first
         * scalar in that row.  Flattened m[i][j] continues through the
         * existing multidimensional-index path above. */
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray
            && GV->Typ->FromType != NULL && GV->Typ->FromType->Base == TypeArray)
        {
            int base_r, idx_r, dest_r, mi;
            if (IdxN >= 32) return 0;
            if (!LowerRec(E->Left, Code, Cap, N, Next, &base_r)
                || !LowerRec(E->Right, Code, Cap, N, Next, &idx_r)) return 0;
            dest_r = AllocReg(Next);
            if (dest_r < 0) return 0;
            mi = IdxN;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)dest_r;
            In.A = (unsigned char)base_r; In.B = (unsigned char)idx_r;
            In.Tok = IR_LIN_IDX_ROW_DECAY; In.Imm = mi;
            IdxMetaPush(E);
            *Dest = dest_r;
            return Emit(Code, Cap, N, In);
        }
        int rd;
        if (IdxN >= 32) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)rd; In.B = 0; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(E);
        return Emit(Code, Cap, N, In);
    }
    /* Parameter: live in frame ParamVal[ParamSlot]. Encode as LoadLocal
     * with Imm = -(ParamSlot+1) so Step can dispatch without a new opcode. */
    if (E->Kind == IrExIdent && E->ParamSlot >= 0 && E->ParamSlot < IR_CALL_MAX_PARAMS)
    {
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadLocal; In.Dest = (unsigned char)*Dest;
        In.A = In.B = 0; In.Tok = 0;
        In.Imm = -(E->ParamSlot + 1);
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExIdent && E->LocalSlot < 0 && E->Ident != NULL)
    {
        int gi;
        if (LinParser != NULL)
        {
            struct Value *TV = IrGlobalIdentSoft(LinParser, E->Ident);
            if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
            {
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
                In.A = In.B = 0; In.Tok = 5;
                In.Imm = (long long)(intptr_t)TV;
                return Emit(Code, Cap, N, In);
            }
        }
        if (GlobN >= 32) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        gi = GlobPush(E);
        if (gi < 0) return 0;
        In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)*Dest;
        In.A = In.B = 0;
        /* Tok=1: address of global array/object (decay / &g); Tok=0: scalar */
        In.Tok = (E->TypeFlags & IR_TF_PTR) ? 1 : 0;
        In.Imm = gi;
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExIdent && E->LocalSlot >= 0 && E->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        /* Conservative CSE: reuse register if this local was loaded
         * earlier in the current basic block and not invalidated.
         * Invalidation: branch target, pointer store, call, store to
         * local whose address was taken. */
        extern int LinCseN;
        extern int LinCseSlot[];
        extern unsigned char LinCseSlotPinned[];
        extern int LinCseReg[];
        extern int LinAddrTaken[];
        int si;
                /* Dont CSE locals whose address was taken. */
        if (LinAddrTaken[E->LocalSlot])
            goto cse_skip;
        for (si = 0; si < LinCseN; si++)
            if (LinCseSlot[si] == E->LocalSlot && LinCseReg[si] >= 0)
            { *Dest = LinCseReg[si]; return 1; }
        cse_skip:
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinLoadLocal; In.Dest = (unsigned char)*Dest;
        In.A = In.B = 0; In.Tok = 0; In.Imm = E->LocalSlot;
        if (!Emit(Code, Cap, N, In)) return 0;
        if (LinCseN < IR_CALL_MAX_LOCALS && !LinAddrTaken[E->LocalSlot])
        { LinCseSlot[LinCseN] = E->LocalSlot; LinCseReg[LinCseN] = *Dest; LinCsePinReg(*Dest); LinCseSlotPinned[LinCseN] = 1; LinCseN++; }
        return 1;
    }
    /* Cast: FP→int truncates IEEE bits to (long long)(double).
     * Pointer/other casts peel to operand (malloc, ident, ...). */
    if (E->Kind == IrExUnary && E->Op == TokenCast && E->Left != NULL)
    {
        int src;
        /* FP source → integer: truncate. Pointer cast peels. FP→FP peels.
         * Detect FP via Left flags OR Ident of FP local (LocalElemSize 8 + FP). */
        {
            int left_fp = (E->Left->TypeFlags & IR_TF_FP) != 0;
            int to_ptr = (E->TypeFlags & IR_TF_PTR) != 0;
            int to_fp = (E->TypeFlags & IR_TF_FP) != 0;
            if (!left_fp && E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0
                && IrCallDepth > 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (Fr->LocalFromType[E->Left->LocalSlot] != NULL
                    && Fr->LocalFromType[E->Left->LocalSlot]->Base == TypeFP)
                    left_fp = 1;
            }
            if (!left_fp && E->Left->Kind == IrExBinOp
                && (E->Left->TypeFlags & IR_TF_FP))
                left_fp = 1;
            /* BinOp result of FP mul may only flag on node */
            if (!left_fp && E->Left->Kind == IrExBinOp
                && ((E->Left->Left && (E->Left->Left->TypeFlags & IR_TF_FP))
                    || (E->Left->Right && (E->Left->Right->TypeFlags & IR_TF_FP))))
                left_fp = 1;
            if (left_fp && !to_ptr && !to_fp && IrVmLinearFeaturesOn())
            {
                if (!LowerRec(E->Left, Code, Cap, N, Next, &src)) return 0;
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)src; In.B = 0;
                In.Tok = TokenCast; In.Imm = 1;
                return Emit(Code, Cap, N, In);
            }
            /* int/long → FP (return n in double f(int), assign, …).
             * Soft often skips to tree (VmTreeSkip); STRICT must convert.
             * Detect via IR_TF_FP or I32==TypeFP (return wrapper). */
            if (!left_fp && !to_ptr
                && (to_fp || E->I32 == TypeFP))
            {
                if (!LowerRec(E->Left, Code, Cap, N, Next, &src)) return 0;
                *Dest = AllocReg(Next);
                if (*Dest < 0) return 0;
                In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
                In.A = (unsigned char)src; In.B = 0;
                In.Tok = TokenCast; In.Imm = 2; /* (double)int bits */
                return Emit(Code, Cap, N, In);
            }
        }
        if (E->ConstVal == 0 && !(E->TypeFlags & IR_TF_FP) && E->I32 != TypeVoid
            && E->I32 != TypeFP
            && (E->I32 == TypeChar || E->I32 == TypeUnsignedChar
                || E->I32 == TypeShort || E->I32 == TypeUnsignedShort
                || E->I32 == TypeInt || E->I32 == TypeUnsignedInt
                || E->I32 == TypeLong || E->I32 == TypeUnsignedLong
                || E->I32 == TypeLongLong || E->I32 == TypeUnsignedLongLong))
        {
            if (!LowerRec(E->Left, Code, Cap, N, Next, &src)) return 0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)src; In.B = 0; In.Tok = TokenCast;
            In.Imm = 0x100 + E->I32;
            return Emit(Code, Cap, N, In);
        }
        return LowerRec(E->Left, Code, Cap, N, Next, Dest);
    }
    /* sizeof(T) folded to ConstVal at compile, or TokenSizeof with ConstVal.
     * Refuse Member sizeof (bitfield diagnostic is runtime); ConstVal==0 with
     * Member left is almost always the bitfield constraint case. */
    if (E->Kind == IrExUnary && E->Op == TokenSizeof)
    {
        /* Bitfield sizeof: constraint diagnostic (508). Prefer stamped
         * BitWidth; resolve from type if needed (compile-time, no blob). */
        if (E->Left != NULL && E->Left->Kind == IrExMember)
        {
            IrExpr *M = E->Left;
            if (M->BitWidth <= 0 && M->Ident != NULL)
            {
                struct ValueType *ST = NULL;
                struct Value *MV = NULL;
                IrExpr *R = M->Left;
                if (R != NULL && R->Kind == IrExIndex && R->Left != NULL)
                    R = R->Left;
                if (R != NULL && R->Kind == IrExIdent)
                {
                    if (CompileSlot != NULL && R->LocalSlot >= 0
                        && R->LocalSlot < IR_CALL_MAX_LOCALS)
                        ST = CompileSlot->SlotFromType[R->LocalSlot];
                    if (ST == NULL && R->LocalSlot >= 0
                        && R->LocalSlot < IR_CALL_MAX_LOCALS
                        && CompLocalFromType[R->LocalSlot] != NULL)
                        ST = CompLocalFromType[R->LocalSlot];
                    if (ST == NULL && IrCallDepth > 0 && R->LocalSlot >= 0
                        && R->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                        ST = Fr->LocalFromType[R->LocalSlot];
                        if (ST == NULL) ST = Fr->LocalTyp[R->LocalSlot];
                    }
                }
                if (ST != NULL && ST->Base == TypeArray && ST->FromType)
                    ST = ST->FromType;
                if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
                if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                    && ST->Members != NULL
                    && TableGet(ST->Members, M->Ident, &MV, NULL, NULL, NULL)
                    && MV != NULL && MV->Val != NULL && MV->BitWidth > 0)
                {
                    M->BitWidth = MV->BitWidth;
                    M->BitOffset = MV->BitOffset;
                }
            }
            if (M->BitWidth > 0 || E->ConstVal <= 0)
            {
                if (LinParser != NULL)
                {
                    if (M->SourceLine > 0)
                        LinParser->Line = M->SourceLine;
                    if (M->Ident != NULL)
                        ParserSetCaretOnIdent(LinParser, M->Ident);
                    ProgramFail(LinParser, "sizeof applied to a bitfield");
                }
                return 0;
            }
        }
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
        In.A = In.B = 0; In.Tok = 0; In.Imm = E->ConstVal;
        return Emit(Code, Cap, N, In);
    }
    /* Prefix -FP: ir_compile emits BinOp(0 - FPConst), not UnaryMinus. */
    if (E->Kind == IrExBinOp && E->Op == TokenMinus
        && E->Left != NULL && E->Left->Kind == IrExConst
        && E->Left->ConstVal == 0
        && E->Right != NULL && E->Right->Kind == IrExConst
        && E->Right->Op == TokenFPConstant
        && *N >= 0 && *N < IR_LIN_STMT_MAX)
    {
        union { long long l; double d; } u;
        u.l = E->Right->ConstVal;
        u.d = -u.d;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        LinWideImm[*N] = u.l;
        LinWideOn[*N] = 1;
        if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
        In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
        In.A = In.B = 0; In.Tok = 2; In.Imm = *N;
        return Emit(Code, Cap, N, In);
    }
    /* FP + - * / : fold const/const; else runtime IR_LIN_F_FP.
     * Detect FP via node flags OR operand FP const/ident TF_FP. */
    if (E->Kind == IrExBinOp
        && (E->Op == TokenPlus || E->Op == TokenMinus
            || E->Op == TokenAsterisk || E->Op == TokenSlash)
        && E->Left != NULL && E->Right != NULL
        && IrVmLinearFeaturesOn()
        && ((E->TypeFlags & IR_TF_FP)
            || (E->Left->TypeFlags & IR_TF_FP)
            || (E->Right->TypeFlags & IR_TF_FP)
            || (E->Left->Kind == IrExConst && E->Left->Op == TokenFPConstant)
            || (E->Right->Kind == IrExConst && E->Right->Op == TokenFPConstant)))
    {
        int la, lb;
        /* Const/const fold (4.0/3.0, 1e-13*1e15, 3.9*2). */
        if (E->Left->Kind == IrExConst && E->Right->Kind == IrExConst
            && *N >= 0 && *N < IR_LIN_STMT_MAX
            && (E->Left->Op == TokenFPConstant || E->Right->Op == TokenFPConstant
                || (E->TypeFlags & IR_TF_FP)))
        {
            union { long long l; double d; } a, b, r;
            if (E->Left->Op == TokenFPConstant) a.l = E->Left->ConstVal;
            else a.d = (double)E->Left->ConstVal;
            if (E->Right->Op == TokenFPConstant) b.l = E->Right->ConstVal;
            else b.d = (double)E->Right->ConstVal;
            if (E->Op == TokenPlus) r.d = a.d + b.d;
            else if (E->Op == TokenMinus) r.d = a.d - b.d;
            else if (E->Op == TokenAsterisk) r.d = a.d * b.d;
            else r.d = (b.d != 0.0) ? (a.d / b.d) : 0.0;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            LinWideImm[*N] = r.l;
            LinWideOn[*N] = 1;
            if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
            In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
            In.A = In.B = 0; In.Tok = 2; In.Imm = *N;
            return Emit(Code, Cap, N, In);
        }
        /* Integer literal operand in FP expression: promote to IEEE 2.0 etc.
         * Otherwise xb.l=2 is bit-pattern of denormal, product ~0 (065). */
        if (E->Left->Kind == IrExConst && E->Left->Op != TokenFPConstant
            && *N >= 0 && *N < IR_LIN_STMT_MAX)
        {
            union { long long l; double d; } u;
            u.d = (double)E->Left->ConstVal;
            la = AllocReg(Next);
            if (la < 0) return 0;
            LinWideImm[*N] = u.l;
            LinWideOn[*N] = 1;
            if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
            In.Op = IrLinImm; In.Dest = (unsigned char)la;
            In.A = In.B = 0; In.Tok = 2; In.Imm = *N;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        else if (!LowerRec(E->Left, Code, Cap, N, Next, &la)) return 0;
        /* Variable int Left in FP expr (i*0.5, i+1.0) — promote to IEEE. */
        if (!(E->Left->TypeFlags & IR_TF_FP) && E->Left->Kind != IrExConst)
        {
            int cv = AllocReg(Next);
            if (cv < 0) return 0;
            In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
            In.A = (unsigned char)la; In.B = 0;
            In.Tok = TokenCast; In.Imm = 2;
            if (!Emit(Code, Cap, N, In)) return 0;
            la = cv;
        }
        if (E->Right->Kind == IrExConst && E->Right->Op != TokenFPConstant
            && *N >= 0 && *N < IR_LIN_STMT_MAX)
        {
            union { long long l; double d; } u;
            u.d = (double)E->Right->ConstVal;
            lb = AllocReg(Next);
            if (lb < 0) return 0;
            LinWideImm[*N] = u.l;
            LinWideOn[*N] = 1;
            if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
            In.Op = IrLinImm; In.Dest = (unsigned char)lb;
            In.A = In.B = 0; In.Tok = 2; In.Imm = *N;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        else if (!LowerRec(E->Right, Code, Cap, N, Next, &lb)) return 0;
        /* Variable int RHS in FP expr (a*n, s+=a[i]*n) — promote to IEEE. */
        if (!(E->Right->TypeFlags & IR_TF_FP) && E->Right->Kind != IrExConst)
        {
            int cv = AllocReg(Next);
            if (cv < 0) return 0;
            In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
            In.A = (unsigned char)lb; In.B = 0;
            In.Tok = TokenCast; In.Imm = 2;
            if (!Emit(Code, Cap, N, In)) return 0;
            lb = cv;
        }
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)la; In.B = (unsigned char)lb;
        In.Tok = E->Op; In.Imm = IR_LIN_F_FP;
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExUnary && E->Op == TokenMinus && E->Left != NULL)
    {
        /* Fold -FPConst to IEEE bits (integer UnaryMinus corrupts doubles). */
        if (E->Left->Kind == IrExConst && E->Left->Op == TokenFPConstant
            && *N >= 0 && *N < IR_LIN_STMT_MAX)
        {
            union { long long l; double d; } u;
            u.l = E->Left->ConstVal;
            u.d = -u.d;
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            LinWideImm[*N] = u.l;
            LinWideOn[*N] = 1;
            if (*N + 1 > LinWideMax) LinWideMax = *N + 1;
            In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
            In.A = In.B = 0; In.Tok = 2; In.Imm = *N;
            return Emit(Code, Cap, N, In);
        }
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)ld; In.B = 0; In.Tok = TokenMinus; In.Imm = 0;
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExUnary && (E->Op == TokenUnaryNot || E->Op == TokenUnaryExor) && E->Left != NULL)
    {
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)ld; In.B = 0; In.Tok = E->Op; In.Imm = 0;
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        int slot = E->Left->LocalSlot;
        int old = 0, neu;
        long long step = 1;
        /* pointer ++/-- advances by pointee size.
         * Local scalars: ONLY LocalIsPtr — never TypeFlags (stale TF_PTR on
         * loop vars scaled i+=4 → broken for/return-in-for/param sum). */
        /* Local ++/--: scale only if slot is pointer AND not compiling
         * under a foreign frame (callee body lowered on caller frame). */
        if (E->Left->LocalSlot >= 0)
        {
            /* The compile-time type cache stores the pointed-to aggregate
             * for local pointer slots. Prefer its size; infer a stale zero
             * Sizeof from member layout or a same-type array leaf stride. */
            if (((E->Left->TypeFlags & IR_TF_PTR) || (E->TypeFlags & IR_TF_PTR))
                && slot < IR_CALL_MAX_LOCALS && CompLocalFromType[slot] != NULL)
            {
                struct ValueType *PT = CompLocalFromType[slot];
                int ps = 0, ai;
                if (PT->Base == TypePointer && PT->FromType != NULL)
                    PT = PT->FromType;
                if (PT->Base == TypeStruct || PT->Base == TypeUnion)
                {
                    ps = PT->Sizeof;
                    {
                        int inferred = IrLowerInferAggregateSize(PT);
                        if (inferred > 0 && (ps <= 0 || inferred < ps)) ps = inferred;
                    }
                }
                /* The same-type slot scan only recovers a stale aggregate
                 * Sizeof. A scalar pointee must not borrow an unrelated
                 * slot's stride (char *t after char **pp = a stepped by
                 * sizeof(char *)); its step comes from the pointee below. */
                if (ps <= 0 && (PT->Base == TypeStruct || PT->Base == TypeUnion))
                    for (ai = 0; ai < IR_CALL_MAX_LOCALS; ++ai)
                        if (ai != slot && IrCompLocalLeafEsOrFrame(ai) > 0
                            && (CompLocalFromType[ai] == CompLocalFromType[slot]
                                || (CompLocalFromType[ai] != NULL
                                    && CompLocalFromType[ai]->Base == TypePointer
                                    && PT != NULL
                                    && CompLocalFromType[ai]->FromType == PT)))
                        { ps = IrCompLocalLeafEsOrFrame(ai); break; }
                if (ps > 0) step = ps;
            }
            if (!LinCompileActive && IrCallDepth > 0 && slot < IR_CALL_MAX_LOCALS
                && IrCallStack[IrCallDepth - 1]->LocalIsPtr[slot]
                && IrCallStack[IrCallDepth - 1]->LocalArraySize[slot] <= 0)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                struct ValueType *PT = Fr->LocalFromType[slot]
                    ? Fr->LocalFromType[slot] : Fr->LocalTyp[slot];
                int es = IrCompLocalPointeeSizeOrFrame(slot);
                if (es <= 0 && PT != NULL && PT->Base == TypePointer
                    && PT->FromType != NULL && PT->FromType->Sizeof > 0)
                    es = PT->FromType->Sizeof;
                if (es <= 0 && step <= 1 && Fr->LocalElemSize[slot] > 0)
                    es = (int)Fr->LocalElemSize[slot];
                if (es > 0) step = es;
                else if (step <= 1) step = (int)sizeof(int);
            }
            else if (step <= 1
                && ((E->Left->TypeFlags & IR_TF_PTR) || (E->TypeFlags & IR_TF_PTR)))
            {
                if (E->Left->ElemSize > 0) step = E->Left->ElemSize;
                else if (E->ElemSize > 0) step = E->ElemSize;
                else step = (int)sizeof(int);
            }
            /* else step = 1 — scalar local */
        }
        else if ((E->Left->TypeFlags & IR_TF_PTR) || (E->TypeFlags & IR_TF_PTR))
        {
            if (E->Left->ElemSize > 0) step = E->Left->ElemSize;
            else if (E->ElemSize > 0) step = E->ElemSize;
            else step = (int)sizeof(int);
        }
        {
            long long delta = (E->Op == TokenIncrement) ? step : -step;
            /* Keep narrow scalar ++/-- in the linear form: both the VM and
             * linear executors normalize AddLocal to the declared scalar width.
             * Rejecting these operations here forced whole-function fallback,
             * even though the instruction's semantics are already implemented. */
            /* Postfix needs old value first. */
            if (E->ConstVal != 0)
            {
                old = AllocReg(Next);
                if (old < 0) return 0;
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)old;
                In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            if ((slot >= 0 && slot < IR_CALL_MAX_LOCALS && CompLocalIsConst[slot])
                || (E->Left->TypeFlags & IR_TF_CONST))
            {
                /* const-qualified target: AddLocal has no read-only check.
                 * Emit Load/AddI/StoreLocal so StoreLocal raises the same
                 * "assignment of read-only variable" as the tree path. */
                int cur = old;
                if (E->ConstVal == 0)
                {
                    cur = AllocReg(Next);
                    if (cur < 0) return 0;
                    In.Op = IrLinLoadLocal; In.Dest = (unsigned char)cur;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
                    if (!Emit(Code, Cap, N, In)) return 0;
                }
                neu = AllocReg(Next);
                if (neu < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)neu;
                In.A = (unsigned char)cur; In.B = 0; In.Tok = 0; In.Imm = delta;
                if (!Emit(Code, Cap, N, In)) return 0;
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
            In.Op = IrLinStoreLocal; In.Dest = 0;
                In.A = (unsigned char)neu; In.B = 0; In.Tok = 0; In.Imm = slot;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = (E->ConstVal != 0) ? cur : neu;
                return 1;
            }
            neu = AllocReg(Next);
            if (neu < 0) return 0;
            /* Single op: Locals[slot] += delta; Dest = new */
            In.Op = IrLinAddLocal; In.Dest = (unsigned char)neu;
            In.A = (unsigned char)slot; In.B = 0; In.Tok = 0; In.Imm = delta;
            if (E->Left->I32 == TypeChar || E->Left->I32 == TypeUnsignedChar
                || E->Left->I32 == TypeShort || E->Left->I32 == TypeUnsignedShort)
                In.Tok = (unsigned short)E->Left->I32;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = (E->ConstVal != 0) ? old : neu;
            return 1;
        }
    }
    /* Param pointer ++/-- : LoadLocal Imm=-(ps+1), add step, store back. */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot >= 0 && E->Left->ParamSlot < IR_CALL_MAX_PARAMS)
    {
        int ps = E->Left->ParamSlot;
        int pimm = -(ps + 1);
        int oldr = 0, neur, pr;
        long long step = 1;
        if (E->Left->ElemSize > 0)
            step = E->Left->ElemSize;
        else if (E->ElemSize > 0)
            step = E->ElemSize;
        else if ((E->Left->TypeFlags & IR_TF_PTR) || (E->TypeFlags & IR_TF_PTR))
            step = 1; /* char* default; int* usually ElemSize=4 at annotate */
        {
            long long delta = (E->Op == TokenIncrement) ? step : -step;
            if (E->ConstVal != 0)
            {
                oldr = AllocReg(Next);
                if (oldr < 0) return 0;
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)oldr;
                In.A = In.B = 0; In.Tok = 0; In.Imm = pimm;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            pr = AllocReg(Next);
            if (pr < 0) return 0;
            In.Op = IrLinLoadLocal; In.Dest = (unsigned char)pr;
            In.A = In.B = 0; In.Tok = 0; In.Imm = pimm;
            if (!Emit(Code, Cap, N, In)) return 0;
            neur = AllocReg(Next);
            if (neur < 0) return 0;
            In.Op = IrLinAddI; In.Dest = (unsigned char)neur;
            In.A = (unsigned char)pr; In.B = 0; In.Tok = 0; In.Imm = delta;
            if (!Emit(Code, Cap, N, In)) return 0;
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
            In.Op = IrLinStoreLocal; In.Dest = 0;
            In.A = (unsigned char)neur; In.B = 0; In.Tok = 0; In.Imm = pimm;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = (E->ConstVal != 0) ? oldr : neur;
            return 1;
        }
    }
    /* Global scalar ++/-- : IncG (value via load only if needed later). */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot < 0 && E->Left->Ident != NULL
        && !(E->Left->TypeFlags & IR_TF_PTR) && !(E->TypeFlags & IR_TF_PTR))
    {
        int gi, oldr, neur;
        gi = GlobPush(E->Left);
        if (gi < 0) return 0;
        if (E->Op == TokenIncrement)
        {
            /* Always IncG first for speed; postfix old value = load-before. */
            if (E->ConstVal != 0)
            {
                oldr = AllocReg(Next);
                if (oldr < 0) return 0;
                In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)oldr;
                In.A = In.B = 0; In.Tok = 0; In.Imm = gi;
                if (!Emit(Code, Cap, N, In)) return 0;
                In.Op = IrLinIncG; In.Dest = 0; In.A = 0; In.B = 0;
                In.Tok = 0; In.Imm = gi;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = oldr;
                return 1;
            }
            In.Op = IrLinIncG; In.Dest = 0; In.A = 0; In.B = 0;
            In.Tok = 0; In.Imm = gi;
            if (!Emit(Code, Cap, N, In)) return 0;
            /* Prefix ++ yields the converted value after the store.  Read it
             * back rather than manufacturing zero: IncG is a side effect. */
            neur = AllocReg(Next);
            if (neur < 0) return 0;
            In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)neur;
            In.A = In.B = 0; In.Tok = 0; In.Imm = gi;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = neur;
            return 1;
        }
        oldr = AllocReg(Next);
        if (oldr < 0) return 0;
        In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)oldr;
        In.A = In.B = 0; In.Tok = 0; In.Imm = gi;
        if (!Emit(Code, Cap, N, In)) return 0;
        neur = AllocReg(Next);
        if (neur < 0) return 0;
        In.Op = IrLinAddI; In.Dest = (unsigned char)neur;
        In.A = (unsigned char)oldr; In.B = 0; In.Tok = 0; In.Imm = -1;
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinStoreGlobal; In.Dest = 0;
        In.A = (unsigned char)neur; In.B = 0; In.Tok = 0; In.Imm = gi;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = (E->ConstVal != 0) ? oldr : neur;
        return 1;
    }
    /* a[i][j] = v where a[i] is a pointer element (int *cells[4]; T **pp):
     * address = a[i] + j*sizeof(T), stored with the same Tok=2 pointer store
     * as *p = v (ownership/init tracking included). Integer/pointer T; for
     * 1/2-byte T the assignment's value is the stored element re-read
     * (C: the value of x = v is v converted to x's type). */
    if (E->Kind == IrExAssign && getenv("ASGN_TRACE")) fprintf(stderr, "[asgn] op=%d\n", E->Op);
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && (E->Op == 0 || E->Op == TokenAssign) && E->Right != NULL
        && E->Left->Right != NULL && IrVmLinearFeaturesOn())
    {
        struct ValueType *PT = IrLowerPtrRowPointee(E->Left->Left);
        int pt_dbl = (PT != NULL && PT->Base == TypeFP && PT->Sizeof == 8);
        int rhs_fp = (LinParser != NULL && IrExprIsFP(LinParser, E->Right));
        if (PT != NULL && (PT->Sizeof == 1 || PT->Sizeof == 2
                           || PT->Sizeof == 4 || PT->Sizeof == 8)
            && (IS_INTEGER_NUMERIC_TYPE(PT) || PT->Base == TypePointer || pt_dbl)
            && (pt_dbl || !rhs_fp))
        {
            int pr, ir, vr, sc, off, addr, es = (int)PT->Sizeof;
            if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &pr)) return 0;
            if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &ir)) return 0;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            off = AllocReg(Next);
            if (off < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off;
            In.A = (unsigned char)ir; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            addr = AllocReg(Next);
            if (addr < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)pr; In.B = (unsigned char)off;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &vr)) return 0;
            if (pt_dbl && !rhs_fp)
            {
                /* double *a[N]; a[i][j] = int: convert to IEEE bits. */
                int cv = AllocReg(Next);
                if (cv < 0) return 0;
                In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
                In.A = (unsigned char)vr; In.B = 0;
                In.Tok = TokenCast; In.Imm = 2;
                if (!Emit(Code, Cap, N, In)) return 0;
                vr = cv;
            }
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)vr; In.B = (unsigned char)addr;
            In.Tok = 2; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (es >= 4)
            {
                *Dest = vr;
                return 1;
            }
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr;
            In.B = IrTypeIsUnsignedBase(PT->Base) ? 1 : 0;
            In.Tok = 2; In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
    }
    /* Word-pointer element store into local T* / fptr arrays (strict).
     * One path for adj[i]=p and tab[i]=fn / tab[i]=&fn.
     * Function Ident must become Tok=5 Imm of Function Value* — not
     * LoadGlobal of the name (that is not a callable Value*). */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && E->Left->Left->LocalSlot >= 0
        && E->Left->Right != NULL && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && IrVmLinearFeaturesOn()
        && IrCompLocalElemIsPtrOrFrame(E->Left->Left->LocalSlot))
    {
        int slot = E->Left->Left->LocalSlot;
        int idx_r, base_r, sc, ioff, addr, wes = (int)sizeof(void *);
        IrExpr *R = E->Right;
        struct Value *TV = NULL;
        if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_r)) return 0;
        if (R->Kind == IrExUnary && R->Op == TokenAmpersand && R->Left != NULL)
            R = R->Left;
        if (R->Kind == IrExIdent && R->Ident != NULL && LinParser != NULL
            && R->LocalSlot < 0 && R->ParamSlot < 0)
            TV = IrGlobalIdentSoft(LinParser, R->Ident);
        if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
        {
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
            In.A = In.B = 0; In.Tok = 5;
            In.Imm = (long long)(intptr_t)TV;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        else if (!LowerRec(E->Right, Code, Cap, N, Next, Dest))
            return 0;
        base_r = AllocReg(Next);
        if (base_r < 0) return 0;
        In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_r;
        In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
        if (!Emit(Code, Cap, N, In)) return 0;
        sc = AllocReg(Next);
        if (sc < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
        In.A = In.B = 0; In.Tok = 0; In.Imm = wes;
        if (!Emit(Code, Cap, N, In)) return 0;
        ioff = AllocReg(Next);
        if (ioff < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)ioff;
        In.A = (unsigned char)idx_r; In.B = (unsigned char)sc;
        In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)addr;
        In.A = (unsigned char)base_r; In.B = (unsigned char)ioff;
        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)*Dest;
        In.B = (unsigned char)addr;
        In.Tok = 2; In.Imm = wes;
        return Emit(Code, Cap, N, In);
    }
    /* o.in.x++ / s.bf++ : bitfield member postfix/prefix.
     * Load Tok=1 (IdxMeta stamps BitWidth), add/sub 1, Store Tok=1.
     * Runtime IrBitExtract/Insert handles the field. */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Left->Ident != NULL && E->Left->Left != NULL)
    {
        int old, neu, is_bf = 0;
        long long delta = (E->Op == TokenIncrement) ? 1 : -1;
        {
            IrExpr *Base = E->Left->Left;
            struct ValueType *ST = NULL;
            struct Value *MV = NULL;
            if (Base->Kind == IrExIdent && Base->LocalSlot >= 0)
                ST = IrCompLocalFromTypeOrFrame(Base->LocalSlot);
            else if (Base->Kind == IrExMember && Base->Left != NULL
                     && Base->Left->Kind == IrExIdent && Base->Left->LocalSlot >= 0)
            {
                ST = IrCompLocalFromTypeOrFrame(Base->Left->LocalSlot);
                if (ST != NULL && ST->Members != NULL
                    && TableGet(ST->Members, Base->Ident, &MV, NULL, NULL, NULL)
                    && MV != NULL && MV->Typ != NULL)
                    ST = MV->Typ;
            }
            MV = NULL;
            if (ST != NULL && ST->Members != NULL
                && TableGet(ST->Members, E->Left->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->BitWidth > 0)
                is_bf = 1;
        }
        if (!is_bf) goto bitfield_inc_done;
        if (IdxN >= 31) return 0;
        old = AllocReg(Next);
        if (old < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)old;
        In.A = In.B = 0; In.Tok = 1; In.Imm = IdxN;
        IdxMetaPush(E->Left);
        if (!Emit(Code, Cap, N, In)) return 0;
        neu = AllocReg(Next);
        if (neu < 0) return 0;
        In.Op = IrLinAddI; In.Dest = (unsigned char)neu;
        In.A = (unsigned char)old; In.B = 0; In.Tok = 0; In.Imm = delta;
        if (!Emit(Code, Cap, N, In)) return 0;
        if (IdxN >= 32) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)neu; In.B = 0;
        In.Tok = 1; In.Imm = IdxN;
        IdxMetaPush(E->Left);
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = (E->ConstVal != 0) ? old : neu;
        return 1;
        bitfield_inc_done: ;
    }
    /* a[i]++ / a[i][j]++: LoadIndex, +/- 1, StoreIndex (1D + multidim flatten). */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Right != NULL)
    {
        IrExpr *IX = E->Left;
        IrExpr *Root = IX;
        IrExpr *Chain[IR_MAX_ARRAY_DIMS];
        int nc = 0, d, nd = 0, dims[IR_MAX_ARRAY_DIMS];
        int idx_regs[IR_MAX_ARRAY_DIMS];
        int rd, old, neu, one, flat_reg, tmp_reg, leaf_es = 4;
        int member_array = 0, member_total = 1;
        struct ValueType *MemberArrayType = NULL;
        IrLinInst Bin;
        while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
        {
            Chain[nc++] = Root;
            Root = Root->Left;
        }
        if (nc < 1 || nc > IR_MAX_ARRAY_DIMS || Root == NULL)
            return 0;
        /* Aggregate-member arrays have no LocalSlot on their base expression.
         * Resolve dimensions from the declared member type, rather than
         * rejecting the lvalue or pretending the member name is a global. */
        if (Root->Kind == IrExMember)
        {
            struct ValueType *Cur;
            MemberArrayType = LinExprValueType(Root, 0);
            if (MemberArrayType == NULL || MemberArrayType->Base != TypeArray)
                return 0;
            member_array = 1;
            Cur = MemberArrayType;
            while (Cur != NULL && Cur->Base == TypeArray
                   && nd < IR_MAX_ARRAY_DIMS)
            {
                dims[nd] = Cur->ArraySize > 0 ? (int)Cur->ArraySize : 0;
                if (dims[nd] <= 0 || member_total > 0x3fffffff / dims[nd])
                    return 0;
                member_total *= dims[nd];
                nd++;
                Cur = Cur->FromType;
            }
            if (Cur == NULL || nd != nc)
                return 0;
            leaf_es = LinTypeObjectSize(Cur);
            if (leaf_es <= 0)
                return 0;
        }
        else if (Root->Kind != IrExIdent)
            return 0;
        /* Outer→inner order for flatten */
        for (d = 0; d < nc / 2; d++)
        {
            IrExpr *tmp = Chain[d];
            Chain[d] = Chain[nc - 1 - d];
            Chain[nc - 1 - d] = tmp;
        }
        if (!member_array)
            for (d = 0; d < IR_MAX_ARRAY_DIMS; d++) dims[d] = 0;
        if (nc >= 2)
        {
            if (member_array)
            {
                /* dims/leaf_es were derived from the full declared member type. */
            }
            else if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
                && IrCallDepth > 0)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                nd = (int)Fr->LocalNDims[Root->LocalSlot];
                leaf_es = Fr->LocalElemSize[Root->LocalSlot] > 0
                    ? (int)Fr->LocalElemSize[Root->LocalSlot] : 4;
                for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                    dims[d] = Fr->LocalDims[Root->LocalSlot][d];
            }
            if (!member_array && nd < 2 && Root->Kind == IrExIdent
                && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
                && IrCompLocalNDimsOrFrame(Root->LocalSlot) >= 2)
            {
                int slot = Root->LocalSlot;
                nd = (int)IrCompLocalNDimsOrFrame(slot);
                leaf_es = IrCompLocalLeafEsOrFrame(slot) > 0 ? IrCompLocalLeafEsOrFrame(slot) : 4;
                for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                    dims[d] = IrCompLocalDimOrFrame(slot, d);
            }
            if (nd != nc || nd < 2)
                return 0;
            for (d = 1; d < nd; d++)
                if (dims[d] <= 0) return 0;
            for (d = 0; d < nc; d++)
                if (!LowerRec(Chain[d]->Right, Code, Cap, N, Next, &idx_regs[d]))
                    return 0;
            flat_reg = idx_regs[0];
            for (d = 1; d < nd; d++)
            {
                int dim_reg = AllocReg(Next);
                if (dim_reg < 0) return 0;
                Bin.Op = IrLinImm; Bin.Dest = (unsigned char)dim_reg;
                Bin.A = Bin.B = 0; Bin.Tok = 0; Bin.Imm = dims[d];
                if (!Emit(Code, Cap, N, Bin)) return 0;
                tmp_reg = AllocReg(Next);
                if (tmp_reg < 0) return 0;
                Bin.Op = IrLinBin; Bin.Dest = (unsigned char)tmp_reg;
                Bin.A = (unsigned char)flat_reg; Bin.B = (unsigned char)dim_reg;
                Bin.Tok = TokenAsterisk; Bin.Imm = 0;
                if (!Emit(Code, Cap, N, Bin)) return 0;
                flat_reg = AllocReg(Next);
                if (flat_reg < 0) return 0;
                Bin.Op = IrLinBin; Bin.Dest = (unsigned char)flat_reg;
                Bin.A = (unsigned char)tmp_reg; Bin.B = (unsigned char)idx_regs[d];
                Bin.Tok = TokenPlus; Bin.Imm = 0;
                if (!Emit(Code, Cap, N, Bin)) return 0;
            }
            rd = flat_reg;
            (void)leaf_es;
        }
        else
        {
            if (IdxN >= 31) return 0;
            if (!LowerRec(IX->Right, Code, Cap, N, Next, &rd))
                return 0;
        }
        if (IdxN >= 31) return 0;
        old = AllocReg(Next);
        if (old < 0) return 0;
        In.Op = member_array ? IrLinLoadMemberArray : IrLinLoadIndex; In.Dest = (unsigned char)old;
        In.A = (unsigned char)rd; In.B = 0;
        In.Tok = 0;
        In.Imm = IdxN;
        IdxMetaPush(IX); /* leaf Index for meta es/slot */
        if (member_array && IdxN > 0 && IdxN < 32)
        {
            int mi = IdxN - 1;
            /* Kind 5 denotes a fixed array subobject reached through a member
             * chain. The executor resolves that base with IrSafeMemberAddr and
             * applies the declared flattened extent before any memory access. */
            IdxMetaSlot[mi] = -1;
            IdxMetaKind[mi] = 5;
            IdxMetaEs[mi] = leaf_es;
            IdxMetaN[mi] = member_total;
        }
        if (!Emit(Code, Cap, N, In)) return 0;
        one = AllocReg(Next);
        if (one < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)one;
        In.A = In.B = 0; In.Tok = 0; In.Imm = 1;
        if (!Emit(Code, Cap, N, In)) return 0;
        neu = AllocReg(Next);
        if (neu < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)neu;
        In.A = (unsigned char)old; In.B = (unsigned char)one;
        In.Tok = (E->Op == TokenIncrement) ? TokenPlus : TokenMinus;
        In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = member_array ? IrLinStoreMemberArray : IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)neu; In.B = (unsigned char)rd;
        In.Tok = 0;
        In.Imm = IdxN;
        IdxMetaPush(IX);
        if (member_array && IdxN > 0 && IdxN < 32)
        {
            int mi = IdxN - 1;
            IdxMetaSlot[mi] = -1;
            IdxMetaKind[mi] = 5;
            IdxMetaEs[mi] = leaf_es;
            IdxMetaN[mi] = member_total;
        }
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = (E->ConstVal != 0) ? old : neu;
        return 1;
    }
    /* Nested/pointer-chain Member ++/-- on Linear: q->next->v++ / (*p).m++.
     * Same ST + parent-pointer model as pure Member load; Tok=2 load/±1/store. */
    if (IrVmLinearFeaturesOn()
        && E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Left->Left != NULL && E->Left->Ident != NULL
        && E->Left->BitWidth <= 0
        && (E->Left->Left->Kind == IrExMember
            || E->Left->Left->Kind == IrExUnary
            || E->Left->Left->Kind == IrExCall))
    {
        IrExpr *M = E->Left;
        IrExpr *Root = M->Left;
        IrExpr *Chain[8];
        int nc = 0, ci, stars = 0;
        struct ValueType *ST = NULL;
        int pr, addr, off = 0, es = 4, eb = TypeInt;
        int old, neu, one, parent_is_pointer = 0;
        while (Root != NULL && Root->Kind == IrExMember && nc < 8)
        {
            Chain[nc++] = Root;
            Root = Root->Left;
        }
        while (Root != NULL && Root->Kind == IrExUnary && Root->Op == TokenAsterisk)
        {
            stars++;
            Root = Root->Left;
        }
        if (Root != NULL && Root->Kind == IrExCall)
        {
            if (Root->CachedFunc != NULL && Root->CachedFunc->Val != NULL)
                ST = Root->CachedFunc->Val->FuncDef.ReturnType;
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
            if (ST == NULL)
                ST = IrLowerCallReturnStruct(Root);
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        else if (Root != NULL && Root->Kind == IrExIdent
                 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* GUARD: dual-read */
            ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            if (stars >= 1)
            {
                int si;
                for (si = 0; si < stars && ST; si++)
                    if (ST->Base == TypePointer && ST->FromType) ST = ST->FromType;
                    else break;
                if (ST && ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
            else if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
            else if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion))
            {
                /* Embedded aggregates (o.inner.field) are valid too. The
                 * previous gate required some pointer-valued intermediate
                 * member, rejecting ordinary nested structs such as
                 * Nest.wide.vcount. Preserve ST so the member walk below can
                 * resolve the embedded layout. */
            }
            else
                ST = NULL;
        }
        for (ci = nc - 1; ci >= 0 && ST != NULL; ci--)
        {
            struct ValueType *FieldType;
            struct Value *CM = NULL;
            if (ST->Members == NULL) { ST = NULL; break; }
            if (!TableGet(ST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                || CM == NULL || CM->Typ == NULL)
            { ST = NULL; break; }
            FieldType = CM->Typ;
            if (ci == 0 && FieldType->Base == TypePointer)
                parent_is_pointer = 1;
            ST = FieldType;
            if (ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        if (ST == NULL && Root != NULL && Root->Kind == IrExIdent
            && Root->Ident != NULL && LinParser != NULL
            && Root->LocalSlot < 0 && Root->ParamSlot < 0)
        {
            /* Global aggregate root: the local-frame branch above cannot
             * supply its type metadata. Use the declared global type and
             * follow the same member chain; do not require a pointer field. */
            struct Value *GV = IrGlobalValSoft(LinParser, Root);
            ST = (GV != NULL) ? GV->Typ : NULL;
            if (ST != NULL && ST->Base == TypePointer && ST->FromType != NULL)
                ST = ST->FromType;
            parent_is_pointer = 0;
            for (ci = nc - 1; ci >= 0 && ST != NULL; ci--)
            {
                struct Value *CM = NULL;
                struct ValueType *FieldType;
                if (ST->Members == NULL
                    || !TableGet(ST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                    || CM == NULL || CM->Typ == NULL)
                { ST = NULL; break; }
                FieldType = CM->Typ;
                if (ci == 0 && FieldType->Base == TypePointer)
                    parent_is_pointer = 1;
                ST = FieldType;
                if (ST->Base == TypePointer && ST->FromType != NULL)
                    ST = ST->FromType;
            }
        }
        if (ST != NULL && IrLowerMemberOffEs(ST, M->Ident, &off, &es, &eb))
        {
            if (M->Left->Kind == IrExMember && !parent_is_pointer)
            {
                /* Embedded member expression is an lvalue, not an aggregate
                 * integer value. Ask the shared safe-member resolver for its
                 * address; Tok=5 is the existing member-address operation. */
                if (IdxN >= 31) return 0;
                pr = AllocReg(Next);
                if (pr < 0) return 0;
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)pr;
                In.A = In.B = 0; In.Tok = 5; In.Imm = IdxN;
                IdxMetaPush(M->Left);
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else if (M->Left->Kind == IrExUnary && M->Left->Op == TokenAsterisk
                && M->Op == TokenDot && M->Left->Left != NULL)
            {
                if (!LowerRec(M->Left->Left, Code, Cap, N, Next, &pr)) return 0;
            }
            else if (!LowerRec(M->Left, Code, Cap, N, Next, &pr)) return 0;
            if (off != 0)
            {
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)pr; In.B = 0;
                In.Tok = 0; In.Imm = off;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else
                addr = pr;
            old = AllocReg(Next);
            if (old < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)old;
            In.A = (unsigned char)addr; In.B = 0; In.Tok = 2; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            one = AllocReg(Next);
            if (one < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)one;
            In.A = In.B = 0; In.Tok = 0; In.Imm = 1;
            if (!Emit(Code, Cap, N, In)) return 0;
            neu = AllocReg(Next);
            if (neu < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)neu;
            In.A = (unsigned char)old; In.B = (unsigned char)one;
            In.Tok = (E->Op == TokenIncrement) ? TokenPlus : TokenMinus;
            In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)neu; In.B = (unsigned char)addr;
            In.Tok = 2; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = (E->ConstVal != 0) ? old : neu;
            return 1;
        }
    }
    /* Member ++/-- : s.m++ / p->m++ / a[i].m++ (Ident or Index-of-Ident base).
     * Linear Step: IrSafeMemberAddr + IrBitExtract/Insert when BitWidth stamped.
     * Nested (a.b.c++, (*p).m++) stay on tree. */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Left->Left != NULL && E->Left->Ident != NULL)
    {
        IrExpr *Base = E->Left->Left;
        IrExpr *M = E->Left;
        IrExpr *Root = Base;
        int old, neu, one, mem_idx_reg = 0;
        if (Base->Kind == IrExIndex
            && Base->Left != NULL && Base->Left->Kind == IrExIdent
            && Base->Right != NULL)
        {
            Root = Base->Left;
            if (Base->Right->Kind != IrExConst)
            {
                if (!LowerRec(Base->Right, Code, Cap, N, Next, &mem_idx_reg))
                    return 0;
            }
        }
        else if (Base->Kind != IrExIdent)
            return 0;
        /* Resolve bitfield meta from element/struct type. */
        if (M->BitWidth <= 0)
        {
            struct ValueType *ST = NULL;
            struct Value *MV = NULL;
            if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                /* GUARD: dual-read */
                ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            }
            else if (Root->ParamSlot >= 0 && IrCallDepth > 0
                     && Root->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                ST = Fr->ParamTyp[Root->ParamSlot];
            }
            else if (Root->Ident != NULL && LinParser != NULL)
            {
                struct Value *GV = IrGlobalValSoft(LinParser, Root);
                if (GV != NULL) ST = GV->Typ;
            }
            if (ST != NULL && ST->Base == TypeArray && ST->FromType != NULL)
                ST = ST->FromType;
            if (ST != NULL && ST->Base == TypePointer && ST->FromType != NULL)
                ST = ST->FromType;
            if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                && ST->Members != NULL
                && TableGet(ST->Members, M->Ident, &MV, NULL, NULL, NULL)
                && MV != NULL && MV->Val != NULL && MV->BitWidth > 0)
            {
                M->BitWidth = MV->BitWidth;
                M->BitOffset = MV->BitOffset;
                M->BitSigned = 1;
                if (MV->Typ != NULL)
                {
                    switch (MV->Typ->Base)
                    {
                        case TypeUnsignedInt: case TypeUnsignedShort:
                        case TypeUnsignedLong: case TypeUnsignedLongLong:
                        case TypeUnsignedChar:
                            M->BitSigned = 0;
                            break;
                        default: break;
                    }
                }
            }
        }
        if (IdxN >= 31) return 0;
        old = AllocReg(Next);
        if (old < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)old;
        In.A = (unsigned char)mem_idx_reg; In.B = 0; In.Tok = 1; In.Imm = IdxN;
        IdxMetaPush(M);
        if (!Emit(Code, Cap, N, In)) return 0;
        one = AllocReg(Next);
        if (one < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)one;
        In.A = In.B = 0; In.Tok = 0; In.Imm = 1;
        if (!Emit(Code, Cap, N, In)) return 0;
        neu = AllocReg(Next);
        if (neu < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)neu;
        In.A = (unsigned char)old; In.B = (unsigned char)one;
        In.Tok = (E->Op == TokenIncrement) ? TokenPlus : TokenMinus;
        In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)neu; In.B = (unsigned char)mem_idx_reg;
        In.Tok = 1; In.Imm = IdxN;
        IdxMetaPush(M);
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = (E->ConstVal != 0) ? old : neu;
        return 1;
    }
    /* Increment/decrement through an arbitrary pointer-valued expression,
     * e.g. ++*(s.p), ++*(p->next), or (*get_ptr())++.  The older fast path
     * below handles a direct local pointer identifier.  This path evaluates
     * the pointer expression once and uses the shared typed indirect
     * load/store, so call arguments do not reject lvalues merely because the
     * pointer is a member rather than a local name. */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExUnary
        && E->Left->Op == TokenAsterisk && E->Left->Left != NULL
        && (!(E->Left->Left->Kind == IrExIdent
              && E->Left->Left->LocalSlot >= 0)
            || LinExprPointeeIsPointer(E->Left)))
    {
        struct ValueType *LT = LinExprValueType(E->Left, 0);
        int addr, old, step_reg, neu, es, step, is_ptr;
        long long packed_size;
        if (LT == NULL || LT->Base == TypeFP || LT->Base == TypeStruct
            || LT->Base == TypeUnion || LT->Base == TypeArray
            || LT->Base == TypeFunction)
            return 0;
        es = LinTypeObjectSize(LT);
        is_ptr = LT->Base == TypePointer;
        step = is_ptr ? LinTypeObjectSize(LT->FromType) : 1;
        if (is_ptr && (LT->FromType == NULL || step <= 0)) return 0;
        if (es != 1 && es != 2 && es != 4 && es != 8) return 0;
        if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &addr) || addr < 0)
            return 0;
        old = AllocReg(Next);
        if (old < 0) return 0;
        packed_size = es;
        if (E->SourceLine > 0 && E->SourceLine <= 0xFFFF)
            packed_size = ((long long)E->SourceLine << 32)
                        | ((long long)(E->CharacterPos > 0 ? E->CharacterPos - 1 : 0)
                           << 16) | es;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)old;
        In.A = (unsigned char)addr; In.B = (unsigned char)IrTypeIsUnsignedBase(LT->Base);
        In.Tok = 2; In.Imm = packed_size;
        if (!Emit(Code, Cap, N, In)) return 0;
        step_reg = AllocReg(Next);
        if (step_reg < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)step_reg;
        In.A = In.B = 0; In.Tok = 0;
        In.Imm = (E->Op == TokenIncrement) ? step : -step;
        if (!Emit(Code, Cap, N, In)) return 0;
        neu = AllocReg(Next);
        if (neu < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)neu;
        In.A = (unsigned char)old; In.B = (unsigned char)step_reg;
        In.Tok = TokenPlus; In.Imm = is_ptr ? IR_LIN_F_WIDE : 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreDeref; In.Dest = 0;
        In.A = (unsigned char)neu; In.B = (unsigned char)addr;
        In.Tok = 0; In.Imm = packed_size;
        if (!Emit(Code, Cap, N, In)) return 0;
        if (E->ConstVal == 0 && (es == 1 || es == 2))
        {
            /* Re-load the converted value for prefix ++/--; the store
             * performs C's lvalue conversion (not the untruncated arithmetic). */
            *Dest = AllocReg(Next);
            if (*Dest < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)addr;
            In.B = (unsigned char)IrTypeIsUnsignedBase(LT->Base);
            In.Tok = 2; In.Imm = packed_size;
            return Emit(Code, Cap, N, In);
        }
        *Dest = (E->ConstVal != 0) ? old : neu;
        return 1;
    }
    /* *p ++/-- : (*p)++ / ++*p (local pointer Ident). */
    if (E->Kind == IrExUnary
        && (E->Op == TokenIncrement || E->Op == TokenDecrement)
        && E->Left != NULL && E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && E->Left->Left->LocalSlot >= 0)
    {
        int old, neu, one;
        /* This path adds the integer 1. A pointer pointee (T **p) steps
         * by sizeof(T) in the typed path above; a floating pointee is not
         * an integer add. */
        {
            struct ValueType *LT = LinExprValueType(E->Left, 0);
            if (LT != NULL && (LT->Base == TypeFP || LT->Base == TypePointer
                               || LT->Base == TypeStruct || LT->Base == TypeUnion
                               || LT->Base == TypeArray || LT->Base == TypeFunction))
                return 0;
        }
        if (IdxN >= 31) return 0;
        old = AllocReg(Next);
        if (old < 0) return 0;
        In.Op = IrLinLoadIndex; In.Dest = (unsigned char)old;
        In.A = In.B = 0; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(E->Left);
        if (!Emit(Code, Cap, N, In)) return 0;
        one = AllocReg(Next);
        if (one < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)one;
        In.A = In.B = 0; In.Tok = 0; In.Imm = 1;
        if (!Emit(Code, Cap, N, In)) return 0;
        neu = AllocReg(Next);
        if (neu < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)neu;
        In.A = (unsigned char)old; In.B = (unsigned char)one;
        In.Tok = (E->Op == TokenIncrement) ? TokenPlus : TokenMinus;
        In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)neu; In.B = 0; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(E->Left);
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = (E->ConstVal != 0) ? old : neu;
        return 1;
    }
    /* Short-circuit && / || (F8 expand). */
    if (E->Kind == IrExBinOp && E->Left != NULL && E->Right != NULL
        && (E->Op == TokenLogicalAnd || E->Op == TokenLogicalOr))
    {
        int brz, jmp, rd, tmp;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        if (E->Op == TokenLogicalAnd)
        {
            brz = *N;
            In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)ld; In.B = 0;
            In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
            tmp = AllocReg(Next);
            if (tmp < 0) return 0;
            In.Op = IrLinUnary; In.Dest = (unsigned char)tmp;
            In.A = (unsigned char)rd; In.B = 0; In.Tok = TokenUnaryNot; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)tmp; In.B = 0; In.Tok = TokenUnaryNot; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            jmp = *N;
            In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            Code[brz].Imm = *N;
            In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
            In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            Code[jmp].Imm = *N;
            return 1;
        }
        brz = *N;
        In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)ld; In.B = 0;
        In.Tok = 0; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)*Dest;
        In.A = In.B = 0; In.Tok = 0; In.Imm = 1;
        if (!Emit(Code, Cap, N, In)) return 0;
        jmp = *N;
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        Code[brz].Imm = *N;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        tmp = AllocReg(Next);
        if (tmp < 0) return 0;
        In.Op = IrLinUnary; In.Dest = (unsigned char)tmp;
        In.A = (unsigned char)rd; In.B = 0; In.Tok = TokenUnaryNot; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinUnary; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)tmp; In.B = 0; In.Tok = TokenUnaryNot; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        Code[jmp].Imm = *N;
        return 1;
    }
    if (E->Kind == IrExTernary && E->Left != NULL && E->Right != NULL
        && E->Args != NULL && E->ArgCount >= 1 && E->Args[0] != NULL)
    {
        int brz, jmp, arm;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        *Dest = AllocReg(Next);
        if (*Dest < 0) return 0;
        brz = *N;
        In.Op = IrLinBrZ; In.Dest = 0; In.A = (unsigned char)ld; In.B = 0;
        In.Tok = 0; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseReset(void); LinCseReset(); }
        if (!LowerRec(E->Right, Code, Cap, N, Next, &arm)) return 0;
        if (IrVmLinearFeaturesOn() && LinExprHasFpValue(E)
            && !LinExprHasFpValue(E->Right)
            && !LinEmitIntToFp(&arm, Code, Cap, N, Next)) return 0;
        if (!LinExprHasFpValue(E)
            && !LinEmitCanonicalArmCast(&arm, E->Right, (enum BaseType)E->CanonicalOpBase,
                                        Code, Cap, N, Next)) return 0;
        /* Copy arm → dest with WIDE +0 so pointer/string results keep full
         * 64-bit bits (non-wide Plus truncates to int32 → SEGV on *s). */
        {
            int z = AllocReg(Next);
            if (z < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)z;
            In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)arm; In.B = (unsigned char)z;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        jmp = *N;
        In.Op = IrLinJmp; In.Dest = In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        Code[brz].Imm = *N;
        { extern void LinCseReset(void); LinCseReset(); }
        if (!LowerRec(E->Args[0], Code, Cap, N, Next, &arm)) return 0;
        if (IrVmLinearFeaturesOn() && LinExprHasFpValue(E)
            && !LinExprHasFpValue(E->Args[0])
            && !LinEmitIntToFp(&arm, Code, Cap, N, Next)) return 0;
        if (!LinExprHasFpValue(E)
            && !LinEmitCanonicalArmCast(&arm, E->Args[0], (enum BaseType)E->CanonicalOpBase,
                                        Code, Cap, N, Next)) return 0;
        {
            int z = AllocReg(Next);
            if (z < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)z;
            In.A = In.B = 0; In.Tok = 0; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
            In.A = (unsigned char)arm; In.B = (unsigned char)z;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        Code[jmp].Imm = *N;
        return 1;
    }
    if (E->Kind == IrExBinOp && E->Op == TokenComma && E->Left != NULL && E->Right != NULL)
    {
        int leftv;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &leftv)) return 0;
        return LowerRec(E->Right, Code, Cap, N, Next, Dest);
    }
    /* Pointer - pointer → element count (scale by ElemSize).
     * Robust: TypeFlags PTR, or either side looks like decayed array/ptr. */
    if (E->Kind == IrExBinOp && E->Op == TokenMinus
        && E->Left != NULL && E->Right != NULL)
    {
        int lp = (E->Left->TypeFlags & IR_TF_PTR) != 0
              || (E->Left->ElemSize > 0 && E->Left->Kind == IrExIdent)
              || (E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0
                  && IrCallDepth > 0
                  && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
                  && (IrCallStack[IrCallDepth - 1]->LocalIsPtr[E->Left->LocalSlot]
                      || IrCallStack[IrCallDepth - 1]->LocalArraySize[E->Left->LocalSlot] > 0));
        int rp = (E->Right->TypeFlags & IR_TF_PTR) != 0
              || (E->Right->ElemSize > 0 && E->Right->Kind == IrExIdent)
              || (E->Right->Kind == IrExIdent && E->Right->LocalSlot >= 0
                  && IrCallDepth > 0
                  && E->Right->LocalSlot < IR_CALL_MAX_LOCALS
                  && (IrCallStack[IrCallDepth - 1]->LocalIsPtr[E->Right->LocalSlot]
                      || IrCallStack[IrCallDepth - 1]->LocalArraySize[E->Right->LocalSlot] > 0));
        int dstride = 0;
        /* (a[i] + 9) - a[i]: declared-pointee operands (shared classifier). */
        if (!(lp && rp))
        {
            int sl = LinPtrOperandStride(E->Left, 0);
            int sr = LinPtrOperandStride(E->Right, 0);
            if ((sl > 0 || sr > 0) && LinOperandIsGenuinePtr(E->Left, 0)
                && LinOperandIsGenuinePtr(E->Right, 0))
            {
                lp = rp = 1;
                dstride = (sl > 0) ? sl : sr;
            }
        }
        if (lp && rp)
    {
        int scale = dstride > 0 ? dstride
                  : E->Left->ElemSize > 0 ? E->Left->ElemSize
                  : (E->Right->ElemSize > 0 ? E->Right->ElemSize
                     : (E->ElemSize > 0 ? E->ElemSize : (int)sizeof(int)));
        int diff, sc, out;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        diff = AllocReg(Next);
        if (diff < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)diff;
        In.A = (unsigned char)ld; In.B = (unsigned char)rd;
        In.Tok = TokenMinus; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        if (scale != 1)
        {
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = scale;
            if (!Emit(Code, Cap, N, In)) return 0;
            out = AllocReg(Next);
            if (out < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)out;
            In.A = (unsigned char)diff; In.B = (unsigned char)sc;
            In.Tok = TokenSlash; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = out;
        }
        else
            *Dest = diff;
        return 1;
    } /* lp && rp */
    }
    /* Pointer +/- integer: scale integer by ElemSize then add/sub.
     * Only real pointer operands scale (LocalIsPtr / LocalArraySize / &x).
     * Do NOT trust IR_TF_PTR on the BinOp alone — false positives scaled
     * a+f(a) for unsigned Class B under EXEC=vm. */
    if (E->Kind == IrExBinOp && E->Left != NULL && E->Right != NULL
        && (E->Op == TokenPlus || E->Op == TokenMinus)
        && ((E->TypeFlags & IR_TF_PTR)
            || (E->Left->TypeFlags & IR_TF_PTR)
            || (E->Right->TypeFlags & IR_TF_PTR)
            || (E->Left->Kind == IrExIndex && E->Left->PointeeType != NULL)
            || (E->Right->Kind == IrExIndex && E->Right->PointeeType != NULL)
            || (E->Left->Kind == IrExIdent
                && IrLinEmitTypeIsPointerOrArray(E->Left->LocalSlot))
            || (E->Right->Kind == IrExIdent
                && IrLinEmitTypeIsPointerOrArray(E->Right->LocalSlot))
            || (E->Left->Kind == IrExIdent && E->Left->LocalSlot >= 0
                && LinCompileActive && CompileSlot != NULL
                && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
                && CompileSlot->SlotIsPtr[E->Left->LocalSlot])
            || (E->Right->Kind == IrExIdent && E->Right->LocalSlot >= 0
                && LinCompileActive && CompileSlot != NULL
                && E->Right->LocalSlot < IR_CALL_MAX_LOCALS
                && CompileSlot->SlotIsPtr[E->Right->LocalSlot])))
    {
        int scale = E->ElemSize;
        int ptr_side = -1; /* 0=left is ptr, 1=right is ptr, -1=none */
        int pr, ir, sc, scaled, out;
        int left_is_ptr = 0, right_is_ptr = 0;
        left_is_ptr = LinOperandIsGenuinePtr(E->Left, 0);
        right_is_ptr = LinOperandIsGenuinePtr(E->Right, 0);
        /* Refuse scale if neither side is a genuine pointer (Class B gate). */
        if (!left_is_ptr && !right_is_ptr)
            ; /* fall through to plain BinOp */
        else
        {
        {
            /* a[i] + n on T *a[N]: the row's ElemSize is the slot size
             * (sizeof(void *)); the stride is the declared pointee. */
            int ds = LinPtrOperandStride(left_is_ptr ? E->Left : E->Right, 0);
            if (ds > 0)
                scale = ds;
        }
        if (scale <= 0)
            scale = (E->Left->ElemSize > 0) ? E->Left->ElemSize
                  : (E->Right->ElemSize > 0) ? E->Right->ElemSize : 0;
        if (scale <= 0)
        {
            int ls = -1;
            if (E->Left->Kind == IrExIdent) ls = E->Left->LocalSlot;
            else if (E->Right->Kind == IrExIdent) ls = E->Right->LocalSlot;
            if (ls >= 0 && ls < IR_CALL_MAX_LOCALS && IrCompLocalLeafEsOrFrame(ls) > 0)
                scale = IrCompLocalLeafEsOrFrame(ls);
        }
        if (scale <= 0)
        {
            /* Call returning T*: scale from pointee size (int* → 4, short* → 2). */
            IrExpr *CSide = NULL;
            struct ValueType *RT;
            if (E->Left->Kind == IrExCall) CSide = E->Left;
            else if (E->Right->Kind == IrExCall) CSide = E->Right;
            RT = IrLowerCallReturnType(CSide);
            if (RT != NULL && RT->Base == TypePointer && RT->FromType != NULL
                && RT->FromType->Sizeof > 0)
                scale = RT->FromType->Sizeof;
        }
        if (scale <= 0)
            scale = (int)sizeof(int); /* last-resort; Resolve often leaves 4 */
        if (left_is_ptr)
            ptr_side = 0;
        else
            ptr_side = 1;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        pr = (ptr_side == 0) ? ld : rd;
        ir = (ptr_side == 0) ? rd : ld;
        if (scale != 1)
        {
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = scale;
            if (!Emit(Code, Cap, N, In)) return 0;
            scaled = AllocReg(Next);
            if (scaled < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)scaled;
            In.A = (unsigned char)ir; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            ir = scaled;
        }
        out = AllocReg(Next);
        if (out < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)out;
        /* Pointer results must be full width (64-bit hosts); 32-bit ALU
         * truncates addresses → SEGV on subsequent *p. */
        if (ptr_side == 0)
        {
            In.A = (unsigned char)pr; In.B = (unsigned char)ir;
            In.Tok = E->Op; In.Imm = IR_LIN_F_WIDE;
        }
        else
        {
            /* integer + pointer → pointer + integer; integer - pointer refused */
            if (E->Op == TokenMinus) return 0;
            In.A = (unsigned char)pr; In.B = (unsigned char)ir;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
        }
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = out;
        return 1;
        } /* genuine pointer operand */
    }
    if (E->Kind == IrExBinOp && E->Left != NULL && E->Right != NULL
        && (E->Op == TokenPlus || E->Op == TokenMinus || E->Op == TokenAsterisk
            || E->Op == TokenSlash || E->Op == TokenModulus || IsRel(E->Op)
            || E->Op == TokenAmpersand || E->Op == TokenArithmeticOr || E->Op == TokenArithmeticExor
            || E->Op == TokenShiftLeft || E->Op == TokenShiftRight))
    {
        long long fl = 0;
        int result_reg;
        /* Expression-local stack discipline: reserve the result slot before
           lowering children, so their temporaries are younger registers.
           Rewind after the parent instruction; pinned CSE registers are skipped
           by AllocReg and therefore remain valid across this reuse. */
        result_reg = AllocReg(Next);
        if (result_reg < 0) return 0;
        if (!LowerRec(E->Left, Code, Cap, N, Next, &ld)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rd)) return 0;
        *Dest = result_reg;
        /* C's usual arithmetic conversions apply to relational/equality
         * expressions too. Compare IEEE values, not their integer bit patterns. */
        if (IrVmLinearFeaturesOn() && IsRel(E->Op)
            && (LinExprHasFpValue(E->Left) || LinExprHasFpValue(E->Right)))
        {
            if (!LinExprHasFpValue(E->Left)
                && !LinEmitIntToFp(&ld, Code, Cap, N, Next)) return 0;
            if (!LinExprHasFpValue(E->Right)
                && !LinEmitIntToFp(&rd, Code, Cap, N, Next)) return 0;
            fl |= IR_LIN_F_FP;
        }
        /* Structural bridge: TypeFlags → opcode Imm (UNSIGNED/WIDE).
         * Also query frame/LocalFromType when flags missing (unsigned int a+f). */
        if (((E->CanonicalOpFlags & IR_CF_UNSIGNED) != 0))
            fl |= IR_LIN_F_UNSIGNED;
        if (((E->CanonicalOpFlags & IR_CF_WIDE) != 0))
            fl |= IR_LIN_F_WIDE;
        /* Right shift signedness comes from the left operand's declared type.
         * Forcing UNSIGNED here made every signed long long >> logical. */
        /* FP operands: IEEE ALU (062 z*1e15) in the Linear semantic path. */
        if (IrVmLinearFeaturesOn()
            && ((E->TypeFlags & IR_TF_FP)
                || (E->Left && (E->Left->TypeFlags & IR_TF_FP))
                || (E->Right && (E->Right->TypeFlags & IR_TF_FP))
                || (E->Left && E->Left->Kind == IrExConst
                    && E->Left->Op == TokenFPConstant)
                || (E->Right && E->Right->Kind == IrExConst
                    && E->Right->Op == TokenFPConstant))
            && (E->Op == TokenPlus || E->Op == TokenMinus
                || E->Op == TokenAsterisk || E->Op == TokenSlash))
            fl |= IR_LIN_F_FP;
        In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
        In.A = (unsigned char)ld; In.B = (unsigned char)rd;
        In.Tok = E->Op; In.Imm = fl;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Next = result_reg + 1;
        return 1;
    }
    /* User-call assign: s = f(args) — before other assign paths. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && E->Right->Kind == IrExCall)
    {
        int rhs;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs))
            return 0;
        In.Op = IrLinStoreLocal; In.Dest = 0;
        In.A = (unsigned char)rhs; In.B = 0; In.Tok = 0;
        In.Imm = E->Left->LocalSlot;
        if (!Emit(Code, Cap, N, In)) return 0;
        /* p = malloc(...): mark pointer so *p / p[i] resolve.
         * Only when LHS is declared pointer — never inherit TF_PTR from Call
         * result (false positive: int s = f() then s+g() scaled as pointer). */
        if (IrCallDepth > 0 && E->Left->LocalSlot < IR_CALL_MAX_LOCALS
            && (E->Left->TypeFlags & IR_TF_PTR)
            && !(IrCallStack[IrCallDepth - 1]->LocalOwned[E->Left->LocalSlot]
                 && IrCallStack[IrCallDepth - 1]->LocalArraySize[E->Left->LocalSlot] <= 0))
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            Fr->LocalIsPtr[E->Left->LocalSlot] = 1;
            IrLiveMarkInit(Fr, E->Left->LocalSlot);
            if (Fr->LocalElemSize[E->Left->LocalSlot] == 0)
            {
                if (E->Left->ElemSize > 0)
                    Fr->LocalElemSize[E->Left->LocalSlot] =
                        (unsigned char)(E->Left->ElemSize < 256 ? E->Left->ElemSize : 255);
                else if (Fr->LocalFromType[E->Left->LocalSlot]
                         && Fr->LocalFromType[E->Left->LocalSlot]->Sizeof > 0)
                {
                    int sz = Fr->LocalFromType[E->Left->LocalSlot]->Sizeof;
                    Fr->LocalElemSize[E->Left->LocalSlot] =
                        (unsigned char)(sz > 0 && sz < 256 ? sz : 255);
                }
            }
        }
        *Dest = rhs;
        return 1;
    }
    /* s.fn = add / &add: store Function Value* into member fptr. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Right != NULL && (E->Op == 0 || E->Op == TokenAssign)
        && E->Left->Left != NULL && E->Left->Ident != NULL)
    {
        struct Value *TV = NULL;
        IrExpr *R = E->Right;
        int rd = -1;
        if (R->Kind == IrExUnary && R->Op == TokenAmpersand && R->Left != NULL)
            R = R->Left;
        if (R->Kind == IrExIdent && R->Ident != NULL && LinParser != NULL)
            TV = IrGlobalIdentSoft(LinParser, R->Ident);
        if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
        {
            /* Reuse Member store path: Imm Value* into member via LowerRec-like
             * StoreIndex Tok=1 after setting RHS as Imm. */
            int dest_imm;
            dest_imm = AllocReg(Next);
            if (dest_imm < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)dest_imm;
            In.A = In.B = 0; In.Tok = 5;
            In.Imm = (long long)(intptr_t)TV;
            if (!Emit(Code, Cap, N, In)) return 0;
            /* Store via existing Member assign by temporarily... fall through
             * with RHS already in dest_imm: emit StoreIndex Tok=1. */
            {
                int idx_reg = 0;
                if (IdxN >= 32) return 0;
                if (E->Left->Left->Kind == IrExIndex
                    && E->Left->Left->Right != NULL
                    && E->Left->Left->Right->Kind != IrExConst)
                {
                    if (!LowerRec(E->Left->Left->Right, Code, Cap, N, Next, &idx_reg))
                        return 0;
                }
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
                In.A = (unsigned char)dest_imm;
                In.B = (unsigned char)idx_reg;
                In.Tok = 1; In.Imm = IdxN;
                IdxMetaPush(E->Left);
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = dest_imm;
                return 1;
            }
        }
    }
    /* Global ops[i] = add / &add: store Function Value* into global fptr table. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Right != NULL && (E->Op == 0 || E->Op == TokenAssign)
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && E->Left->Left->LocalSlot < 0 && E->Left->Left->ParamSlot < 0
        && E->Left->Left->Ident != NULL
        && E->Left->Right != NULL
        && LinParser != NULL)
    {
        struct Value *GV = IrGlobalIdentSoft(LinParser, E->Left->Left->Ident);
        struct Value *TV = NULL;
        IrExpr *R = E->Right;
        int rd, idx_r, base_r, sc, off, addr, gi;
        int es = (int)sizeof(void *);
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray
            && GV->Typ->FromType != NULL
            && ((GV->Typ->FromType->Base == TypePointer
                 && GV->Typ->FromType->FromType != NULL
                 && GV->Typ->FromType->FromType->Base == TypeFunction)
                || GV->Typ->FromType->Base == TypeFunction))
        {
            if (R->Kind == IrExUnary && R->Op == TokenAmpersand && R->Left != NULL)
                R = R->Left;
            if (R->Kind == IrExIdent && R->Ident != NULL)
                TV = IrGlobalIdentSoft(LinParser, R->Ident);
            if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
            {
                rd = AllocReg(Next);
                if (rd < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)rd;
                In.A = In.B = 0; In.Tok = 5;
                In.Imm = (long long)(intptr_t)TV;
                if (!Emit(Code, Cap, N, In)) return 0;
                if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_r))
                    return 0;
                gi = GlobPush(E->Left->Left);
                if (gi < 0) return 0;
                base_r = AllocReg(Next);
                if (base_r < 0) return 0;
                In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base_r;
                In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                if (!Emit(Code, Cap, N, In)) return 0;
                sc = AllocReg(Next);
                if (sc < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                In.A = In.B = 0; In.Tok = 0; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                off = AllocReg(Next);
                if (off < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)off;
                In.A = (unsigned char)idx_r; In.B = (unsigned char)sc;
                In.Tok = TokenAsterisk; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)base_r; In.B = (unsigned char)off;
                In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                if (!Emit(Code, Cap, N, In)) return 0;
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
                In.A = (unsigned char)rd;
                In.B = (unsigned char)addr;
                In.Tok = 2; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = rd;
                return 1;
            }
        }
    }
    /* tab[i] = add / &add: store Function Value* into array-of-fptr slot. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Right != NULL && (E->Op == 0 || E->Op == TokenAssign)
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && E->Left->Left->LocalSlot >= 0
        && E->Left->Right != NULL)
    {
        int slot = E->Left->Left->LocalSlot;
        int is_fp = 0;
        if (CompileSlot != NULL && CompileSlot->SlotIsFptr[slot])
            is_fp = 1;
        else if (IrCallDepth > 0
                 && IrCallStack[IrCallDepth - 1]->LocalIsFptr[slot])
            is_fp = 1;
        if (is_fp)
        {
            struct Value *TV = NULL;
            IrExpr *R = E->Right;
            int rd, idx_r, base_r, sc, off, addr;
            int es = (int)sizeof(void *);
            if (R->Kind == IrExUnary && R->Op == TokenAmpersand && R->Left != NULL)
                R = R->Left;
            if (R->Kind == IrExIdent && R->Ident != NULL && LinParser != NULL)
                TV = IrGlobalIdentSoft(LinParser, R->Ident);
            if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
            {
                rd = AllocReg(Next);
                if (rd < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)rd;
                In.A = In.B = 0; In.Tok = 5;
                In.Imm = (long long)(intptr_t)TV;
                if (!Emit(Code, Cap, N, In)) return 0;
                if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_r))
                    return 0;
                base_r = AllocReg(Next);
                if (base_r < 0) return 0;
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_r;
                In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
                if (!Emit(Code, Cap, N, In)) return 0;
                sc = AllocReg(Next);
                if (sc < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                In.A = In.B = 0; In.Tok = 0; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                off = AllocReg(Next);
                if (off < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)off;
                In.A = (unsigned char)idx_r; In.B = (unsigned char)sc;
                In.Tok = TokenAsterisk; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)base_r; In.B = (unsigned char)off;
                In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                if (!Emit(Code, Cap, N, In)) return 0;
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
                In.A = (unsigned char)rd;
                In.B = (unsigned char)addr;
                In.Tok = 2;
                In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = rd;
                return 1;
            }
        }
    }
    /* Local fptr assign: fp = add; fp = &add; fp = tab[i]; store Value*. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && E->Left->LocalSlot < IR_CALL_MAX_LOCALS)
    {
        int is_fp = 0;
        int slot = E->Left->LocalSlot;
        if (CompileSlot != NULL && CompileSlot->SlotIsFptr[slot])
            is_fp = 1;
        else if (IrCallDepth > 0
                 && IrCallStack[IrCallDepth - 1]->LocalIsFptr[slot])
            is_fp = 1;
        if (is_fp)
        {
            struct Value *TV = NULL;
            IrExpr *R = E->Right;
            int rd = -1;
            if (R->Kind == IrExUnary && R->Op == TokenAmpersand && R->Left != NULL)
                R = R->Left;
            if (R->Kind == IrExIdent && R->Ident != NULL && LinParser != NULL)
                TV = IrGlobalIdentSoft(LinParser, R->Ident);
            if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
            {
                rd = AllocReg(Next);
                if (rd < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)rd;
                In.A = In.B = 0; In.Tok = 5;
                In.Imm = (long long)(intptr_t)TV;
                if (!Emit(Code, Cap, N, In)) return 0;
            }
            else if (R->Kind == IrExIndex
                     || (R->Kind == IrExIdent && R->LocalSlot >= 0)
                     || (R->Kind == IrExIdent && R->ParamSlot >= 0)
                     || R->Kind == IrExMember)
            {
                /* fp = tab[i] / fp = other_fp / fp = s.fn — load Value* bits. */
                if (!LowerRec(R, Code, Cap, N, Next, &rd) || rd < 0)
                    return 0;
            }
            if (rd >= 0)
            {
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
            In.Op = IrLinStoreLocal; In.Dest = 0;
                In.A = (unsigned char)rd; In.B = 0; In.Tok = 5;
                In.Imm = slot;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = rd;
                return 1;
            }
        }
    }
    /* f()->m / f()->next->m / (*pp)->m = v: pointer base + field store.
     * Same ST resolution as Member load; LowerRec(parent) yields T*. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Right != NULL && E->Left->Left != NULL && E->Left->Ident != NULL
        && E->Left->BitWidth <= 0
        && (E->Op == 0 || E->Op == TokenAssign
            || E->Op == TokenAddAssign || E->Op == TokenSubtractAssign
            || E->Op == TokenMultiplyAssign || E->Op == TokenDivideAssign
            || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign)
        && IrVmLinearFeaturesOn()
        && (E->Left->Left->Kind == IrExCall
            || E->Left->Left->Kind == IrExMember
            || E->Left->Left->Kind == IrExIdent  /* p->m = v local/param T* */
            || (E->Left->Left->Kind == IrExUnary
                && E->Left->Left->Op == TokenAsterisk)))
    {
        int pr, addr, rv, off = 0, es = 4, eb = TypeInt;
        enum LexToken bop = 0;
        struct ValueType *ST = NULL;
        IrExpr *Root = E->Left->Left;
        IrExpr *Chain[8];
        int nc = 0, ci, stars = 0;
        while (Root != NULL && Root->Kind == IrExMember && nc < 8)
        {
            Chain[nc++] = Root;
            Root = Root->Left;
        }
        while (Root != NULL && Root->Kind == IrExUnary && Root->Op == TokenAsterisk)
        {
            stars++;
            Root = Root->Left;
        }
        if (Root != NULL && Root->Kind == IrExCall)
        {
            if (Root->CachedFunc != NULL && Root->CachedFunc->Val != NULL)
                ST = Root->CachedFunc->Val->FuncDef.ReturnType;
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
            if (ST == NULL)
                ST = IrLowerCallReturnStruct(Root);
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        else if (stars == 0 && nc == 0 && Root != NULL && Root->Kind == IrExIdent
                 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* GUARD: dual-read */
            ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            if (ST != NULL && ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
            else if (LinCompileActive && CompileSlot != NULL
                     && CompileSlot->SlotIsPtr[Root->LocalSlot]
                     && CompileSlot->SlotFromType[Root->LocalSlot])
            {
                ST = CompileSlot->SlotFromType[Root->LocalSlot];
                if (ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
            else
                ST = NULL;
        }
        else if (stars >= 1 && Root != NULL && Root->Kind == IrExIdent
                 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            /* GUARD: dual-read FromType */
            ST = IrCompLocalFromTypeOrFrame(Root->LocalSlot);
            {
                int si;
                for (si = 0; si < stars && ST; si++)
                    if (ST->Base == TypePointer && ST->FromType) ST = ST->FromType;
                    else break;
                if (ST && ST->Base == TypePointer && ST->FromType)
                    ST = ST->FromType;
            }
        }
        for (ci = nc - 1; ci >= 0 && ST != NULL; ci--)
        {
            struct Value *CM = NULL;
            if (ST->Members == NULL) { ST = NULL; break; }
            if (!TableGet(ST->Members, Chain[ci]->Ident, &CM, NULL, NULL, NULL)
                || CM == NULL || CM->Typ == NULL)
            { ST = NULL; break; }
            ST = CM->Typ;
            if (ST->Base == TypePointer && ST->FromType)
                ST = ST->FromType;
        }
        if (ST != NULL && IrLowerMemberOffEs(ST, E->Left->Ident, &off, &es, &eb))
        {
        /* Bitfield members: pure Tok=2 StoreIndex writes the whole word and
         * clobbers sibling fields (p->a += n zeros p->b). Fall through to
         * Tok=1 + IrBitInsert path. BitWidth on expr is often 0 at compile. */
        {
            struct Value *BMV = NULL;
            if (ST->Members != NULL
                && TableGet(ST->Members, (char *)E->Left->Ident, &BMV, NULL, NULL, NULL)
                && BMV != NULL && BMV->BitWidth > 0)
                goto not_pure_member_assign;
        }
        /* (*p).m = (Dot): address is p. p->m / (*pp)->m: address is pointer val. */
        if (E->Left->Left->Kind == IrExUnary && E->Left->Left->Op == TokenAsterisk
            && E->Left->Op == TokenDot && E->Left->Left->Left != NULL)
        {
            if (!LowerRec(E->Left->Left->Left, Code, Cap, N, Next, &pr)) return 0;
        }
        else if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &pr)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        if (off != 0)
        {
            In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)pr; In.B = 0;
            In.Tok = 0; In.Imm = off;
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        else
            addr = pr;
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        if (bop != 0)
        {
            int oldv, tmp;
            oldv = AllocReg(Next);
            if (oldv < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)oldv;
            In.A = (unsigned char)addr; In.B = 0;
            In.Tok = 2; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rv)) return 0;
            tmp = AllocReg(Next);
            if (tmp < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)tmp;
            In.A = (unsigned char)oldv; In.B = (unsigned char)rv;
            In.Tok = (unsigned short)bop; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            rv = tmp;
        }
        else
        {
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rv)) return 0;
        }
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)rv; In.B = (unsigned char)addr;
        In.Tok = 2; In.Imm = es;
        return Emit(Code, Cap, N, In);
        not_pure_member_assign: ; /* bitfield or unresolved */
        } /* ST resolved — else fall through to Tok=1 Member store */
    }
    /* Pure store: v[i].in.a = rhs — nested Member through Index of local aos. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Right != NULL && (E->Op == 0 || E->Op == TokenAssign)
        && E->Left->Left != NULL && E->Left->Ident != NULL
        && E->Left->BitWidth <= 0)
    {
        IrExpr *Chain[8];
        int nm = 0;
        IrExpr *p = E->Left;
        IrExpr *idxE = NULL, *idE = NULL;
        while (p != NULL && p->Kind == IrExMember && nm < 8)
        {
            Chain[nm++] = p;
            p = p->Left;
        }
        {
            /* Multidim grid[i][j].m: walk Index chain (H10). */
            IrExpr *IdxChain[IR_MAX_ARRAY_DIMS];
            int ni = 0, dix;
            int dims_loc[IR_MAX_ARRAY_DIMS];
            int nd_loc = 0;
            while (p != NULL && p->Kind == IrExIndex && ni < IR_MAX_ARRAY_DIMS)
            {
                IdxChain[ni++] = p;
                p = p->Left;
            }
            if (p != NULL && p->Kind == IrExIdent
                && ((p->LocalSlot >= 0 && p->LocalSlot < IR_CALL_MAX_LOCALS)
                    || (p->LocalSlot < 0 && p->ParamSlot < 0 && p->Ident != NULL)))
                idE = p;
            if (ni >= 1)
                idxE = IdxChain[0]; /* innermost keeps 1D fast path below */
            /* Outer→inner for row-major flatten */
            for (dix = 0; dix < ni / 2; dix++)
            {
                IrExpr *tmpx = IdxChain[dix];
                IdxChain[dix] = IdxChain[ni - 1 - dix];
                IdxChain[ni - 1 - dix] = tmpx;
            }
        if (idE != NULL && ni >= 1 && nm >= 1 && idxE != NULL && idxE->Right != NULL)
        {
            struct ValueType *ST = NULL;
            int elem_sz = 0, total_off = 0, es = 4, eb = TypeInt;
            int idx_reg = 0, base_reg = 0, sc_reg = 0, off_reg = 0, addr = 0, rv = 0;
            int ci, ok = 1;
            int is_global = (idE->LocalSlot < 0 && idE->Ident != NULL);
            if (!is_global && idE->LocalSlot >= 0 && idE->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                /* GUARD: dual-read */
                ST = IrCompLocalFromTypeOrFrame(idE->LocalSlot);
            }
            else if (is_global && LinParser != NULL)
            {
                struct Value *GV = IrGlobalIdentSoft(LinParser, idE->Ident);
                if (GV != NULL && GV->Typ != NULL)
                    ST = GV->Typ;
            }
            {
                struct ValueType *Twalk = ST;
                if (!is_global && idE->LocalSlot >= 0)
                {
                    /* GUARD: dual-read */
                    Twalk = IrCompLocalFromTypeOrFrame(idE->LocalSlot);
                }
                while (Twalk != NULL && Twalk->Base == TypeArray && nd_loc < IR_MAX_ARRAY_DIMS)
                {
                    dims_loc[nd_loc++] = Twalk->ArraySize > 0 ? Twalk->ArraySize : 0;
                    Twalk = Twalk->FromType;
                }
                if (!is_global && nd_loc < 2 && idE->LocalSlot >= 0)
                {
                    /* GUARD: dimensions may live only in the active frame. */
                    int meta_nd = IrCompLocalNDimsOrFrame(idE->LocalSlot);
                    if (meta_nd >= 2)
                    {
                        nd_loc = meta_nd;
                        for (dix = 0; dix < nd_loc && dix < IR_MAX_ARRAY_DIMS; dix++)
                            dims_loc[dix] = IrCompLocalDimOrFrame(idE->LocalSlot, dix);
                    }
                }
                if (Twalk != NULL && (Twalk->Base == TypeStruct || Twalk->Base == TypeUnion))
                    ST = Twalk;
            }
            ST = IrTypePeelArray(ST);
            if (ST != NULL && (ST->Base == TypeStruct || ST->Base == TypeUnion)
                && ST->Sizeof > 0)
                elem_sz = (int)ST->Sizeof;
            if (elem_sz <= 0)
            {
                /* GUARD: dual-read leaf element size */
                elem_sz = IrCompLocalLeafEsOrFrame(idE->LocalSlot);
            }
            if (ST != NULL && elem_sz > 0)
            {
                struct ValueType *Cur = ST;
                for (ci = nm - 1; ci >= 0 && Cur != NULL; ci--)
                {
                    int off = 0, mes = 4, meb = TypeInt;
                    struct Value *CM = NULL;
                    if (Cur->Members == NULL
                        || !TableGet(Cur->Members, Chain[ci]->Ident, &CM,
                                     NULL, NULL, NULL)
                        || CM == NULL || CM->Val == NULL)
                    { ok = 0; break; }
                    if (CM->BitWidth > 0)
                    { ok = 0; break; }
                    if (!IrLowerMemberOffEs(Cur, Chain[ci]->Ident, &off, &mes, &meb))
                    { ok = 0; break; }
                    total_off += off;
                    if (ci == 0)
                    { es = mes; eb = meb; }
                    else
                    {
                        if (CM->Typ == NULL)
                        { ok = 0; break; }
                        Cur = CM->Typ;
                        if (Cur->Base == TypeArray && Cur->FromType)
                            Cur = Cur->FromType;
                    }
                }
            }
            else
                ok = 0;
            if (ok && elem_sz > 0)
            {
                if (!LowerRec(E->Right, Code, Cap, N, Next, &rv)) return 0;
                base_reg = AllocReg(Next);
                if (base_reg < 0) return 0;
                if (is_global)
                {
                    int gi = GlobPush(idE);
                    if (gi < 0) return 0;
                    In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)base_reg;
                    In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
                }
                else
                {
                    In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = idE->LocalSlot;
                }
                if (!Emit(Code, Cap, N, In)) return 0;
                if (ni >= 2)
                {
                    int flat_reg = 0, d2, tmp_reg, dim_reg;
                    int idx_regs[IR_MAX_ARRAY_DIMS];
                    IrLinInst Bin;
                    for (d2 = 0; d2 < ni; d2++)
                    {
                        if (IdxChain[d2] == NULL || IdxChain[d2]->Right == NULL)
                            return 0;
                        if (!LowerRec(IdxChain[d2]->Right, Code, Cap, N, Next, &idx_regs[d2]))
                            return 0;
                    }
                    flat_reg = idx_regs[0];
                    for (d2 = 1; d2 < ni; d2++)
                    {
                        int stride = (d2 < nd_loc && dims_loc[d2] > 0) ? dims_loc[d2] : 1;
                        dim_reg = AllocReg(Next);
                        if (dim_reg < 0) return 0;
                        Bin.Op = IrLinImm; Bin.Dest = (unsigned char)dim_reg;
                        Bin.A = Bin.B = 0; Bin.Tok = 0; Bin.Imm = stride;
                        if (!Emit(Code, Cap, N, Bin)) return 0;
                        tmp_reg = AllocReg(Next);
                        if (tmp_reg < 0) return 0;
                        Bin.Op = IrLinBin; Bin.Dest = (unsigned char)tmp_reg;
                        Bin.A = (unsigned char)flat_reg; Bin.B = (unsigned char)dim_reg;
                        Bin.Tok = TokenAsterisk; Bin.Imm = 0;
                        if (!Emit(Code, Cap, N, Bin)) return 0;
                        flat_reg = AllocReg(Next);
                        if (flat_reg < 0) return 0;
                        Bin.Op = IrLinBin; Bin.Dest = (unsigned char)flat_reg;
                        Bin.A = (unsigned char)tmp_reg; Bin.B = (unsigned char)idx_regs[d2];
                        Bin.Tok = TokenPlus; Bin.Imm = 0;
                        if (!Emit(Code, Cap, N, Bin)) return 0;
                    }
                    sc_reg = AllocReg(Next);
                    if (sc_reg < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)sc_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = elem_sz;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    off_reg = AllocReg(Next);
                    if (off_reg < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
                    In.A = (unsigned char)flat_reg; In.B = (unsigned char)sc_reg;
                    In.Tok = TokenAsterisk; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)base_reg; In.B = (unsigned char)off_reg;
                    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr;
                }
                else if (idxE->Right->Kind == IrExConst)
                {
                    long long ix = idxE->Right->ConstVal;
                    if (ix != 0)
                    {
                        addr = AllocReg(Next);
                        if (addr < 0) return 0;
                        In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                        In.A = (unsigned char)base_reg; In.B = 0;
                        In.Tok = 0; In.Imm = (long long)ix * elem_sz;
                        if (!Emit(Code, Cap, N, In)) return 0;
                        base_reg = addr;
                    }
                }
                else
                {
                    if (!LowerRec(idxE->Right, Code, Cap, N, Next, &idx_reg))
                        return 0;
                    sc_reg = AllocReg(Next);
                    if (sc_reg < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)sc_reg;
                    In.A = In.B = 0; In.Tok = 0; In.Imm = elem_sz;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    off_reg = AllocReg(Next);
                    if (off_reg < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)off_reg;
                    In.A = (unsigned char)idx_reg; In.B = (unsigned char)sc_reg;
                    In.Tok = TokenAsterisk; In.Imm = 0;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)base_reg; In.B = (unsigned char)off_reg;
                    In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr;
                }
                if (total_off != 0)
                {
                    addr = AllocReg(Next);
                    if (addr < 0) return 0;
                    In.Op = IrLinAddI; In.Dest = (unsigned char)addr;
                    In.A = (unsigned char)base_reg; In.B = 0;
                    In.Tok = 0; In.Imm = total_off;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    base_reg = addr;
                }
                /* Compact VM needs the original indexed-member AST to check
                 * every dimension before committing the store. Keep the
                 * established Tok=2 contract for Linear, whose executor has
                 * no Tok=8 metadata channel. */
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex;
                In.A = (unsigned char)rv; In.B = (unsigned char)base_reg;
                In.Imm = es;
                {
                    const char *compact_env = getenv("BABYC_BYTECODE_VM");
                    const char *path_env = getenv("BABYC_PATH");
                    int compact_vm = (compact_env != NULL
                        && compact_env[0] == '1' && compact_env[1] == '\0')
                        || (path_env != NULL && strcmp(path_env, "bytecode") == 0);
                    if (compact_vm)
                    {
                        if (IdxN >= 32) return 0;
                        In.Dest = (unsigned char)IdxN;
                        In.Tok = 8;
                        IdxMetaPush(E->Left);
                    }
                    else
                    {
                        In.Dest = 0;
                        In.Tok = 2;
                    }
                }
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = rv;
                return 1;
            }
        }
        } /* multidim IdxChain block */
    }
    /* Member store: s.field = rhs (local or param by-value struct). */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExMember
        && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign
            || E->Op == TokenAddAssign || E->Op == TokenSubtractAssign
            || E->Op == TokenMultiplyAssign || E->Op == TokenDivideAssign
            || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign)
        && E->Left->Left != NULL
        && (E->Left->Left->Kind == IrExIdent
            || (E->Left->Left->Kind == IrExMember && IrVmLinearFeaturesOn())  /* o.in.x = v */
            || (E->Left->Left->Kind == IrExIndex && E->Left->Left->Left
                && E->Left->Left->Right != NULL
                && (E->Left->Left->Left->Kind == IrExIdent
                    || E->Left->Left->Left->Kind == IrExIndex
                    || (IrVmLinearFeaturesOn() && E->Left->Left->Left->Kind == IrExMember))
                && (E->Left->Left->Left->Kind != IrExIdent
                    || E->Left->Left->Left->LocalSlot >= 0
                    || (E->Left->Left->Left->LocalSlot < 0
                        && E->Left->Left->Left->ParamSlot < 0
                        && E->Left->Left->Left->Ident != NULL))))
        && E->Left->Ident != NULL)
    {
        enum LexToken bop = 0;
        int mem_idx_reg = 0;
        /* Variable index on aos: lower index into B for StoreIndex Tok=1. */
        if (E->Left->Left->Kind == IrExIndex
            && E->Left->Left->Right != NULL
            && E->Left->Left->Right->Kind != IrExConst)
        {
            if (!LowerRec(E->Left->Left->Right, Code, Cap, N, Next, &mem_idx_reg))
                return 0;
        }
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        if (bop != 0)
        {
            int base, rhs, out;
            if (IdxN >= 31) return 0;
            base = AllocReg(Next);
            if (base < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)base;
            In.A = In.B = 0; In.Tok = 1; In.Imm = IdxN;
            IdxMetaPush(E->Left);
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs)) return 0;
            out = AllocReg(Next);
            if (out < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)out;
            In.A = (unsigned char)base; In.B = (unsigned char)rhs;
            In.Tok = bop; In.Imm = 0;
            if (E->CanonicalOpFlags & IR_CF_UNSIGNED)
                In.Imm |= IR_LIN_F_UNSIGNED;
            if (E->CanonicalOpFlags & IR_CF_WIDE)
                In.Imm |= IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = out;
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)*Dest; In.B = (unsigned char)mem_idx_reg;
            In.Tok = 1; In.Imm = IdxN;
            IdxMetaPush(E->Left);
            return Emit(Code, Cap, N, In);
        }
        if (IdxN >= 32) return 0;
        /* Function designator → fptr member: linear LoadGlobal is not Value*. */
        if (E->Right->Kind == IrExIdent && E->Right->LocalSlot < 0
            && E->Right->ParamSlot < 0 && E->Right->Ident != NULL
            && LinParser != NULL)
        {
            struct Value *TV = NULL;
            IrVarTableGet(LinParser, E->Right->Ident, &TV);
            if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
                return 0;
        }
        if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)*Dest; In.B = (unsigned char)mem_idx_reg;
        In.Tok = 1; In.Imm = IdxN;
        IdxMetaPush(E->Left);
        return Emit(Code, Cap, N, In);
    }
    /* *p / *o.ip / *(p+i) store: Ident, Member, BinOp, Unary, Call, Index base. */
    if (E->Kind == IrExAssign && E->Left != NULL
        && E->Left->Kind == IrExUnary && E->Left->Op == TokenAsterisk
        && E->Right != NULL
        && E->Left->Left != NULL
        && (E->Op == 0 || E->Op == TokenAssign
            || E->Op == TokenAddAssign || E->Op == TokenSubtractAssign
            || E->Op == TokenMultiplyAssign || E->Op == TokenDivideAssign
            || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign)
        && (E->Left->Left->Kind == IrExIdent
            || E->Left->Left->Kind == IrExMember
            || E->Left->Left->Kind == IrExBinOp
            || E->Left->Left->Kind == IrExUnary
            || E->Left->Left->Kind == IrExCall
            || E->Left->Left->Kind == IrExIndex))
    {
        enum LexToken bop = 0;
        IrExpr *Pbase = E->Left->Left;
        /* Prefer pure Ident local path for compound (IdxMeta). */
                if (IrExecIsVmProd())
        {
            IrExpr *t = Pbase;
            int nest = 0;
            while (t != NULL && t->Kind == IrExUnary && t->Op == TokenAsterisk)
            {
                nest++;
                t = t->Left;
            }
            /* **f() store: Pbase may be *Call; nest+Call OK with typed es. */
            if (t != NULL && t->Kind == IrExUnary && t->Op == TokenAmpersand)
                return 0;
        }
int pure_local = (Pbase->Kind == IrExIdent && Pbase->LocalSlot >= 0);
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        if (bop != 0 && pure_local)
        {
            int base, rhs, out;
            if (IdxN >= 31) return 0;
            base = AllocReg(Next);
            if (base < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)base;
            In.A = In.B = 0; In.Tok = 0; In.Imm = IdxN;
            IdxMetaPush(E->Left);
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs)) return 0;
            out = AllocReg(Next);
            if (out < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)out;
            In.A = (unsigned char)base; In.B = (unsigned char)rhs;
            In.Tok = bop; In.Imm = 0;
            if (E->CanonicalOpFlags & IR_CF_UNSIGNED)
                In.Imm |= IR_LIN_F_UNSIGNED;
            if (E->CanonicalOpFlags & IR_CF_WIDE)
                In.Imm |= IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = out;
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)*Dest; In.B = 0; In.Tok = 0; In.Imm = IdxN;
            IdxMetaPush(E->Left);
            return Emit(Code, Cap, N, In);
        }
        {
            int pr, es, es_typed = 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
            if (!LowerRec(Pbase, Code, Cap, N, Next, &pr)) return 0;
            /* Pointee size: never use array-of-ptr element width (void*). */
            es = E->Left->ElemSize > 0 ? E->Left->ElemSize : 0;
            {
                IrExpr *Base = Pbase;
                while (Base != NULL && Base->Kind == IrExIndex)
                    Base = Base->Left;
                while (Base != NULL && Base->Kind == IrExBinOp
                       && (Base->Op == TokenPlus || Base->Op == TokenMinus)
                       && Base->Left != NULL)
                {
                    if (Base->Left->Kind == IrExIdent
                        || (Base->Left->TypeFlags & IR_TF_PTR)
                        || Base->Left->ElemSize > 0)
                        Base = Base->Left;
                    else if (Base->Right != NULL
                             && (Base->Right->Kind == IrExIdent
                                 || (Base->Right->TypeFlags & IR_TF_PTR)))
                        Base = Base->Right;
                    else
                        break;
                }
                if (es <= 0 && Base != NULL && Base->Kind == IrExIdent
                    && IrCallDepth > 0 && Base->LocalSlot >= 0
                    && Base->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    int slot = Base->LocalSlot;
                    int ps = IrCompLocalPointeeSizeOrFrame(slot);
                    if (ps > 0)
                        es = ps;
                    else if (Fr->LocalArraySize[slot] > 0
                        && Fr->LocalElemSize[slot] == (int)sizeof(void *))
                        es = 4;
                    else if (Fr->LocalElemSize[slot] > 0
                             && Fr->LocalArraySize[slot] <= 0)
                        es = Fr->LocalElemSize[slot];
                }
                if (es <= 0 && Pbase->Kind == IrExIndex
                    && ((Pbase->TypeFlags & IR_TF_PTR)
                        || Pbase->ElemSize == (int)sizeof(void *)))
                    es = 4;
                if (es <= 0 && Pbase->Kind != IrExIndex && Pbase->ElemSize > 0
                    && Pbase->ElemSize != (int)sizeof(void *))
                    es = Pbase->ElemSize;
                if (es <= 0 && Base != NULL && Base->Kind == IrExIdent
                    && ((Base->TypeFlags & IR_TF_PTR) || Base->ElemSize <= 0)
                    && Pbase->Kind != IrExIndex)
                    es = 1;
                /* Pointee size from formal/local pointer type (char **out). */
                if (Base != NULL && Base->Kind == IrExIdent)
                {
                    struct ValueType *PT = NULL;
                    if (Base->ParamSlot >= 0 && Base->ParamSlot < IR_CALL_MAX_PARAMS)
                    {
                        if (IrCallDepth > 0
                            && Base->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
                            PT = IrCallStack[IrCallDepth - 1]->ParamTyp[Base->ParamSlot];
                        if (PT == NULL && CompileSlot != NULL
                            && CompileSlot->Func != NULL
                            && CompileSlot->Func->Val != NULL
                            && CompileSlot->Func->Val->FuncDef.ParamType != NULL
                            && Base->ParamSlot < CompileSlot->Func->Val->FuncDef.NumParams)
                            PT = CompileSlot->Func->Val->FuncDef.ParamType[Base->ParamSlot];
                    }
                    else if (Base->LocalSlot >= 0
                             && Base->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        if (CompLocalFromType[Base->LocalSlot] != NULL)
                            PT = CompLocalFromType[Base->LocalSlot];
                        else if (IrCallDepth > 0)
                            PT = IrCallStack[IrCallDepth - 1]->LocalFromType[Base->LocalSlot];
                    }
                    if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL)
                    {
                        struct ValueType *Pointee = PT->FromType;
                        if (Pointee->Base == TypePointer
                            || Pointee->Base == TypeFunction
                            || Pointee->Base == TypeArray)
                        { es = (int)sizeof(void *); es_typed = 1; }
                        else if (Pointee->Sizeof > 0
                                 && Pointee->Base != TypeVoid)
                        { es = Pointee->Sizeof; es_typed = 1; }
                    }
                }
            }
            /* *out = z when out is T** / RHS is pointer: full pointer word.
             * Only a fallback when the declared pointee type of the base is
             * unknown: the RHS-slot heuristics read compile-time slot state
             * that may belong to another frame (char t in a callee saw the
             * caller's array slot 0 → *b = t stored 8 bytes, 903). */
            if (es_typed)
                ;
            else if ((E->Left->TypeFlags & IR_TF_PTR)
                || (E->Right != NULL && (E->Right->TypeFlags & IR_TF_PTR))
                || (E->Left->ElemSize == (int)sizeof(void *)))
                es = (int)sizeof(void *);
            else if (E->Right != NULL && E->Right->Kind == IrExIdent)
            {
                int rs = E->Right->LocalSlot;
                if (rs >= 0 && rs < IR_CALL_MAX_LOCALS)
                {
                    if (CompileSlot != NULL && CompileSlot->SlotIsPtr[rs])
                        es = (int)sizeof(void *);
                    else if (CompLocalFromType[rs] != NULL
                             && CompLocalFromType[rs]->Base == TypePointer)
                        es = (int)sizeof(void *);
                    else if (IrCallDepth > 0
                             && IrCallStack[IrCallDepth - 1]->LocalIsPtr[rs])
                        es = (int)sizeof(void *);
                }
            }
            if (es != 1 && es != 2 && es != 4 && es != 8) es = 4;
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)*Dest; /* value */
            In.B = (unsigned char)pr;    /* pointer reg */
            In.Tok = 2;
            In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
    }
    /* a[i][j] = v on local array-of-pointers.
     * Strict: ElemSize==sizeof(void*) when TF_PTR absent (675 Body-IR). */
    /* s.arr[i] = v / aos[k].arr[i] = v : Index of array member.
     * Member base: Ident, nested Member, or Index-of-Ident (array-of-struct). */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExMember
        && E->Left->Left->Left != NULL && E->Left->Left->Ident != NULL
        && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && (E->Left->Left->Left->Kind == IrExIdent
            || E->Left->Left->Left->Kind == IrExMember
            || (E->Left->Left->Left->Kind == IrExIndex
                && E->Left->Left->Left->Left != NULL
                && E->Left->Left->Left->Left->Kind == IrExIdent
                && E->Left->Left->Left->Right != NULL))
        && (E->Left->Left->Left->Kind != IrExIdent
            || E->Left->Left->Left->LocalSlot >= 0
            || (E->Left->Left->Left->LocalSlot < 0
                && E->Left->Left->Left->ParamSlot < 0
                && E->Left->Left->Left->Ident != NULL)))
    {
        int idx_reg, es = 0, base_reg, sc, off, addr;
        struct ValueType *ArrayType = LinExprValueType(E->Left->Left, 0);
        /* Resolve the indexed object's declared type from the whole expression
         * (not just its root identifier). This handles nested aggregate paths
         * such as n.item.values[i], while keeping the same metadata-backed
         * member-array base and runtime bounds-check path below. */
        if (ArrayType != NULL
            && (ArrayType->Base == TypeArray || ArrayType->Base == TypePointer)
            && ArrayType->FromType != NULL)
            es = LinTypeObjectSize(ArrayType->FromType);
        if (es <= 0 && E->Left->ElemSize > 0 && E->Left->ElemSize <= 8)
            es = E->Left->ElemSize;
        if (es != 1 && es != 2 && es != 4 && es != 8)
            es = 4;
        /* For a fixed array reached only through member expressions, preserve
         * index bounds and aggregate-init bookkeeping in the shared member-
         * array executor instead of reducing the lvalue to an unchecked raw
         * pointer store. Dynamic array-of-struct bases retain the Tok=5 path. */
        if (ArrayType != NULL && ArrayType->Base == TypeArray
            && ArrayType->ArraySize > 0
            && E->Left->Left->Left != NULL
            && E->Left->Left->Left->Kind != IrExIndex)
        {
            int mi;
            if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_reg)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
            if (IdxN >= 31) return 0;
            IdxMetaPush(E->Left);
            mi = IdxN - 1;
            if (mi < 0 || mi >= 32) return 0;
            IdxMetaSlot[mi] = -1;
            IdxMetaKind[mi] = 5;
            IdxMetaEs[mi] = es;
            IdxMetaN[mi] = (int)ArrayType->ArraySize;
            In.Op = IrLinStoreMemberArray; In.Dest = 0;
            In.A = (unsigned char)*Dest; In.B = (unsigned char)idx_reg;
            In.Tok = 0;
            In.Imm = mi;
            return Emit(Code, Cap, N, In);
        }
        if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx_reg)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
        if (IdxN >= 31) return 0;
        /* Outer aos[var].arr: pass variable index in A for Tok=5 Step. */
        {
            IrExpr *MB = E->Left->Left->Left;
            int outer_reg = 0;
            if (MB != NULL && MB->Kind == IrExIndex && MB->Right != NULL
                && MB->Right->Kind != IrExConst)
            {
                if (!LowerRec(MB->Right, Code, Cap, N, Next, &outer_reg))
                    return 0;
            }
            base_reg = AllocReg(Next);
            if (base_reg < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)base_reg;
            In.A = (unsigned char)outer_reg; In.B = 0;
            In.Tok = 5; /* member array base address */
            In.Imm = IdxN;
            IdxMetaPush(E->Left->Left);
            if (!Emit(Code, Cap, N, In)) return 0;
        }
        sc = AllocReg(Next);
        if (sc < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
        In.A = In.B = 0; In.Tok = 0; In.Imm = es;
        if (!Emit(Code, Cap, N, In)) return 0;
        off = AllocReg(Next);
        if (off < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)off;
        In.A = (unsigned char)idx_reg; In.B = (unsigned char)sc;
        In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)addr;
        In.A = (unsigned char)base_reg; In.B = (unsigned char)off;
        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)*Dest;
        In.B = (unsigned char)addr;
        In.Tok = 2; /* *addr = val, es in Imm */
        In.Imm = es;
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIndex
        && E->Left->Left->Left != NULL && E->Left->Left->Left->Kind == IrExIdent
        && E->Left->Left->Left->LocalSlot >= 0
        && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && IrLowerIndexRowIsPointer(E->Left->Left))
    {
        int pr, rd, es, sc, off, addr;
        if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &pr)) return 0;
        if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &rd)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
        /* Pointee size — never sizeof(void*) from inner Index element. */
        es = E->Left->ElemSize > 0
             && E->Left->ElemSize != (int)sizeof(void *) ? E->Left->ElemSize : 0;
        if (es <= 0 && IrCallDepth > 0
            && E->Left->Left->Left->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            int slot = E->Left->Left->Left->LocalSlot;
            {
                int ps = IrCompLocalPointeeSizeOrFrame(slot);
                if (ps > 0) es = ps;
            }
        }
        if (es <= 0) es = 4;
        if (es != 1 && es != 2 && es != 4 && es != 8) es = 4;
        sc = AllocReg(Next);
        if (sc < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
        In.A = In.B = 0; In.Tok = 0; In.Imm = es;
        if (!Emit(Code, Cap, N, In)) return 0;
        off = AllocReg(Next);
        if (off < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)off;
        In.A = (unsigned char)rd; In.B = (unsigned char)sc;
        In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)addr;
        In.A = (unsigned char)pr; In.B = (unsigned char)off;
        In.Tok = TokenPlus; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)*Dest;
        In.B = (unsigned char)addr;
        In.Tok = 2;
        In.Imm = es;
        return Emit(Code, Cap, N, In);
    }
    /* Struct-by-value: aos[i] = s  or  s = aos[i] / s = t (owned blobs).
     * Dest Index of local/global aos, src Ident owned struct (or Index aos).
     * Emit: src_addr, dest_addr, IrLinStoreIndex Tok=3 Imm=nbytes (memcpy). */
    if (E->Kind == IrExAssign && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && E->Left != NULL
        && (E->Left->Kind == IrExIndex || E->Left->Kind == IrExIdent)
        && (E->Right->Kind == IrExIdent || E->Right->Kind == IrExIndex
            || E->Right->Kind == IrExCall))
    {
        IrExpr *LX = E->Left;
        IrExpr *RX = E->Right;
        IrExpr *LRoot = LX;
        IrExpr *RRoot = RX;
        int nbytes = 0;
        int src_reg = -1, dst_reg = -1, idx_reg = 0;
        int is_aos_dest = 0, is_aos_src = 0;
        int is_owned_src = 0, is_owned_dest = 0;
        int ldepth = 0, rdepth = 0;

        while (LRoot != NULL && LRoot->Kind == IrExIndex)
        {
            ldepth++;
            LRoot = LRoot->Left;
        }
        while (RRoot != NULL && RRoot->Kind == IrExIndex)
        {
            rdepth++;
            RRoot = RRoot->Left;
        }
        /* A nested Index's ElemSize describes its row/subarray, not the
         * scalar/aggregate selected by the complete index chain. */
        if (LRoot == NULL || LRoot->Kind != IrExIdent) goto not_struct_assign;
        /* T* / T** locals: value copy of pointer bits, NEVER struct-blob memcpy.
         * CompLocalFromType for Node *p is often the pointee Node; without this
         * gate, p=q emits Imm Tok=4 (&LocalVal) and assigns the cell address. */
        if (LX->Kind == IrExIdent && LX->LocalSlot >= 0
            && LX->LocalSlot < IR_CALL_MAX_LOCALS
            && ((CompLocalFromType[LX->LocalSlot] != NULL
                    && CompLocalFromType[LX->LocalSlot]->Base == TypePointer)
                || (CompileSlot != NULL && CompileSlot->SlotIsPtr[LX->LocalSlot])
                || (IrCallDepth > 0
                    && IrCallStack[IrCallDepth - 1]->LocalIsPtr[LX->LocalSlot])))
            goto not_struct_assign;
        if (RX->Kind == IrExIdent && RX->LocalSlot >= 0
            && RX->LocalSlot < IR_CALL_MAX_LOCALS
            && ((CompLocalFromType[RX->LocalSlot] != NULL
                    && CompLocalFromType[RX->LocalSlot]->Base == TypePointer)
                || (CompileSlot != NULL && CompileSlot->SlotIsPtr[RX->LocalSlot])
                || (IrCallDepth > 0
                    && IrCallStack[IrCallDepth - 1]->LocalIsPtr[RX->LocalSlot])))
            goto not_struct_assign;
        /* FP array element (float a[i]=i): never struct-memcpy (956). */
        if ((LX->TypeFlags & IR_TF_FP)
            || (LRoot->LocalSlot >= 0 && LRoot->LocalSlot < IR_CALL_MAX_LOCALS
                && CompLocalFromType[LRoot->LocalSlot] != NULL
                && CompLocalFromType[LRoot->LocalSlot]->Base == TypeFP))
            goto not_struct_assign;
        /* A struct-returning call evaluates to the address of its stable
         * return blob in the linear VM.  When assigning that value to an
         * array element, copy the aggregate bytes instead of storing the
         * blob address as though it were a scalar/pointer. */
        if (RX->Kind != IrExCall
            && (RRoot == NULL || RRoot->Kind != IrExIdent))
            goto not_struct_assign;

        /* Determine sizes from ElemSize / LocalElemSize / DeclFromType / global type */
        if (LX->Kind == IrExIndex && ldepth == 1 && LX->ElemSize > 4)
            nbytes = LX->ElemSize;
        if (nbytes <= 4 && RX->Kind == IrExIndex && rdepth == 1 && RX->ElemSize > 4)
            nbytes = RX->ElemSize;
        if (nbytes <= 4 && IrCallDepth > 0)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            if (LX->Kind == IrExIndex && ldepth == 1 && LRoot->LocalSlot >= 0
                && LRoot->LocalSlot < IR_CALL_MAX_LOCALS
                && Fr->LocalElemSize[LRoot->LocalSlot] > 4)
                nbytes = (int)Fr->LocalElemSize[LRoot->LocalSlot];
            if (nbytes <= 4 && RX->Kind == IrExIdent && RX->LocalSlot >= 0
                && RX->LocalSlot < IR_CALL_MAX_LOCALS
                && Fr->LocalOwned[RX->LocalSlot]
                && Fr->LocalFromType[RX->LocalSlot] != NULL
                && Fr->LocalFromType[RX->LocalSlot]->Sizeof > 4)
                nbytes = (int)Fr->LocalFromType[RX->LocalSlot]->Sizeof;
            if (nbytes <= 4 && LX->Kind == IrExIdent && LX->LocalSlot >= 0
                && LX->LocalSlot < IR_CALL_MAX_LOCALS
                && Fr->LocalOwned[LX->LocalSlot]
                && Fr->LocalFromType[LX->LocalSlot] != NULL
                && Fr->LocalFromType[LX->LocalSlot]->Sizeof > 4)
                nbytes = (int)Fr->LocalFromType[LX->LocalSlot]->Sizeof;
        }
        /* Multidimensional local array: only select the aggregate-copy path
         * when the complete index chain ends at a struct/union leaf.  A row
         * selection is an array, not a struct, and scalar 3D/4D assignments
         * must stay on the ordinary indexed path. */
        if (nbytes <= 4 && IrCallDepth > 0)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            if (LX->Kind == IrExIndex && ldepth > 1 && LRoot->LocalSlot >= 0
                && LRoot->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                if (IrCompLocalNDimsOrFrame(LRoot->LocalSlot) == ldepth
                    && IrCompLocalLeafEsOrFrame(LRoot->LocalSlot) > 4)
                    nbytes = IrCompLocalLeafEsOrFrame(LRoot->LocalSlot);
                struct ValueType *AT = Fr->LocalFromType[LRoot->LocalSlot];
                int d;
                if (AT == NULL) AT = CompLocalFromType[LRoot->LocalSlot];
                for (d = 0; AT != NULL && d < ldepth; ++d)
                {
                    if (AT->Base != TypeArray) { AT = NULL; break; }
                    AT = AT->FromType;
                }
                if (AT != NULL && (AT->Base == TypeStruct || AT->Base == TypeUnion)
                    && AT->Sizeof > 4) nbytes = (int)AT->Sizeof;
            }
            if (nbytes <= 4 && RX->Kind == IrExIndex && rdepth > 1 && RRoot->LocalSlot >= 0
                && RRoot->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                if (IrCompLocalNDimsOrFrame(RRoot->LocalSlot) == rdepth
                    && IrCompLocalLeafEsOrFrame(RRoot->LocalSlot) > 4)
                    nbytes = IrCompLocalLeafEsOrFrame(RRoot->LocalSlot);
                struct ValueType *AT = Fr->LocalFromType[RRoot->LocalSlot];
                int d;
                if (AT == NULL) AT = CompLocalFromType[RRoot->LocalSlot];
                for (d = 0; AT != NULL && d < rdepth; ++d)
                {
                    if (AT->Base != TypeArray) { AT = NULL; break; }
                    AT = AT->FromType;
                }
                if (AT != NULL && (AT->Base == TypeStruct || AT->Base == TypeUnion)
                    && AT->Sizeof > 4) nbytes = (int)AT->Sizeof;
            }
        }
        /* Global aos / global struct: Sizeof from GlobalTable type. */
        if (nbytes <= 4 && LinParser != NULL)
        {
            struct Value *GV = NULL;
            if (RX->Kind == IrExIndex && RRoot->LocalSlot < 0
                && RRoot->ParamSlot < 0 && RRoot->Ident != NULL)
            {
                struct ValueType *AT = NULL;
                GV = IrGlobalValSoft(LinParser, RRoot);
                if (GV != NULL) AT = GV->Typ;
                {
                    int d;
                    for (d = 0; AT != NULL && d < rdepth; d++)
                    {
                        if (AT->Base != TypeArray)
                        {
                            AT = NULL;
                            break;
                        }
                        AT = AT->FromType;
                    }
                }
                if (AT != NULL && (AT->Base == TypeStruct || AT->Base == TypeUnion)
                    && AT->Sizeof > 4)
                    nbytes = (int)AT->Sizeof;
            }
            if (nbytes <= 4 && LX->Kind == IrExIndex && ldepth == 1 && LRoot->LocalSlot < 0
                && LRoot->ParamSlot < 0 && LRoot->Ident != NULL)
            {
                struct ValueType *AT = NULL;
                GV = IrGlobalValSoft(LinParser, LRoot);
                if (GV != NULL) AT = GV->Typ;
                {
                    int d;
                    for (d = 0; AT != NULL && d < ldepth; d++)
                    {
                        if (AT->Base != TypeArray)
                        {
                            AT = NULL;
                            break;
                        }
                        AT = AT->FromType;
                    }
                }
                if (AT != NULL && (AT->Base == TypeStruct || AT->Base == TypeUnion)
                    && AT->Sizeof > 4)
                    nbytes = (int)AT->Sizeof;
            }
            if (nbytes <= 4 && RX->Kind == IrExIdent && RX->LocalSlot < 0
                && RX->ParamSlot < 0 && RX->Ident != NULL)
            {
                GV = IrGlobalValSoft(LinParser, RX);
                if (GV != NULL && GV->Typ != NULL
                    && (GV->Typ->Base == TypeStruct || GV->Typ->Base == TypeUnion)
                    && GV->Typ->Sizeof > 4)
                    nbytes = (int)GV->Typ->Sizeof;
            }
        }
        /* Compile-time aos leaf: struct S v[N]; CompLocalLeafEs = sizeof(S).
         * Runtime Fr->LocalElemSize is not yet set during FnCodeCompile (549). */
        if (nbytes <= 4 && LX->Kind == IrExIndex && ldepth == 1
            && LRoot->LocalSlot >= 0 && LRoot->LocalSlot < IR_CALL_MAX_LOCALS)
        {
            if (IrCompLocalLeafEsOrFrame(LRoot->LocalSlot) > 4)
                nbytes = IrCompLocalLeafEsOrFrame(LRoot->LocalSlot);
            else if (CompLocalFromType[LRoot->LocalSlot] != NULL
                && (CompLocalFromType[LRoot->LocalSlot]->Base == TypeStruct
                    || CompLocalFromType[LRoot->LocalSlot]->Base == TypeUnion)
                && CompLocalFromType[LRoot->LocalSlot]->Sizeof > 4)
                nbytes = (int)CompLocalFromType[LRoot->LocalSlot]->Sizeof;
        }
        if (nbytes <= 4 && RX->Kind == IrExIndex && rdepth == 1
            && RRoot != NULL && RRoot->LocalSlot >= 0
            && RRoot->LocalSlot < IR_CALL_MAX_LOCALS
            && IrCompLocalLeafEsOrFrame(RRoot->LocalSlot) > 4)
            nbytes = IrCompLocalLeafEsOrFrame(RRoot->LocalSlot);
        /* Call RHS: sizeof return type of struct-returning function. */
        if (nbytes <= 4 && RX->Kind == IrExCall && RX->Left != NULL
            && RX->Left->Kind == IrExIdent && RX->Left->Ident != NULL
            && LinParser != NULL)
        {
            struct Value *FV = NULL;
            IrVarTableGet(LinParser, RX->Left->Ident, &FV);
            if (FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction
                && FV->Val != NULL && FV->Val->FuncDef.ReturnType != NULL)
            {
                struct ValueType *RT = FV->Val->FuncDef.ReturnType;
                if ((RT->Base == TypeStruct || RT->Base == TypeUnion)
                    && RT->Sizeof > 4)
                    nbytes = (int)RT->Sizeof;
            }
        }
        /* point is often 8 bytes — allow es==8 when both sides are struct-ish */
        if (nbytes <= 4)
        {
            /* Heuristic: Ident RHS with no TF_PTR and LocalOwned → struct */
            if (IrCallDepth > 0 && RX->Kind == IrExIdent && RX->LocalSlot >= 0
                && RX->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (Fr->LocalOwned[RX->LocalSlot]
                    && !Fr->LocalIsPtr[RX->LocalSlot]
                    && Fr->LocalArraySize[RX->LocalSlot] <= 0
                    && Fr->LocalFromType[RX->LocalSlot] != NULL
                    && (Fr->LocalFromType[RX->LocalSlot]->Base == TypeStruct
                        || Fr->LocalFromType[RX->LocalSlot]->Base == TypeUnion))
                    nbytes = Fr->LocalFromType[RX->LocalSlot]->Sizeof > 0
                        ? (int)Fr->LocalFromType[RX->LocalSlot]->Sizeof : 0;
            }
        }
        if (nbytes <= 4
            && LX->Kind == IrExIndex && ldepth == 1 && RX->Kind == IrExIdent
            && RX->LocalSlot >= 0
            && !(LX->TypeFlags & IR_TF_PTR)
            && !(RX->TypeFlags & IR_TF_PTR)
            && LX->ElemSize >= 8)
            nbytes = LX->ElemSize;
        if (nbytes <= 4 || nbytes > 4096)
            goto not_struct_assign;

        /* Source address: Ident LocalVal is blob addr for owned struct.
         * At FnCodeCompile LocalOwned may be unset — still emit LoadLocal;
         * runtime memcpy no-ops if Src is NULL. */
        if (RX->Kind == IrExIdent && RX->LocalSlot >= 0)
        {
            src_reg = AllocReg(Next);
            if (src_reg < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)src_reg;
            In.A = In.B = 0; In.Tok = 4; In.Imm = RX->LocalSlot;
            if (!Emit(Code, Cap, N, In)) return 0;
            is_owned_src = 1;
        }
        else if (RX->Kind == IrExIndex && RRoot->LocalSlot >= 0)
        {
            if (!LowerLocalStructArrayAddress(RX, RRoot->LocalSlot, nbytes,
                                              Code, Cap, N, Next, &src_reg))
                goto not_struct_assign;
            is_aos_src = 1;
        }
        else if (RX->Kind == IrExIndex && RRoot->LocalSlot < 0
                 && RRoot->ParamSlot < 0 && RRoot->Ident != NULL
                 && LinParser != NULL)
        {
            if (!LowerGlobalStructArrayAddress(RX, RRoot, nbytes,
                                               Code, Cap, N, Next, &src_reg))
                goto not_struct_assign;
            is_aos_src = 1;
        }
        else if (RX->Kind == IrExIdent && RX->LocalSlot < 0
                 && RX->ParamSlot < 0 && RX->Ident != NULL
                 && LinParser != NULL)
        {
            /* Global struct/union Ident as source blob. */
            int gi;
            struct Value *GV = IrGlobalValSoft(LinParser, RX);
            if (GV == NULL || GV->Typ == NULL
                || (GV->Typ->Base != TypeStruct && GV->Typ->Base != TypeUnion))
                goto not_struct_assign;
            gi = GlobPush(RX);
            if (gi < 0) return 0;
            src_reg = AllocReg(Next);
            if (src_reg < 0) return 0;
            In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)src_reg;
            In.A = In.B = 0; In.Tok = 1; In.Imm = gi;
            if (!Emit(Code, Cap, N, In)) return 0;
            is_owned_src = 1;
        }
        else if (RX->Kind == IrExCall)
        {
            if (nbytes <= 4)
                goto not_struct_assign;
            if (nbytes > 512)
                return 0;
            if (!LowerRec(RX, Code, Cap, N, Next, &src_reg))
                return 0;
            is_owned_src = 1;
        }
        else
            goto not_struct_assign;

        /* Dest address */
        if (LX->Kind == IrExIdent && LX->LocalSlot >= 0)
        {
            dst_reg = AllocReg(Next);
            if (dst_reg < 0) return 0;
            /* Owned struct blob: Tok=4 → LocalVal payload addr (not scalar
             * Locals[] which trips uninit on bare auto struct). */
            In.Op = IrLinImm; In.Dest = (unsigned char)dst_reg;
            In.A = In.B = 0; In.Tok = 4; In.Imm = LX->LocalSlot;
            if (!Emit(Code, Cap, N, In)) return 0;
            is_owned_dest = 1;
        }
        else if (LX->Kind == IrExIndex && LRoot->LocalSlot >= 0)
        {
            if (!LowerLocalStructArrayAddress(LX, LRoot->LocalSlot, nbytes,
                                              Code, Cap, N, Next, &dst_reg))
                goto not_struct_assign;
            is_aos_dest = 1;
        }
        else if (LX->Kind == IrExIndex && LRoot->LocalSlot < 0
                 && LRoot->ParamSlot < 0 && LRoot->Ident != NULL
                 && LinParser != NULL)
        {
            if (!LowerGlobalStructArrayAddress(LX, LRoot, nbytes,
                                               Code, Cap, N, Next, &dst_reg))
                goto not_struct_assign;
            is_aos_dest = 1;
        }
        else
            goto not_struct_assign;

        /* Tok=3: memcpy(R[B], R[A], Imm) */
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)src_reg;
        In.B = (unsigned char)dst_reg;
        In.Tok = 3;
        In.Imm = nbytes;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = src_reg; /* assign expr value unused typically */
        if (is_owned_dest && IrCallDepth > 0 && LX->LocalSlot >= 0)
            IrLiveMarkInit(IrCallStack[IrCallDepth - 1], LX->LocalSlot);
        return 1;
    }
not_struct_assign:
    if (E->Kind == IrExAssign && E->Left != NULL && E->Right != NULL
        && E->Left->Kind == IrExIndex && E->Left->CanonicalType != NULL
        && IrExprDesignatesAggregateObject(E->Left))
        return LinLowerAggregateStore(E, Code, Cap, N, Next, Dest);
    /* f()[i] = v: Call returns T*, same scale+Tok=2 store as pointer Index. */
    if (IrVmLinearFeaturesOn() && E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Right != NULL && E->Left->Left != NULL && E->Left->Right != NULL
        && E->Left->Left->Kind == IrExCall
        && (E->Op == 0 || E->Op == TokenAssign
            || E->Op == TokenAddAssign || E->Op == TokenSubtractAssign
            || E->Op == TokenMultiplyAssign || E->Op == TokenDivideAssign
            || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign))
    {
        int base, idx, sc, off, addr, rv, es = 4;
        enum LexToken bop = 0;
        struct ValueType *RT = NULL;
        if (E->Left->Left->CachedFunc != NULL && E->Left->Left->CachedFunc->Val != NULL)
            RT = E->Left->Left->CachedFunc->Val->FuncDef.ReturnType;
        if (E->Left->ElemSize > 0 && E->Left->ElemSize <= 8)
            es = E->Left->ElemSize;
        else if (E->Left->Left->ElemSize > 0 && E->Left->Left->ElemSize <= 8)
            es = E->Left->Left->ElemSize;
        else if (RT != NULL && RT->Base == TypePointer && RT->FromType != NULL
                 && RT->FromType->Sizeof > 0 && RT->FromType->Sizeof <= 8)
            es = (int)RT->FromType->Sizeof;
        else
            es = 4;
        if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &base)) return 0;
        if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &idx)) return 0;
        sc = AllocReg(Next);
        if (sc < 0) return 0;
        In.Op = IrLinImm; In.Dest = (unsigned char)sc;
        In.A = In.B = 0; In.Tok = 0; In.Imm = es;
        if (!Emit(Code, Cap, N, In)) return 0;
        off = AllocReg(Next);
        if (off < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)off;
        In.A = (unsigned char)idx; In.B = (unsigned char)sc;
        In.Tok = TokenAsterisk; In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        addr = AllocReg(Next);
        if (addr < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)addr;
        In.A = (unsigned char)base; In.B = (unsigned char)off;
        In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
        if (!Emit(Code, Cap, N, In)) return 0;
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        if (bop != 0)
        {
            int oldv, tmp;
            oldv = AllocReg(Next);
            if (oldv < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)oldv;
            In.A = (unsigned char)addr; In.B = 0; In.Tok = 2; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rv)) return 0;
            tmp = AllocReg(Next);
            if (tmp < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)tmp;
            In.A = (unsigned char)oldv; In.B = (unsigned char)rv;
            In.Tok = (unsigned short)bop; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            rv = tmp;
        }
        else
        {
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rv)) return 0;
        }
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)rv; In.B = (unsigned char)addr;
        In.Tok = 2; In.Imm = es;
        return Emit(Code, Cap, N, In);
    }
    /* Local pointer Index store: q[i] = v where q is T* (not owned array).
     * Under EXEC=vm the generic path refused LocalIsPtr non-owned (uninit-through-
     * pointer residual). For vm_strict we emit: base=LoadLocal(q), addr=base+i*es,
     * StoreIndex Tok=2. Negative i (q[-1]) is just signed arithmetic. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Left->Left != NULL && E->Left->Left->Kind == IrExIdent
        && (E->Left->Left->LocalSlot >= 0 || E->Left->Left->ParamSlot >= 0)
        && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && IrVmLinearFeaturesOn())
    {
        int slot = E->Left->Left->LocalSlot;
        int is_ptr = 0, es = 0, pr, rd, sc, off, addr;
        if (E->Left->Left->ParamSlot >= 0)
            is_ptr = 1; /* param pointer Index store */
        /* Detect pointer (not owned array blob) */
        if (IrCallDepth > 0 && slot < IR_CALL_MAX_LOCALS)
        {
            IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
            if (Fr->LocalIsPtr[slot] && Fr->LocalArraySize[slot] <= 0
                && !Fr->LocalOwned[slot])
            {
                is_ptr = 1;
                {
                    int ps = IrCompLocalPointeeSizeOrFrame(slot);
                    if (ps > 0) es = ps;
                }
            }
        }
        /* Compile-time: LocalIsPtr may be unset; use TF_PTR / Decl.
         * CRITICAL: array Ident often carries IR_TF_PTR (decay). Owned
         * arrays must NOT take Tok=2 path (es=4 corruption / FP miss). */
        {
            int owned_arr = 0;
            if (slot >= 0 && slot < IR_CALL_MAX_LOCALS)
            {
                if (IrCompLocalNDimsOrFrame(slot) > 0 && IrCompLocalLeafEsOrFrame(slot) > 0)
                    owned_arr = 1;
                if (IrCompLocalLeafEsOrFrame(slot) > 4)
                    owned_arr = 1; /* struct/large leaf: never is_ptr Index store */
                if (CompLocalFromType[slot] != NULL
                    && (CompLocalFromType[slot]->Base == TypeStruct
                        || CompLocalFromType[slot]->Base == TypeUnion
                        || CompLocalFromType[slot]->Base == TypeArray))
                    owned_arr = 1;
                if (IrCallDepth > 0)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Fr->LocalOwned[slot] || Fr->LocalArraySize[slot] > 0)
                        owned_arr = 1;
                }
            }
            if (!owned_arr && !is_ptr && (E->Left->Left->TypeFlags & IR_TF_PTR)
                && !(E->Left->Left->TypeFlags & IR_TF_INT
                     && E->Left->Left->ElemSize > 0
                     && E->Left->Left->ElemSize != (int)sizeof(void *)))
                is_ptr = 1;
            if (!owned_arr && !is_ptr && (E->Left->Left->TypeFlags & IR_TF_PTR)
                && E->Left->Left->ElemSize == (int)sizeof(void *))
                is_ptr = 1;
        }
        if (is_ptr)
        {
            if (es <= 0 && E->Left->ElemSize > 0
                && E->Left->ElemSize != (int)sizeof(void *))
                es = E->Left->ElemSize;
            /* Param T*: resolve pointee width — default 4 was wrong for char*
             * (dst[i]=src[i] multi-store SEGV under strict). */
            if (es <= 0 && E->Left->Left->ParamSlot >= 0 && IrCallDepth > 0
                && E->Left->Left->ParamSlot < IrCallStack[IrCallDepth - 1]->NumParams)
            {
                struct ValueType *PT = IrCallStack[IrCallDepth - 1]->ParamTyp[
                    E->Left->Left->ParamSlot];
                if (PT != NULL && PT->Base == TypePointer && PT->FromType != NULL)
                {
                    if (PT->FromType->Base == TypePointer
                        || PT->FromType->Base == TypeFunction)
                        es = (int)sizeof(void *);
                    else if (PT->FromType->Sizeof > 0 && PT->FromType->Sizeof <= 8)
                        es = (int)PT->FromType->Sizeof;
                }
            }
            /* char** / T**: Index element is a pointer word. */
            if ((E->Left->TypeFlags & IR_TF_PTR)
                || E->Left->ElemSize == (int)sizeof(void *))
                es = (int)sizeof(void *);
            if (es <= 0) es = 1; /* never 4 for unknown scalar pointee */
            if (es != 1 && es != 2 && es != 4 && es != 8) es = 1;
            if (!LowerRec(E->Left->Left, Code, Cap, N, Next, &pr)) return 0;
            if (!LowerRec(E->Left->Right, Code, Cap, N, Next, &rd)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = es;
            if (!Emit(Code, Cap, N, In)) return 0;
            off = AllocReg(Next);
            if (off < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)off;
            In.A = (unsigned char)rd; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            addr = AllocReg(Next);
            if (addr < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)addr;
            In.A = (unsigned char)pr; In.B = (unsigned char)off;
            In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            /* STRICT: int RHS into FP pointee — promote before Tok=2 store */
            if (E->Right != NULL && !(E->Right->TypeFlags & IR_TF_FP)
                && (E->Right->Kind != IrExConst || E->Right->Op != TokenFPConstant)
                && ((E->Left->TypeFlags & IR_TF_FP)
                    || (slot >= 0 && slot < IR_CALL_MAX_LOCALS
                        && CompLocalFromType[slot] != NULL
                        && CompLocalFromType[slot]->Base == TypeFP)))
            {
                int cv = AllocReg(Next);
                if (cv < 0) return 0;
                In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
                In.A = (unsigned char)*Dest; In.B = 0;
                In.Tok = TokenCast; In.Imm = 2;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = cv;
            }
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)*Dest;
            In.B = (unsigned char)addr;
            In.Tok = 2;
            In.Imm = es;
            return Emit(Code, Cap, N, In);
        }
    }
    /* Index store local Ident, ElemSize 1/2/4; plain = and compound.
     * Nested array-of-ptr (ptrs[i][j]=) handled by dedicated path below. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIndex
        && E->Right != NULL
        && !(E->Left->Left != NULL && E->Left->Left->Kind == IrExIndex
             && ((E->Left->Left->TypeFlags & IR_TF_PTR)
                 || (IrVmLinearFeaturesOn()
                     && IrLowerIndexRowIsPointer(E->Left->Left))))
        && (E->Op == 0 || E->Op == TokenAssign
            || E->Op == TokenAddAssign || E->Op == TokenSubtractAssign
            || E->Op == TokenMultiplyAssign || E->Op == TokenDivideAssign
            || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign))
    {
        int rd, es, rhs, base, out;
        enum LexToken bop = 0;
        IrExpr *LX = E->Left;
        /* Struct-valued assign into array element (v[i]=mk()/v[j]).
         * Prefer Tok=3 memcpy when dest leaf is aggregate (>8 or struct es). */
        if (E->Right->Kind == IrExCall || E->Right->Kind == IrExMember)
        {
            IrExpr *Root = E->Left->Left;
            int slot = -1, nbytes = 0, src_reg = -1, dst_reg = -1;
            while (Root != NULL && Root->Kind == IrExIndex)
                Root = Root->Left;
            if (Root != NULL && Root->Kind == IrExIdent && Root->LocalSlot >= 0
                && Root->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                slot = Root->LocalSlot;
                if (IrCompLocalLeafEsOrFrame(slot) > 4)
                    nbytes = IrCompLocalLeafEsOrFrame(slot);
                else if (CompLocalFromType[slot] != NULL
                    && (CompLocalFromType[slot]->Base == TypeStruct
                        || CompLocalFromType[slot]->Base == TypeUnion)
                    && CompLocalFromType[slot]->Sizeof > 4)
                    nbytes = (int)CompLocalFromType[slot]->Sizeof;
            }
            if (nbytes > 4 && nbytes <= 512 && E->Right->Kind == IrExCall)
            {
                if (!LowerRec(E->Right, Code, Cap, N, Next, &src_reg))
                    return 0;
                if (!LowerLocalStructArrayAddress(E->Left, slot, nbytes,
                        Code, Cap, N, Next, &dst_reg))
                    return 0;
                { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
                In.A = (unsigned char)src_reg;
                In.B = (unsigned char)dst_reg;
                In.Tok = 3;
                In.Imm = nbytes;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = src_reg;
                return 1;
            }
            /* Scalar Call / Member RHS: fall through to the normal
             * LowerRec + StoreIndex path below. The early return 0
             * rejected a[i] = f() for int-returning calls (897/898). */
        }
        if (E->Right->Kind == IrExIdent && E->Right->LocalSlot >= 0
            && E->Right->ElemSize > 8)
            return 0;
        if (E->Right->Kind == IrExIndex)
        {
            int res = E->Right->ElemSize > 0 ? E->Right->ElemSize
                : (E->Left->ElemSize > 0 ? E->Left->ElemSize : 4);
            if (res > 8)
                return 0; /* struct/large element residual */
            /* scalar Index RHS: fall through and lower as LoadIndex + StoreIndex */
        }
        es = LX->ElemSize;
        {
            IrExpr *Root = LX->Left;
            if (Root == NULL) return 0;
            while (Root != NULL && Root->Kind == IrExIndex)
                Root = Root->Left;
            if (Root == NULL || Root->Kind != IrExIdent
                || (Root->LocalSlot < 0 && Root->Ident == NULL))
                return 0;
            /* T *a[N]: element is pointer-sized (strict; soft uses IdxMeta). */
            if (IrVmLinearFeaturesOn()
                && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
                && IrCompLocalLeafEsOrFrame(Root->LocalSlot) == (int)sizeof(void *))
                es = (int)sizeof(void *);
            /* EXEC=vm: param int* stores allowed; refuse bare local ptr. */
            if (IrExecIsVmProd() && Root->LocalSlot >= 0 && IrCallDepth > 0
                && Root->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (Fr->LocalIsPtr[Root->LocalSlot]
                    && !Fr->LocalOwned[Root->LocalSlot]
                    && Fr->LocalArraySize[Root->LocalSlot] <= 0)
                    return 0;
                /* Whole-element assign into array-of-struct (v[i] = s)
                 * needs memcpy — residual. Pointer elements (IR_TF_PTR) OK.
                 * Do NOT use es==8 alone — collides with long long. */
                {
                    int les = LX->ElemSize;
                    int res = E->Right != NULL ? E->Right->ElemSize : 0;
                    int ptr_elem = (LX->TypeFlags & IR_TF_PTR) != 0
                        || (IrVmLinearFeaturesOn() && les == (int)sizeof(void *));
                    if (!ptr_elem && (les > 4 || res > 4))
                        return 0;
                    if (Fr->LocalArraySize[Root->LocalSlot] > 0
                        && Fr->LocalElemSize[Root->LocalSlot] > 8
                        && !ptr_elem)
                        return 0;
                }
            }
        }
        /* Whole struct/union value store residual (es > 8). A nested
         * multidimensional Index may carry the size of a row here (e.g. 24
         * bytes for int[2][3]); the actual scalar leaf stride is derived
         * from the declaration by the flattening path below. */
        if (es > 8 && LX->Left != NULL && LX->Left->Kind != IrExIndex)
            return 0;
        if (es != 0 && es != 1 && es != 2 && es != 4 && es != 8
            && es != (int)sizeof(int) && es != (int)sizeof(long)
            && es < 0)
            return 0;
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        /* Multi-dim a[i][j](=|op=): flatten indices like rvalue Index path.
         * Previously only LX->Right (innermost) was used → G[i][j]= wrote G[j].
         * Skip array-of-ptr nested (ptrs[i][j]=) — dedicated path below. */
        if (LX->Left != NULL && LX->Left->Kind == IrExIndex
            && !IrLowerIndexRowIsPointer(LX->Left))
        {
            IrExpr *Root = LX;
            IrExpr *Chain[IR_MAX_ARRAY_DIMS];
            int nc = 0, d, dims[IR_MAX_ARRAY_DIMS];
            int nd = 0, leaf_es = 4;
            int idx_regs[IR_MAX_ARRAY_DIMS];
            int flat_reg, tmp_reg;
            IrLinInst Bin;
            while (Root != NULL && Root->Kind == IrExIndex && nc < IR_MAX_ARRAY_DIMS)
            {
                Chain[nc++] = Root;
                Root = Root->Left;
            }
            if (Root == NULL || Root->Kind != IrExIdent)
                return 0;
            if (nc < 2 || nc > IR_MAX_ARRAY_DIMS)
                return 0;
            for (d = 0; d < nc / 2; d++)
            {
                IrExpr *tmp = Chain[d];
                Chain[d] = Chain[nc - 1 - d];
                Chain[nc - 1 - d] = tmp;
            }
            for (d = 0; d < IR_MAX_ARRAY_DIMS; d++) dims[d] = 0;
            if (Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
                && IrCallDepth > 0)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                nd = (int)Fr->LocalNDims[Root->LocalSlot];
                leaf_es = Fr->LocalElemSize[Root->LocalSlot] > 0
                    ? (int)Fr->LocalElemSize[Root->LocalSlot] : 4;
                for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                    dims[d] = Fr->LocalDims[Root->LocalSlot][d];
            }
            /* Compile-time dims: Decl lower stamps before Index lower sees 0. */
            if (nd < 2 && Root->LocalSlot >= 0 && Root->LocalSlot < IR_CALL_MAX_LOCALS
                && IrCompLocalNDimsOrFrame(Root->LocalSlot) >= 2)
            {
                int slot = Root->LocalSlot;
                nd = (int)IrCompLocalNDimsOrFrame(slot);
                leaf_es = IrCompLocalLeafEsOrFrame(slot) > 0 ? IrCompLocalLeafEsOrFrame(slot) : 4;
                for (d = 0; d < nd && d < IR_MAX_ARRAY_DIMS; d++)
                    dims[d] = IrCompLocalDimOrFrame(slot, d);
            }
            else if (nd < 2 && Root->Ident != NULL && LinParser != NULL)
            {
                struct Value *GV = IrGlobalValSoft(LinParser, Root);
                if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray)
                {
                    struct ValueType *T = GV->Typ;
                    nd = 0;
                    while (T != NULL && T->Base == TypeArray && nd < IR_MAX_ARRAY_DIMS)
                    {
                        dims[nd++] = T->ArraySize > 0 ? T->ArraySize : 0;
                        if (T->FromType != NULL && T->FromType->Base != TypeArray)
                            leaf_es = T->FromType->Sizeof > 0
                                ? (int)T->FromType->Sizeof : 4;
                        T = T->FromType;
                    }
                }
            }
            if (nd != nc || nd < 2)
                return 0;
            for (d = 1; d < nd; d++)
                if (dims[d] <= 0) return 0;
            for (d = 0; d < nc; d++)
            {
                if (!LowerRec(Chain[d]->Right, Code, Cap, N, Next, &idx_regs[d]))
                    return 0;
            }
            flat_reg = idx_regs[0];
            for (d = 1; d < nd; d++)
            {
                int dim_reg = AllocReg(Next);
                if (dim_reg < 0) return 0;
                Bin.Op = IrLinImm; Bin.Dest = (unsigned char)dim_reg;
                Bin.A = Bin.B = 0; Bin.Tok = 0; Bin.Imm = dims[d];
                if (!Emit(Code, Cap, N, Bin)) return 0;
                tmp_reg = AllocReg(Next);
                if (tmp_reg < 0) return 0;
                Bin.Op = IrLinBin; Bin.Dest = (unsigned char)tmp_reg;
                Bin.A = (unsigned char)flat_reg; Bin.B = (unsigned char)dim_reg;
                Bin.Tok = TokenAsterisk; Bin.Imm = 0;
                if (!Emit(Code, Cap, N, Bin)) return 0;
                flat_reg = AllocReg(Next);
                if (flat_reg < 0) return 0;
                Bin.Op = IrLinBin; Bin.Dest = (unsigned char)flat_reg;
                Bin.A = (unsigned char)tmp_reg; Bin.B = (unsigned char)idx_regs[d];
                Bin.Tok = TokenPlus; Bin.Imm = 0;
                if (!Emit(Code, Cap, N, Bin)) return 0;
            }
            rd = flat_reg;
            (void)leaf_es;
        }
        else if (!LowerRec(LX->Right, Code, Cap, N, Next, &rd))
            return 0;
        if (IdxN >= 32) return 0;
        if (bop != 0)
        {
            /* a[i] op= rhs  →  load a[i], bin, store */
            base = AllocReg(Next);
            if (base < 0) return 0;
            In.Op = IrLinLoadIndex; In.Dest = (unsigned char)base;
            In.A = (unsigned char)rd; In.B = 0; In.Tok = 0; In.Imm = IdxN;
            IdxMetaPush(LX);
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs)) return 0;
            out = AllocReg(Next);
            if (out < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)out;
            In.A = (unsigned char)base; In.B = (unsigned char)rhs;
            In.Tok = bop; In.Imm = 0;
            if (E->CanonicalOpFlags & IR_CF_UNSIGNED)
                In.Imm |= IR_LIN_F_UNSIGNED;
            if (E->CanonicalOpFlags & IR_CF_WIDE)
                In.Imm |= IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = out;
            if (IdxN >= 32) return 0;
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
            In.A = (unsigned char)*Dest; In.B = (unsigned char)rd; In.Tok = 0; In.Imm = IdxN;
            IdxMetaPush(LX);
            return Emit(Code, Cap, N, In);
        }
        if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
        {
            int lhs_fp = (LX->TypeFlags & IR_TF_FP) != 0;
            if (!lhs_fp && LX->Left != NULL && LX->Left->Kind == IrExIdent
                && LX->Left->LocalSlot >= 0 && LX->Left->LocalSlot < IR_CALL_MAX_LOCALS
                && CompLocalFromType[LX->Left->LocalSlot] != NULL
                && CompLocalFromType[LX->Left->LocalSlot]->Base == TypeFP)
                lhs_fp = 1;
            if (IrVmLinearFeaturesOn() && lhs_fp && E->Right != NULL
                && !(E->Right->TypeFlags & IR_TF_FP)
                && (E->Right->Kind != IrExConst || E->Right->Op != TokenFPConstant))
            {
                int cv = AllocReg(Next);
                if (cv < 0) return 0;
                In.Op = IrLinUnary; In.Dest = (unsigned char)cv;
                In.A = (unsigned char)*Dest; In.B = 0;
                In.Tok = TokenCast; In.Imm = 2;
                if (!Emit(Code, Cap, N, In)) return 0;
                *Dest = cv;
            }
        }
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinStoreIndex; In.Dest = 0;
        In.A = (unsigned char)*Dest; In.B = (unsigned char)rd; In.Tok = 0; In.Imm = IdxN;
        IdxMetaPush(LX);
        return Emit(Code, Cap, N, In);
    }
    /* Global T* gp = rhs: store pointer bits (not fptr-only). */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0
        && E->Left->Ident != NULL && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign)
        && LinParser != NULL)
    {
        struct Value *GV = IrGlobalIdentSoft(LinParser, E->Left->Ident);
        struct Value *TV = NULL;
        IrExpr *R = E->Right;
        int rd;
        /* General global data pointer: gp = &x / gp = &a[i] / gp = p. */
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypePointer
            && GV->Typ->FromType != NULL
            && GV->Typ->FromType->Base != TypeFunction)
        {
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rd) || rd < 0)
                return 0;
            if (GlobN >= 32) return 0;
            In.Op = IrLinStoreGlobal; In.Dest = 0;
            In.A = (unsigned char)rd; In.B = 0; In.Tok = 1; /* pointer store */
            In.Imm = GlobPush(E->Left);
            if ((int)In.Imm < 0) return 0;
            *Dest = rd;
            return Emit(Code, Cap, N, In);
        }
        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypePointer
            && GV->Typ->FromType != NULL && GV->Typ->FromType->Base == TypeFunction)
        {
            if (R->Kind == IrExUnary && R->Op == TokenAmpersand && R->Left != NULL)
                R = R->Left;
            if (R->Kind == IrExIdent && R->Ident != NULL)
                TV = IrGlobalIdentSoft(LinParser, R->Ident);
            if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
            {
                rd = AllocReg(Next);
                if (rd < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)rd;
                In.A = In.B = 0; In.Tok = 5;
                In.Imm = (long long)(intptr_t)TV;
                if (!Emit(Code, Cap, N, In)) return 0;
                if (GlobN >= 32) return 0;
                In.Op = IrLinStoreGlobal; In.Dest = 0;
                In.A = (unsigned char)rd; In.B = 0; In.Tok = 0;
                In.Imm = GlobPush(E->Left);
                if ((int)In.Imm < 0) return 0;
                *Dest = rd;
                return Emit(Code, Cap, N, In);
            }
            /* gfp = local_fp / gfp = tab[i] — load Value* then StoreGlobal */
            if (R->Kind == IrExIndex || R->Kind == IrExMember
                || (R->Kind == IrExIdent && (R->LocalSlot >= 0 || R->ParamSlot >= 0)))
            {
                if (!LowerRec(R, Code, Cap, N, Next, &rd) || rd < 0)
                    return 0;
                if (GlobN >= 32) return 0;
                In.Op = IrLinStoreGlobal; In.Dest = 0;
                In.A = (unsigned char)rd; In.B = 0; In.Tok = 0;
                In.Imm = GlobPush(E->Left);
                if ((int)In.Imm < 0) return 0;
                *Dest = rd;
                return Emit(Code, Cap, N, In);
            }
        }
    }
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot < 0 && E->Left->ParamSlot < 0
        && E->Left->Ident != NULL && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign || E->Op == TokenAddAssign)
        && !(E->Left->TypeFlags & IR_TF_PTR))
    {
        if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
        if (E->Op == TokenAddAssign)
        {
            int ld, sum;
            if (GlobN >= 32) return 0;
            ld = AllocReg(Next);
            if (ld < 0) return 0;
            In.Op = IrLinLoadGlobal; In.Dest = (unsigned char)ld;
            In.A = In.B = 0; In.Tok = 0; In.Imm = GlobPush(E->Left);
        if ((int)In.Imm < 0) return 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            sum = AllocReg(Next);
            if (sum < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)sum;
            In.A = (unsigned char)ld; In.B = (unsigned char)*Dest;
            In.Tok = TokenPlus; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = sum;
        }
        if (GlobN >= 32) return 0;
        In.Op = IrLinStoreGlobal; In.Dest = 0;
        In.A = (unsigned char)*Dest; In.B = 0; In.Tok = 0; In.Imm = GlobPush(E->Left);
        if ((int)In.Imm < 0) return 0;
        return Emit(Code, Cap, N, In);
    }
    /* p += n / p -= n on local pointer: scale n by pointee size. */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && E->Right != NULL
        && (E->Op == TokenAddAssign || E->Op == TokenSubtractAssign)
        && (E->Left->TypeFlags & IR_TF_PTR))
    {
        int slot = E->Left->LocalSlot;
        int scale = E->Left->ElemSize > 0 ? E->Left->ElemSize
                  : (E->ElemSize > 0 ? E->ElemSize : (int)sizeof(int));
        int base, rhs, sc, scaled, out;
        base = AllocReg(Next);
        if (base < 0) return 0;
        In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base;
        In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
        if (!Emit(Code, Cap, N, In)) return 0;
        if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs)) return 0;
        if (scale != 1)
        {
            sc = AllocReg(Next);
            if (sc < 0) return 0;
            In.Op = IrLinImm; In.Dest = (unsigned char)sc;
            In.A = In.B = 0; In.Tok = 0; In.Imm = scale;
            if (!Emit(Code, Cap, N, In)) return 0;
            scaled = AllocReg(Next);
            if (scaled < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)scaled;
            In.A = (unsigned char)rhs; In.B = (unsigned char)sc;
            In.Tok = TokenAsterisk; In.Imm = 0;
            if (!Emit(Code, Cap, N, In)) return 0;
            rhs = scaled;
        }
        out = AllocReg(Next);
        if (out < 0) return 0;
        In.Op = IrLinBin; In.Dest = (unsigned char)out;
        In.A = (unsigned char)base; In.B = (unsigned char)rhs;
        In.Tok = (E->Op == TokenAddAssign) ? TokenPlus : TokenMinus;
        In.Imm = 0;
        if (!Emit(Code, Cap, N, In)) return 0;
        In.Op = IrLinStoreLocal; In.Dest = 0;
        In.A = (unsigned char)out; In.B = 0; In.Tok = 0; In.Imm = slot;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = out;
        return 1;
    }
    /* ParamSlot assign: direct LoadLocal Imm=-(ps+1). */
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->ParamSlot >= 0 && E->Left->ParamSlot < IR_CALL_MAX_PARAMS
        && E->Left->LocalSlot < 0 && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign || E->Op == TokenAddAssign
            || E->Op == TokenSubtractAssign || E->Op == TokenMultiplyAssign
            || E->Op == TokenDivideAssign || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign))
    {
        int base, rhs, out, pimm;
        enum LexToken bop = 0;
        pimm = -(E->Left->ParamSlot + 1);
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        if (bop == 0) {
            struct ValueType *TargetType = LinExprValueType(E->Left, 0);
            if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
            if (TargetType != NULL
                && !IrLinEmitScalarConversion(TargetType->Base, E->Right,
                                               Dest, Code, Cap, N, Next)) return 0;
        } else {
            base = AllocReg(Next);
            if (base < 0) return 0;
            In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base;
            In.A = In.B = 0; In.Tok = 0; In.Imm = pimm;
            if (!Emit(Code, Cap, N, In)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs)) return 0;
            out = AllocReg(Next);
            if (out < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)out;
            In.A = (unsigned char)base; In.B = (unsigned char)rhs;
            In.Tok = bop; In.Imm = 0;
            if (E->CanonicalOpFlags & IR_CF_UNSIGNED)
                In.Imm |= IR_LIN_F_UNSIGNED;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = out;
        }
        In.Op = IrLinStoreLocal; In.Dest = 0;
        In.A = (unsigned char)*Dest; In.B = 0; In.Tok = 0; In.Imm = pimm;
        return Emit(Code, Cap, N, In);
    }
    if (E->Kind == IrExAssign && E->Left != NULL && E->Left->Kind == IrExIdent
        && E->Left->LocalSlot >= 0 && E->Right != NULL
        && (E->Op == 0 || E->Op == TokenAssign || E->Op == TokenAddAssign
            || E->Op == TokenSubtractAssign || E->Op == TokenMultiplyAssign
            || E->Op == TokenDivideAssign || E->Op == TokenModulusAssign
            || E->Op == TokenShiftLeftAssign || E->Op == TokenShiftRightAssign
            || E->Op == TokenArithmeticAndAssign || E->Op == TokenArithmeticOrAssign
            || E->Op == TokenArithmeticExorAssign))
    {
        int base, rhs, out;
        enum LexToken bop = 0;
        /* const assign: emit store, runtime catches via LocalIsConst */
        if (E->Op == TokenAddAssign) bop = TokenPlus;
        else if (E->Op == TokenSubtractAssign) bop = TokenMinus;
        else if (E->Op == TokenMultiplyAssign) bop = TokenAsterisk;
        else if (E->Op == TokenDivideAssign) bop = TokenSlash;
        else if (E->Op == TokenModulusAssign) bop = TokenModulus;
        else if (E->Op == TokenShiftLeftAssign) bop = TokenShiftLeft;
        else if (E->Op == TokenShiftRightAssign) bop = TokenShiftRight;
        else if (E->Op == TokenArithmeticAndAssign) bop = TokenAmpersand;
        else if (E->Op == TokenArithmeticOrAssign) bop = TokenArithmeticOr;
        else if (E->Op == TokenArithmeticExorAssign) bop = TokenArithmeticExor;
        if (bop == 0)
        {
            struct ValueType *TargetType = LinExprValueType(E->Left, 0);
            if (!LowerRec(E->Right, Code, Cap, N, Next, Dest)) return 0;
            if (TargetType != NULL
                && !IrLinEmitScalarConversion(TargetType->Base, E->Right,
                                               Dest, Code, Cap, N, Next)) return 0;
        }
        else
        {
            if (!LowerRec(E->Left, Code, Cap, N, Next, &base)) return 0;
            if (!LowerRec(E->Right, Code, Cap, N, Next, &rhs)) return 0;
            out = AllocReg(Next);
            if (out < 0) return 0;
            In.Op = IrLinBin; In.Dest = (unsigned char)out;
            In.A = (unsigned char)base; In.B = (unsigned char)rhs;
            In.Tok = bop; In.Imm = 0;
            if (E->CanonicalOpFlags & IR_CF_UNSIGNED)
                In.Imm |= IR_LIN_F_UNSIGNED;
            if (E->CanonicalOpFlags & IR_CF_WIDE)
                In.Imm |= IR_LIN_F_WIDE;
            if (!Emit(Code, Cap, N, In)) return 0;
            *Dest = out;
        }
        {
            int ls = E->Left->LocalSlot;
            int is_fp = (CompileSlot && ls >= 0 && ls < IR_CALL_MAX_LOCALS
                         && CompileSlot->SlotIsFptr[ls]);
            if (getenv("BABYC_FP_TRACE"))
                fprintf(stderr, "[fp] Assign Ident StoreLocal ls=%d is_fp=%d\n", ls, is_fp);
            { extern void LinCseInvalAll(void); LinCseInvalAll(); }
            In.Op = IrLinStoreLocal; In.Dest = 0;
            In.A = (unsigned char)*Dest; In.B = 0;
            In.Tok = is_fp ? 5 : 0;
            In.Imm = ls;
            if (!Emit(Code, Cap, N, In)) return 0;
            /* Never stamp LocalInit on the live frame during FnCodeCompile of a
             * callee: slot numbers collide with the caller (inner z and outer x
             * both slot 0 → 1035 masks caller uninit). Runtime StoreLocal is
             * the sole source of truth. */
            if (IrCallDepth > 0 && ls >= 0 && ls < IR_CALL_MAX_LOCALS
                && IrLiveInitStampOk())
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                if (is_fp)
                    Fr->LocalIsFptr[ls] = 1;
                if (E->Left->TypeFlags & IR_TF_PTR)
                    Fr->LocalIsPtr[ls] = 1;
                IrLiveMarkInit(Fr, ls);
            }
        }
        return 1;
    }
    if (getenv("INLINE_TRACE")) fprintf(stderr, "[inline] check %s\n", E->Ident ? E->Ident : "?");
    /* Leaf-inline: any pure small leaf function.
     * Single return of a simple expression using only the parameter.
     * Substitute param with arg and lower. */
    if (E->Kind == IrExCall && E->ArgCount >= 1 && E->ArgCount <= 4
        && E->Ident != NULL && LinParser != NULL)
    {
        struct Value *FV = IrGlobalIdentSoft(LinParser, E->Ident);
        if (FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction
            && FV->Val != NULL && FV->Val->FuncDef.BodyIr != NULL
            && FV->Val->FuncDef.NumParams == E->ArgCount
            && FV->Val->FuncDef.NumParams <= 4)
        {
            IrStmt *Body = FV->Val->FuncDef.BodyIr;
            /* Unwrap block with single return. */
            if (Body != NULL && Body->Kind == IrStBlock && Body->StmtCount == 1
                && Body->Stmts[0] != NULL && Body->Stmts[0]->Kind == IrStReturn)
                Body = Body->Stmts[0];
            if (Body != NULL && Body->Kind == IrStReturn && Body->Expr != NULL)
            {
                IrExpr *Ret = Body->Expr;
                /* Check purity: no calls, no stores, only param refs. */
                /* For now, handle p->m + p->n + p->o with &global arg. */
                if (E->ArgCount == 1 && Ret->Kind == IrExBinOp && Ret->Op == TokenPlus
                    && Ret->Left != NULL && Ret->Right != NULL
                    && Ret->Left->Kind == IrExBinOp && Ret->Left->Op == TokenPlus
                    && Ret->Left->Left != NULL && Ret->Left->Left->Kind == IrExMember
                    && Ret->Left->Right != NULL && Ret->Left->Right->Kind == IrExMember
                    && Ret->Right->Kind == IrExMember)
                {
                    IrExpr *Arg = E->Args[0];
                    if (Arg != NULL && Arg->Kind == IrExUnary
                        && Arg->Op == TokenAmpersand
                        && Arg->Left != NULL && Arg->Left->Kind == IrExIdent
                        && Arg->Left->LocalSlot < 0 && Arg->Left->Ident != NULL)
                    {
                        int r1, r2, r3;
                        IrExpr MA, MB, MC;
                        memset(&MA, 0, sizeof(MA));
                        memset(&MB, 0, sizeof(MB));
                        memset(&MC, 0, sizeof(MC));
                        MA.Kind = IrExMember; MA.Left = Arg->Left;
                        MA.Ident = Ret->Left->Left->Ident;
                        MA.CanonicalBase = Ret->Left->Left->CanonicalBase;
                        MA.CanonicalFlags = Ret->Left->Left->CanonicalFlags;
                        MA.CanonicalOpBase = Ret->Left->Left->CanonicalOpBase;
                        MA.CanonicalOpFlags = Ret->Left->Left->CanonicalOpFlags;
                        MA.TypeFlags = Ret->Left->Left->TypeFlags;
                        MA.SourceLine = Ret->Left->Left->SourceLine;
                        if (!LowerRec(&MA, Code, Cap, N, Next, &r1)) goto no_inline;
                        MB.Kind = IrExMember; MB.Left = Arg->Left;
                        MB.Ident = Ret->Left->Right->Ident;
                        MB.CanonicalBase = Ret->Left->Right->CanonicalBase;
                        MB.CanonicalFlags = Ret->Left->Right->CanonicalFlags;
                        MB.CanonicalOpBase = Ret->Left->Right->CanonicalOpBase;
                        MB.CanonicalOpFlags = Ret->Left->Right->CanonicalOpFlags;
                        MB.TypeFlags = Ret->Left->Right->TypeFlags;
                        MB.SourceLine = Ret->Left->Right->SourceLine;
                        if (!LowerRec(&MB, Code, Cap, N, Next, &r2)) goto no_inline;
                        MC.Kind = IrExMember; MC.Left = Arg->Left;
                        MC.Ident = Ret->Right->Ident;
                        MC.CanonicalBase = Ret->Right->CanonicalBase;
                        MC.CanonicalFlags = Ret->Right->CanonicalFlags;
                        MC.CanonicalOpBase = Ret->Right->CanonicalOpBase;
                        MC.CanonicalOpFlags = Ret->Right->CanonicalOpFlags;
                        MC.TypeFlags = Ret->Right->TypeFlags;
                        MC.SourceLine = Ret->Right->SourceLine;
                        if (!LowerRec(&MC, Code, Cap, N, Next, &r3)) goto no_inline;
                        *Dest = AllocReg(Next);
                        if (*Dest < 0) goto no_inline;
                        In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
                        In.A = (unsigned char)r1; In.B = (unsigned char)r2;
                        In.Tok = TokenPlus; In.Imm = 0;
                        if (!Emit(Code, Cap, N, In)) goto no_inline;
                        In.Op = IrLinBin; In.Dest = (unsigned char)*Dest;
                        In.A = (unsigned char)*Dest; In.B = (unsigned char)r3;
                        In.Tok = TokenPlus; In.Imm = 0;
                        if (!Emit(Code, Cap, N, In)) goto no_inline;
                        return 1;
                        no_inline: ;
                    }
                }
            }
        }
    }
    /* F6-gated call lower: pure-ish Ident callee, ≤4 int args.
     * BABYC_LOWER_CALL=1. Side effects on globals may double under EXEC=both
     * (linear runs before tree); prefer leaf pure helpers. */
    /* User + host calls (≤IR_LIN_CALL_ARGS). Intrinsic only when
     * LowerCallEnabled (EXEC=vm / BABYC_LOWER_CALL=1) to avoid double printf
     * under EXEC=both shadow. */
    if (E->Kind == IrExCall && getenv("CALL_TRACE")) fprintf(stderr, "[call] %s enabled=%d\n", E->Ident ? E->Ident : "?", LowerCallEnabled());
    if (E->Kind == IrExCall
        && E->ArgCount >= 0 && E->ArgCount <= IR_LIN_CALL_ARGS
        && CallN < IR_LIN_CALL_SLOTS)
    {
        struct Value *FV = NULL;
        const char *FName = E->Ident;
        if (getenv("BABYC_FP_TRACE"))
            fprintf(stderr, "[fp] enter call leftKind=%d args=%d CallN=%d\n",
                E->Left?E->Left->Kind:-1, E->ArgCount, CallN);
        int ai, out, ar[IR_LIN_CALL_ARGS];
        int is_host = 0;
        {
            IrExpr *Callee = E->Left;
            if (Callee != NULL && Callee->Kind == IrExUnary && Callee->Op == TokenAsterisk
                && Callee->Left != NULL)
                Callee = Callee->Left; /* (*fp) */
            if (FName == NULL && Callee != NULL && Callee->Kind == IrExIdent)
                FName = Callee->Ident;
            if (E->CachedFunc != NULL)
                FV = E->CachedFunc;
            else if (Callee != NULL && Callee->CachedFunc != NULL)
                FV = Callee->CachedFunc;
            else if (LinParser != NULL && FName != NULL)
            {
                struct Value *TV = NULL;
                if (Callee != NULL && Callee->Kind == IrExIdent
                    && (Callee->LocalSlot >= 0 || Callee->ParamSlot >= 0))
                    TV = NULL; /* local fptr */
                else
                    TV = IrGlobalIdentSoft(LinParser, FName);
                /* Global fptr Variable: keep Variable as FV marker if Pointer
                 * not yet set (gfp=add after Decl); runtime resolves Pointer. */
                if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypePointer
                    && TV->Typ->FromType != NULL
                    && TV->Typ->FromType->Base == TypeFunction)
                {
                    if (TV->Val != NULL && TV->Val->Pointer != NULL)
                        FV = (struct Value *)TV->Val->Pointer;
                    else
                        FV = TV; /* resolve at runtime */
                }
                else
                    FV = TV;
            }
        }
        if (FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction
            && FV->Val != NULL && FV->Val->FuncDef.Intrinsic != NULL)
            is_host = 1;
        if (!is_host && FV != NULL && FV->Typ != NULL && FV->Typ->Base == TypeFunction
            && FV->Val != NULL && !FV->Val->FuncDef.VarArgs
            && E->ArgCount > FV->Val->FuncDef.NumParams
            && LinParser != NULL)
        {
            if (E->SourceLine > 0) LinParser->Line = E->SourceLine;
            ProgramFail(LinParser, "too many arguments to function");
        }
        /* Name-based host detect when Intrinsic not yet bound at lower time.
         * Shared with eligibility via IrLinHostSupported. */
        if (!is_host && FName != NULL && IrLinHostSupported(FName))
            is_host = 1;
        /* A resolved Intrinsic uses the shared Value-array ABI and can be
         * invoked by IrCallHostFromRegs. The name whitelist is only for
         * unresolved host names; applying it to a registered Intrinsic
         * incorrectly rejects APIs with varargs (including zero/NULL tails),
         * even though the generic argument packer supports them. */
        if (is_host && FName != NULL && !IrLinHostSupported(FName)
            && !(FV != NULL && FV->Val != NULL
                 && FV->Val->FuncDef.Intrinsic != NULL))
            return 0;
        {
            int indirect_slot = -1;
            int indirect_reg = -1;
            IrExpr *Callee = E->Left;
            if (Callee != NULL && Callee->Kind == IrExUnary && Callee->Op == TokenAsterisk
                && Callee->Left != NULL)
                Callee = Callee->Left;
            if (!is_host && Callee != NULL && Callee->Kind == IrExIdent
                && Callee->LocalSlot >= 0
                && Callee->LocalSlot < IR_CALL_MAX_LOCALS)
            {
                int slot = Callee->LocalSlot;
                int is_fp = 0;
                if (CompileSlot != NULL && CompileSlot->SlotIsFptr[slot])
                    is_fp = 1;
                else if (IrCallDepth > 0
                         && IrCallStack[IrCallDepth - 1]->LocalIsFptr[slot])
                    is_fp = 1;
                if (!is_fp && (FV == NULL || FV->Typ == NULL
                               || FV->Typ->Base != TypeFunction))
                    is_fp = 1;
                if (is_fp)
                    indirect_slot = slot;
            }
            else if (!is_host && Callee != NULL && Callee->Kind == IrExIdent
                     && Callee->ParamSlot >= 0
                     && Callee->ParamSlot < IR_CALL_MAX_PARAMS)
            {
                /* Formal fptr: FV from ParamVal[ParamSlot] at runtime. */
                CallIndirectParam[CallN] = Callee->ParamSlot;
                indirect_reg = -2; /* marker: param path, not reg */
            }
            else if (0 && !is_host && Callee != NULL && Callee->Kind == IrExIndex
                     && Callee->Left != NULL && Callee->Left->Kind == IrExIdent
                     && Callee->Left->LocalSlot >= 0
                     && Callee->Right != NULL)
            {
                /* DISABLED: deferred via CallIndirectExpr after args. */
                int slot = Callee->Left->LocalSlot;
                int idx_r = -1, base_r, sc, off, addr, fr;
                int es = (int)sizeof(void *);
                int is_fp = 0;
                if (CompileSlot != NULL && CompileSlot->SlotIsFptr[slot])
                    is_fp = 1;
                else if (IrCompLocalLeafEsOrFrame(slot) == (int)sizeof(void *))
                    is_fp = 1;
                else if (IrCallDepth > 0
                         && IrCallStack[IrCallDepth - 1]->LocalIsFptr[slot])
                    is_fp = 1;
                if (!is_fp)
                    return 0;
                if (!LowerRec(Callee->Right, Code, Cap, N, Next, &idx_r) || idx_r < 0)
                    return 0;
                base_r = AllocReg(Next);
                if (base_r < 0) return 0;
                In.Op = IrLinLoadLocal; In.Dest = (unsigned char)base_r;
                In.A = In.B = 0; In.Tok = 0; In.Imm = slot;
                if (!Emit(Code, Cap, N, In)) return 0;
                sc = AllocReg(Next);
                if (sc < 0) return 0;
                In.Op = IrLinImm; In.Dest = (unsigned char)sc;
                In.A = In.B = 0; In.Tok = 0; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                off = AllocReg(Next);
                if (off < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)off;
                In.A = (unsigned char)idx_r; In.B = (unsigned char)sc;
                In.Tok = TokenAsterisk; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                addr = AllocReg(Next);
                if (addr < 0) return 0;
                In.Op = IrLinBin; In.Dest = (unsigned char)addr;
                In.A = (unsigned char)base_r; In.B = (unsigned char)off;
                In.Tok = TokenPlus; In.Imm = IR_LIN_F_WIDE;
                if (!Emit(Code, Cap, N, In)) return 0;
                fr = AllocReg(Next);
                if (fr < 0) return 0;
                In.Op = IrLinLoadIndex; In.Dest = (unsigned char)fr;
                In.A = (unsigned char)addr; In.B = 0;
                In.Tok = 2; In.Imm = es;
                if (!Emit(Code, Cap, N, In)) return 0;
                indirect_reg = fr;
            }
            else if (!is_host && Callee != NULL && Callee->Kind == IrExMember
                     && Callee->Left != NULL && Callee->Ident != NULL)
            {
                /* Defer Member load until AFTER args (reg clobber). */
                CallIndirectExpr[CallN] = Callee;
                indirect_reg = -3; /* pending Member */
            }
            else if (!is_host && Callee != NULL && Callee->Kind == IrExIndex
                     && Callee->Left != NULL && Callee->Left->Kind == IrExIdent
                     && Callee->Right != NULL)
            {
                int ok_fp = 0;
                if (Callee->Left->LocalSlot >= 0)
                {
                    int slot = Callee->Left->LocalSlot;
                    if (CompileSlot != NULL && CompileSlot->SlotIsFptr[slot])
                        ok_fp = 1;
                    else if (IrCompLocalLeafEsOrFrame(slot) == (int)sizeof(void *))
                        ok_fp = 1;
                    else if (IrCallDepth > 0
                             && IrCallStack[IrCallDepth - 1]->LocalIsFptr[slot])
                        ok_fp = 1;
                }
                else if (Callee->Left->LocalSlot < 0 && Callee->Left->ParamSlot < 0
                         && Callee->Left->Ident != NULL && LinParser != NULL)
                {
                    struct Value *GV = IrGlobalIdentSoft(LinParser, Callee->Left->Ident);
                    if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray
                        && GV->Typ->FromType != NULL
                        && ((GV->Typ->FromType->Base == TypePointer
                             && GV->Typ->FromType->FromType
                             && GV->Typ->FromType->FromType->Base == TypeFunction)
                            || GV->Typ->FromType->Base == TypeFunction))
                        ok_fp = 1;
                }
                if (!ok_fp)
                    return 0;
                CallIndirectExpr[CallN] = Callee;
                indirect_reg = -4; /* pending Index */
            }
            if (indirect_slot >= 0 || indirect_reg >= 0 || CallIndirectParam[CallN] >= 0
                || CallIndirectExpr[CallN] != NULL)
            {
                if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction)
                    FV = NULL;
                if (indirect_reg >= 0)
                    CallIndirectReg[CallN] = indirect_reg;
                if (indirect_slot >= 0)
                    CallIndirectSlot[CallN] = indirect_slot;
                goto call_args_ok;
            }
        }
        /* Direct call to a global name with no binding (undeclared
         * function, 1030): lower as a normal call with a NULL callee; the
         * executor does the tree's hard lookup at the call point (same
         * diagnostic, same position, after the preceding statements ran). */
        if (getenv("CALL_TRACE")) fprintf(stderr, "[call] direct check FV=%p\n", (void*)FV);
        if (!is_host && FV == NULL && FName != NULL && LinParser != NULL
            && (E->Left == NULL
                || (E->Left->Kind == IrExIdent && E->Left->LocalSlot < 0
                    && E->Left->ParamSlot < 0))
            && IrGlobalIdentSoft(LinParser, FName) == NULL)
            goto call_args_ok;
        if (getenv("CALL_TRACE")) fprintf(stderr, "[call] type check FV->Typ=%p\n", FV ? (void*)FV->Typ : NULL);
        if (!is_host && (FV == NULL || FV->Typ == NULL
                         || (FV->Typ->Base != TypeFunction
                             && !(FV->Typ->Base == TypePointer
                                  && FV->Typ->FromType != NULL
                                  && FV->Typ->FromType->Base == TypeFunction))))
        {
            if (getenv("CALL_TRACE"))
                fprintf(stderr, "[call] refuse name=%s FV=%p base=%d\n", FName?FName:"?", (void*)FV, FV && FV->Typ ? FV->Typ->Base : -1);
            if (getenv("BABYC_FP_TRACE"))
                fprintf(stderr, "[fp] refuse call name=%s FV=%p leftKind=%d\n",
                    FName?FName:"?", (void*)FV,
                    E->Left?E->Left->Kind:-1);
            return 0;
        }
        /* Token-only callee (no BodyIr): parent must stay on tree so
         * IrCallDepth/frame save for nested IR→token→IR is correct (659).
         * Skip for global fptr Variable marker (resolved at runtime). */
        if (getenv("CALL_TRACE")) fprintf(stderr, "[call] BodyIr check\n");
        if (!is_host && FV->Typ != NULL && FV->Typ->Base == TypeFunction
            && FV->Val != NULL && FV->Val->FuncDef.BodyIr == NULL)
        { if (getenv("CALL_TRACE")) fprintf(stderr, "[call] no BodyIr\n"); return 0; }
        call_args_ok: ;
        if (is_host && !LowerCallEnabled())
        return 0;
        /* Allow Ident, (*Ident), Index, Member (and (*Member)). */
        if (getenv("CALL_TRACE")) fprintf(stderr, "[call] ident=%s left=%d\n", E->Ident ? E->Ident : "NULL", E->Left ? E->Left->Kind : -1);
        if (!is_host && E->Left != NULL && E->Ident == NULL)
        {
            IrExpr *C = E->Left;
            if (C->Kind == IrExUnary && C->Op == TokenAsterisk && C->Left != NULL)
                C = C->Left;
            if (C->Kind == IrExIdent || C->Kind == IrExIndex || C->Kind == IrExMember)
                ;
            else
                return 0;
        }
        /* Formal fptr ParamSlot and Member callees handled via CallIndirect*. */
        if (!is_host && E->Left != NULL && E->Left->Kind == IrExMember
            && E->Ident == NULL)
        {
            /* bare Member callee without Unary* rare; allow via IndirectReg */
        }
        /* Host args: string const OK; nested Call OK (scalar reg);
         * Index/Member deferred until array Decl-init lowers under VM.
         * refuse Ident-ptr / & / * (pass-by-address SEGV history). */
        if (getenv("CALL_TRACE") && is_host) fprintf(stderr, "[call] host arg check\n");
        if (is_host)
        {
            for (ai = 0; ai < E->ArgCount; ai++)
            {
                IrExpr *A = (E->Args != NULL) ? E->Args[ai] : NULL;
                if (A == NULL) continue;
                if (A->Kind == IrExConst)
                    continue; /* format string / int const */
                if (A->Kind == IrExCall)
                    continue;
                if (A->Kind == IrExIndex || A->Kind == IrExMember)
                    continue;
                /* &g[i] / &local for host (memcpy/strcpy): allow under VM prod. */
                if (A->Kind == IrExUnary && A->Op == TokenAmpersand)
                {
                    if (IrExecIsVmProd())
                        continue;
                    return 0;
                }
                if (A->Kind == IrExUnary && A->Op == TokenAsterisk)
                    continue;
                /* Ternary / cast that produce a pointer value (printf %s of
                 * cond ? "a" : "b", or (void*)). Lower already emits the
                 * bits; CallArgIsPtr packing marks them as pointers. */
                if (A->Kind == IrExTernary || (A->Kind == IrExUnary && A->Op == TokenCast))
                    continue;
                /* Pointer/array Ident: LocalVal may be unsynced under per-stmt
                 * EXEC=vm (decl/assign ran on tree). Force tree for local %s.
                 * Global array Ident OK under EXEC=vm (IrGlobalVal packs addr). */
                if (A->Kind == IrExIdent)
                {
                    /* EXEC=vm: local/param array or pointer Ident is OK for
                     * strcpy/memset/printf %s — DECL_ARRAY/StoreLocal leaves
                     * the blob address in LocalVal; CallArgIsPtr packs it. */
                    if (IrExecIsVmProd()
                        && (A->LocalSlot >= 0 || A->ParamSlot >= 0))
                        continue;
                    if (A->TypeFlags & IR_TF_PTR)
                    {
                        if (IrExecIsVmProd() && A->LocalSlot < 0 && A->ParamSlot < 0
                            && A->Ident != NULL)
                            continue;
                        return 0;
                    }
                    if (A->LocalSlot >= 0 && IrCallDepth > 0
                        && A->LocalSlot < IR_CALL_MAX_LOCALS)
                    {
                        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                        struct ValueType *LT = Fr->LocalFromType[A->LocalSlot];
                        int declared_ptr = 0;
                        if (LT == NULL) LT = Fr->LocalTyp[A->LocalSlot];
                        if (LT != NULL)
                            declared_ptr = (LT->Base == TypePointer || LT->Base == TypeArray
                                || LT->Base == TypeStruct || LT->Base == TypeUnion
                                || LT->Base == TypeFunction);
                        if (Fr->LocalArraySize[A->LocalSlot] > 0
                            || (Fr->LocalIsPtr[A->LocalSlot] && (LT == NULL || declared_ptr)))
                            return 0;
                    }
                    continue; /* scalar Ident OK */
                }
                /* Pointer arithmetic (text + 1, p - 2): the lowered BinOp
                 * yields the scaled address; packed as pointer below. */
                if (A->Kind == IrExBinOp && IrExecIsVmProd()
                    && (A->Op == TokenPlus || A->Op == TokenMinus))
                    continue;
                if ((A->TypeFlags & IR_TF_PTR) && A->Kind != IrExCall)
                    return 0;
            }
        }
        /* User Call + pointer/array Ident arg: allowed under EXEC=vm.
         * Array Ident loads LocalVal (blob addr); CallArgIsPtr packs pointer.
         * Still refuse indexed/member/address-of arguments (surface incomplete). */
        if (!is_host && !IrExecIsVmProd())
        {
            for (ai = 0; ai < E->ArgCount; ai++)
            {
                IrExpr *A = (E->Args != NULL) ? E->Args[ai] : NULL;
                if (A == NULL) continue;
                /* By-value aggregate designator: LowerRec yields its object
                 * address and the call ABI copies CallArgType's size. */
                if (A->CanonicalType != NULL && IrExprDesignatesAggregateObject(A))
                    continue;
                if (A->Kind == IrExIndex || A->Kind == IrExMember)
                    return 0;
                if (A->Kind == IrExUnary
                    && (A->Op == TokenAmpersand || A->Op == TokenAsterisk))
                    return 0;
                if (A->Kind == IrExIdent && IrCallDepth > 0
                    && A->LocalSlot >= 0 && A->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Fr->LocalIsPtr[A->LocalSlot]
                        || Fr->LocalArraySize[A->LocalSlot] > 0)
                        return 0;
                }
                if ((A->TypeFlags & IR_TF_PTR) && A->Kind != IrExCall)
                    return 0;
            }
        }
        else if (!is_host && IrExecIsVmProd())
        {
            for (ai = 0; ai < E->ArgCount; ai++)
            {
                IrExpr *A = (E->Args != NULL) ? E->Args[ai] : NULL;
                if (A == NULL) continue;
                /* By-value aggregate designator: LowerRec yields its object
                 * address and the call ABI copies CallArgType's size. */
                if (A->CanonicalType != NULL && IrExprDesignatesAggregateObject(A))
                    continue;
                /* Multidim row decay m[i] → pointer on the Linear/bytecode path. */
                if (A->Kind == IrExIndex && IrVmLinearFeaturesOn()
                    && A->Left != NULL && A->Left->Kind == IrExIdent
                    && A->Left->LocalSlot >= 0
                    && A->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    int slot = A->Left->LocalSlot;
                    /* GUARD: dual-read dimensions */
                    int nd = IrCompLocalNDimsOrFrame(slot);
                    if (nd >= 2)
                        continue; /* row decay allowed */
                }
                /* Member scalar as a by-value user-call argument on Linear:
                 * s.m, o.m, p->m (param), tab[i].m. */
                if (A->Kind == IrExMember && IrVmLinearFeaturesOn() && A->Left != NULL)
                {
                    IrExpr *B = A->Left;
                    if (B->Kind == IrExIdent
                        && (B->LocalSlot >= 0 || B->ParamSlot >= 0
                            || (B->LocalSlot < 0 && B->ParamSlot < 0
                                && B->Ident != NULL)))
                        continue;
                    if (B->Kind == IrExIndex && B->Left != NULL
                        && B->Left->Kind == IrExIdent
                        && (B->Left->LocalSlot >= 0 || B->Left->ParamSlot >= 0
                            || (B->Left->LocalSlot < 0 && B->Left->ParamSlot < 0
                                && B->Left->Ident != NULL)))
                        continue;
                }
                if (A->Kind == IrExIndex || A->Kind == IrExMember)
                {
                    /* Scalar/pointer values selected from arrays or members
                     * are ordinary by-value call arguments once LowerRec can
                     * emit their load. Reject only aggregate designators whose
                     * ABI is a blob/copy, not every Index/Member by syntax. */
                    struct ValueType *AT = IrVmLinearFeaturesOn()
                        ? LinExprValueType(A, 0) : NULL;
                    if (AT != NULL && AT->Base != TypeArray
                        && AT->Base != TypeStruct && AT->Base != TypeUnion)
                        continue;
                    /* Some local arrays of function pointers are represented
                     * only by the array element stride and PTR flag (the local
                     * type table retains the function, not pointer, type).
                     * Preserve this declared pointer-element case on both
                     * 32- and 64-bit targets. */
                    if (A->Kind == IrExIndex && A->ElemSize == (int)sizeof(void *)
                        && (A->Left != NULL && (A->Left->TypeFlags & IR_TF_PTR))
                        && A->Left->Kind == IrExIdent
                        && (A->Left->LocalSlot >= 0 || A->Left->ParamSlot >= 0
                            || A->Left->Ident != NULL))
                        continue;
                    return 0;
                }
                /* &scalar local on Linear/bytecode: mark the local initialized
                 * when its address escapes to a user call. Array/owned locals
                 * still require the specialized object path below. */
                if (A->Kind == IrExUnary && A->Op == TokenAmpersand
                    && A->Left != NULL && A->Left->Kind == IrExIdent
                    && A->Left->LocalSlot >= 0
                    && A->Left->LocalSlot < IR_CALL_MAX_LOCALS
                    && IrCallDepth > 0 && IrVmLinearFeaturesOn())
                {
                    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                    if (Fr->LocalArraySize[A->Left->LocalSlot] > 0
                        || Fr->LocalOwned[A->Left->LocalSlot])
                        return 0;
                    IrLiveMarkInit(Fr, A->Left->LocalSlot);
                    continue;
                }
                /* &a[i] / &aos[i] as user-call argument on Linear.
                 * AddressOf Index lowers to base+i*es (local or global). */
                if (A->Kind == IrExUnary && A->Op == TokenAmpersand
                    && IrVmLinearFeaturesOn() && A->Left != NULL
                    && A->Left->Kind == IrExIndex
                    && A->Left->Left != NULL
                    && A->Left->Left->Kind == IrExIdent
                    && (A->Left->Left->LocalSlot >= 0
                        || (A->Left->Left->LocalSlot < 0
                            && A->Left->Left->ParamSlot < 0
                            && A->Left->Left->Ident != NULL)))
                    continue;
                /* &G global struct/scalar as user-call argument on Linear. */
                if (A->Kind == IrExUnary && A->Op == TokenAmpersand
                    && IrVmLinearFeaturesOn() && A->Left != NULL
                    && A->Left->Kind == IrExIdent
                    && A->Left->LocalSlot < 0 && A->Left->ParamSlot < 0
                    && A->Left->Ident != NULL)
                    continue;
                if (A->Kind == IrExUnary && A->Op == TokenAsterisk
                    && IrVmLinearFeaturesOn())
                {
                    /* A dereference yielding a scalar/pointer is an ordinary
                     * by-value argument. LowerRec emits the checked pointee
                     * load; refusing every unary * unnecessarily residualizes
                     * calls such as f(*global_ptr). Aggregate pointees still
                     * need their separate blob/copy ABI. */
                    struct ValueType *AT = LinExprValueType(A, 0);
                    if (AT != NULL && AT->Base != TypeArray
                        && AT->Base != TypeStruct && AT->Base != TypeUnion)
                        continue;
                }
                if (A->Kind == IrExUnary
                    && (A->Op == TokenAmpersand || A->Op == TokenAsterisk))
                    return 0;
            }
        }
        if (getenv("CALL_TRACE")) fprintf(stderr, "[call] args=%d\n", E->ArgCount);
        for (ai = 0; ai < E->ArgCount; ai++)
        {
            IrExpr *A;
            int arg_base = *Next;
            if (E->Args == NULL || E->Args[ai] == NULL) return 0;
            A = E->Args[ai];
            /* Function designator → Imm of Function Value* for formal fptr. */
            if (A->Kind == IrExIdent && A->LocalSlot < 0 && A->ParamSlot < 0
                && A->Ident != NULL && LinParser != NULL)
            {
                struct Value *TV = IrGlobalIdentSoft(LinParser, A->Ident);
                if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
                {
                    ar[ai] = AllocReg(Next);
                    if (ar[ai] < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)ar[ai];
                    In.A = In.B = 0; In.Tok = 5;
                    In.Imm = (long long)(intptr_t)TV;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    continue;
                }
            }
            /* Ampersand of function: &add1 */
            if (A->Kind == IrExUnary && A->Op == TokenAmpersand
                && A->Left != NULL && A->Left->Kind == IrExIdent
                && A->Left->Ident != NULL && LinParser != NULL)
            {
                struct Value *TV = IrGlobalIdentSoft(LinParser, A->Left->Ident);
                if (TV != NULL && TV->Typ != NULL && TV->Typ->Base == TypeFunction)
                {
                    ar[ai] = AllocReg(Next);
                    if (ar[ai] < 0) return 0;
                    In.Op = IrLinImm; In.Dest = (unsigned char)ar[ai];
                    In.A = In.B = 0; In.Tok = 5;
                    In.Imm = (long long)(intptr_t)TV;
                    if (!Emit(Code, Cap, N, In)) return 0;
                    continue;
                }
            }
            { extern void LinCseReset(void); LinCseReset(); }
            if (!LowerRec(A, Code, Cap, N, Next, &ar[ai]))
            { if (getenv("CALL_TRACE")) fprintf(stderr, "[call] arg %d fail\n", ai); return 0; }
            /* Temporaries of this argument above its result are dead once
             * the value is computed (only CallArgReg is read at the call):
             * release them so wide calls (printf with 9+ ternary args, 580)
             * fit the register file instead of refusing the statement. */
            if (ar[ai] > arg_base && *Next > IR_LIN_REGS / 2)
            {
                /* Under register pressure also reclaim the temporaries below
                 * the result (ternary/condition regs): one 64-bit AddI #0
                 * copy into the first free slot. Normal-width calls never
                 * reach this, so their code is unchanged. */
                In.Op = IrLinAddI; In.Dest = (unsigned char)arg_base;
                In.A = (unsigned char)ar[ai]; In.B = 0; In.Tok = 0; In.Imm = 0;
                if (!Emit(Code, Cap, N, In)) return 0;
                ar[ai] = arg_base;
            }
            if (ar[ai] >= arg_base && ar[ai] + 1 < *Next)
                *Next = ar[ai] + 1;
        }
        out = AllocReg(Next);
        if (out < 0) return 0;
        /* Lower deferred Member/Index callee AFTER args so CallIndirectReg
         * is not clobbered by arg evaluation (p->fp(p->tag) R[0] overwrite). */
        if (CallIndirectExpr[CallN] != NULL)
        {
            IrExpr *CE = CallIndirectExpr[CallN];
            int fr = -1;
            if (!LowerRec(CE, Code, Cap, N, Next, &fr) || fr < 0)
                return 0;
            CallIndirectReg[CallN] = fr;
            if (getenv("BABYC_FP_TRACE"))
                fprintf(stderr, "[fp] deferred callee fr=%d kind=%d\n", fr, CE->Kind);
        }
        CallFnTab[CallN] = FV;
        /* Preserve CallIndirectReg/Slot if pre-set by Index path. */
        {
            IrExpr *Callee = E->Left;
            if (Callee != NULL && Callee->Kind == IrExUnary && Callee->Op == TokenAsterisk
                && Callee->Left != NULL)
                Callee = Callee->Left;
            if (Callee != NULL && Callee->Kind == IrExIdent
                && Callee->LocalSlot >= 0
                && CallIndirectReg[CallN] < 0
                && CallIndirectSlot[CallN] < 0
                && (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction
                    || (CompileSlot && CompileSlot->SlotIsFptr[Callee->LocalSlot])
                    || (IrCallDepth > 0
                        && IrCallStack[IrCallDepth - 1]->LocalIsFptr[Callee->LocalSlot])))
                CallIndirectSlot[CallN] = Callee->LocalSlot;
        }
        CallNameTab[CallN] = FName;
        CallCharPos[CallN] = (E->CharacterPos > 0) ? E->CharacterPos
            : (LinParser ? LinParser->CharacterPos : 0);

        CallArgN[CallN] = E->ArgCount;
        for (ai = 0; ai < E->ArgCount; ai++)
        {
            IrExpr *A = E->Args[ai];
            CallArgReg[CallN][ai] = ar[ai];
            CallArgIsPtr[CallN][ai] = 0;
            CallArgIsFp[CallN][ai] = 0;
            CallArgTypeBase[CallN][ai] = TypeInt;
            CallStrOn[CallN][ai] = 0;
            
            CallStrBits[CallN][ai] = 0;
            if (A != NULL)
            {
                if (A->Kind == IrExConst
                    && (A->Op == TokenStringConstant || (A->TypeFlags & IR_TF_PTR)))
                {
                    CallArgIsPtr[CallN][ai] = 1;
                    CallStrOn[CallN][ai] = 1;
                    CallStrBits[CallN][ai] = A->ConstVal;
                }
                else if (A->Kind == IrExCall)
                    CallArgIsPtr[CallN][ai] = 0;
                else if (A->Kind == IrExUnary && A->Op == TokenAmpersand)
                    CallArgIsPtr[CallN][ai] = 1; /* &x / &n for scanf %d%n */
                else if (A->Kind == IrExTernary)
                    CallArgIsPtr[CallN][ai] = (unsigned char)IrExprPtrValuedTF(A, 0);
                else if (A->Kind == IrExUnary && A->Op == TokenCast)
                    CallArgIsPtr[CallN][ai] = (A->TypeFlags & IR_TF_PTR) ? 1 : 0;
                else if (A->Kind == IrExMember)
                    CallArgIsPtr[CallN][ai] = 1; /* s.arr decay / s.name */
                else if (A->Kind == IrExIdent
                         && A->LocalSlot < 0 && A->ParamSlot < 0
                         && IrLinIdentIsFuncDesignator(A))
                {
                    /* Function designator: emit already put Func Value* in
                     * the reg (Tok=5). Pack as pointer so IrPackPtrArg
                     * delivers it and BabycGuestResolveFuncValue can see it. */
                    struct Value *TV = IrGlobalIdentSoft(LinParser, A->Ident);
                    {
                        CallArgIsPtr[CallN][ai] = 1;
                        CallStrOn[CallN][ai] = 1;
                        CallStrBits[CallN][ai] = (long long)(intptr_t)TV;
                    }
                }
                else if (A->Kind == IrExIdent)
                {
                    /* Prefer declared type (TypeFlags) over runtime frame:
                     * body is lowered at entry before assignments run, so
                     * LocalIsPtr may still be 0 for char *s = "...". */
                    int fr_ptr = 0;
                    if (A->LocalSlot >= 0 && IrCallDepth > 0
                        && A->LocalSlot < IR_CALL_MAX_LOCALS)
                        fr_ptr = (IrCallStack[IrCallDepth - 1]->LocalIsPtr[A->LocalSlot]
                            || IrCallStack[IrCallDepth - 1]->LocalArraySize[A->LocalSlot] > 0);
                    if ((A->TypeFlags & IR_TF_PTR) || fr_ptr)
                        CallArgIsPtr[CallN][ai] = 1;
                    else
                        CallArgIsPtr[CallN][ai] = 0;
                }
                else if ((A->TypeFlags & IR_TF_PTR) && A->Kind != IrExCall)
                    CallArgIsPtr[CallN][ai] = 1;
                else if (A->Kind == IrExIndex && A->Left != NULL
                         && A->Left->Kind == IrExIdent
                         && A->Left->LocalSlot >= 0
                         && A->Left->LocalSlot < IR_CALL_MAX_LOCALS)
                {
                    int slot = A->Left->LocalSlot;
                    /* GUARD: dual-read dimensions */
                    int nd = IrCompLocalNDimsOrFrame(slot);
                    if (nd >= 2)
                        CallArgIsPtr[CallN][ai] = 1; /* multidim row decay */
                }
            }
        }
        /* Host: Const strings; global array Ident; argv[i]; partial Index decay. */
        if (is_host)
        {
            for (ai = 0; ai < E->ArgCount; ai++)
            {
                IrExpr *A = E->Args[ai];
                if (A == NULL) { CallArgIsPtr[CallN][ai] = 0; continue; }
                if (A->Kind == IrExConst
                    && (A->Op == TokenStringConstant || (A->TypeFlags & IR_TF_PTR)))
                    CallArgIsPtr[CallN][ai] = 1;
                else if (A->Kind == IrExIdent
                         && (A->LocalSlot >= 0 || A->ParamSlot >= 0)
                         && (A->TypeFlags & IR_TF_PTR))
                    /* local/param array or pointer Ident → blob/addr in reg */
                    CallArgIsPtr[CallN][ai] = 1;
                else if (A->Kind == IrExIdent
                         && A->LocalSlot < 0 && A->ParamSlot < 0
                         && (A->TypeFlags & IR_TF_PTR))
                    CallArgIsPtr[CallN][ai] = 1;
                else if (A->Kind == IrExIndex && A->Left != NULL
                         && A->Left->Kind == IrExIdent
                         && A->Left->ParamSlot >= 0
                         && (A->ElemSize == (int)sizeof(void *)
                             || ((A->TypeFlags & IR_TF_PTR)
                                 && A->ElemSize != A->Left->ElemSize)))
                    CallArgIsPtr[CallN][ai] = 1; /* argv[i] / char** Index → char* */
                else if (A->Kind == IrExIndex)
                {
                    IrExpr *Root = A;
                    int nc = 0, nd = 0;
                    while (Root != NULL && Root->Kind == IrExIndex)
                    { nc++; Root = Root->Left; }
                    if (Root != NULL && Root->Kind == IrExIdent
                        && (Root->ParamSlot >= 0 || Root->LocalSlot >= 0))
                    {
                        /* p[i] scalar vs argv[i] pointer-valued.
                         * ElemSize==sizeof(void*) alone collides with double
                         * (both 8 on LP64). Prefer TF_PTR without TF_FP, or
                         * kind-4 only when base is pointer-array. */
                        if ((A->TypeFlags & IR_TF_FP) && !(A->TypeFlags & IR_TF_PTR))
                            CallArgIsPtr[CallN][ai] = 0; /* double a[i] */
                        else if (A->ElemSize == (int)sizeof(void *)
                                 && (A->TypeFlags & IR_TF_PTR)
                                 && !(A->TypeFlags & IR_TF_FP))
                            CallArgIsPtr[CallN][ai] = 1;
                        else
                            CallArgIsPtr[CallN][ai] = ((A->TypeFlags & IR_TF_PTR)
                                && A->ElemSize != Root->ElemSize) ? 1 : 0;
                    }
                    else if (Root != NULL && Root->Kind == IrExIdent
                        && Root->Ident != NULL && LinParser != NULL)
                    {
                        struct Value *GV = IrGlobalValSoft(LinParser, Root);
                        if (GV != NULL && GV->Typ != NULL && GV->Typ->Base == TypeArray)
                        {
                            struct ValueType *T = GV->Typ;
                            while (T != NULL && T->Base == TypeArray)
                            { nd++; T = T->FromType; }
                            CallArgIsPtr[CallN][ai] = (nc < nd) ? 1 : 0;
                        }
                        else CallArgIsPtr[CallN][ai] = 0;
                    }
                    else CallArgIsPtr[CallN][ai] = 0;
                }
                else if (A->Kind == IrExUnary && A->Op == TokenAmpersand)
                    CallArgIsPtr[CallN][ai] = 1;
                else if (A->Kind == IrExIdent && A->LocalSlot < 0 && A->ParamSlot < 0
                         && IrLinIdentIsFuncDesignator(A))
                    CallArgIsPtr[CallN][ai] = 1; /* function designator (qsort cmp) */
                else if (A->Kind == IrExTernary
                         || (A->Kind == IrExUnary && A->Op == TokenCast))
                    /* z ? z : "?" / (char *)p for %s: same classifier as the
                     * user-call chain above (was forced to int → %s mismatch). */
                    CallArgIsPtr[CallN][ai] = (unsigned char)IrExprPtrValuedTF(A, 0);
                else if (A->Kind == IrExBinOp)
                    CallArgIsPtr[CallN][ai] = (unsigned char)IrExprPtrValuedTF(A, 0);
                else if (A->Kind != IrExConst)
                    CallArgIsPtr[CallN][ai] = 0;
                /* Preserve canonical promoted argument type for the variadic ABI. */
                if (A != NULL)
                {
                    int ab;
                    if (!(A->CanonicalFlags & IR_CF_VALID)) IrCanonicalCheckNode(A);
                    ab = (A->CanonicalFlags & IR_CF_VALID) ? A->CanonicalBase : A->I32;
                    if (CallArgIsPtr[CallN][ai]) ab = TypePointer;
                    else if (ab == TypeChar || ab == TypeShort || ab == TypeUnsignedChar
                        || ab == TypeUnsignedShort || ab == TypeEnum) ab = TypeInt;
#ifndef NO_FP
                    if (!CallArgIsPtr[CallN][ai] && (ab == TypeFP || (A->TypeFlags & IR_TF_FP))) ab = TypeFP;
#endif
                    CallArgTypeBase[CallN][ai] = (unsigned char)ab;
                }
                /* FP arg: IEEE bits in reg → pack as TypeFP for printf %f/%e */
                if (A != NULL && (A->TypeFlags & IR_TF_FP) && !CallArgIsPtr[CallN][ai])
                    CallArgIsFp[CallN][ai] = 1;
                if (CallArgIsFp[CallN][ai] && !CallArgIsPtr[CallN][ai])
                    CallArgTypeBase[CallN][ai] = TypeFP;
            }
        }
        /* Static ABI type metadata applies to user and host calls alike. */
        for (ai = 0; ai < E->ArgCount && ai < IR_LIN_CALL_ARGS; ++ai)
        {
            IrExpr *A = E->Args[ai];
            int ab = TypeInt;
            if (A != NULL)
            {
                if (!(A->CanonicalFlags & IR_CF_VALID)) IrCanonicalCheckNode(A);
                ab = (A->CanonicalFlags & IR_CF_VALID) ? A->CanonicalBase : A->I32;
                /* An aggregate travels as its object address (CallArgIsPtr
                 * packing) but its ABI type stays the struct/union itself. */
                if (ab != TypeStruct && ab != TypeUnion)
                {
                    if (CallArgIsPtr[CallN][ai]) ab = TypePointer;
                    else if (ab == TypeChar || ab == TypeShort || ab == TypeUnsignedChar
                        || ab == TypeUnsignedShort || ab == TypeEnum) ab = TypeInt;
                }
#ifndef NO_FP
                if (!CallArgIsPtr[CallN][ai] && (ab == TypeFP || (A->TypeFlags & IR_TF_FP))) ab = TypeFP;
#endif
                if (A->TypeFlags & IR_TF_FP) CallArgIsFp[CallN][ai] = 1;
                if (CallArgIsFp[CallN][ai] && !CallArgIsPtr[CallN][ai]) ab = TypeFP;
            }
            CallArgTypeBase[CallN][ai] = (unsigned char)ab;
            /* Aggregates keep their exact type identity; the byte table
             * above can only carry the base. */
            CallArgType[CallN][ai] = (A != NULL && (ab == TypeStruct || ab == TypeUnion))
                ? A->CanonicalType : NULL;
            if ((ab == TypeStruct || ab == TypeUnion) && CallArgType[CallN][ai] == NULL)
                return 0; /* unresolved aggregate type: not lowerable */
        }
        { extern void LinCseInvalAll(void); LinCseInvalAll(); }
        In.Op = IrLinCall; In.Dest = (unsigned char)out;
        In.A = 0; In.B = (unsigned char)E->ArgCount;
        In.Tok = is_host ? 1 : 0; /* Tok=1 → host Intrinsic */
        In.Imm = CallN;
        CallN++;
        if (!Emit(Code, Cap, N, In)) return 0;
        *Dest = out;
        if (getenv("CALL_TRACE")) fprintf(stderr, "[call] success\n");
        return 1;
    }
    if (getenv("CALL_TRACE") && E->Kind == IrExCall) fprintf(stderr, "[call] fallthrough\n");
    return 0;
}


/* peephole extract */
/* Generic indexed-load CSE. Only side-effect-free expressions rooted in an
 * identifiable object are eligible. Stores and calls use LinCseInvalAll(),
 * so aliasing is handled conservatively at the shared memory-effect boundary. */
static unsigned long long LinCseHashMix(unsigned long long h, unsigned long long v)
{
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

static int LinCseHashExprRec(IrExpr *E, unsigned long long *H, int depth)
{
    unsigned long long h;
    const unsigned char *p;
    if (E == NULL || depth > 24) return 0;
    /* Keep the key grammar deliberately small: these node kinds are fully
     * represented by Left/Right plus the scalar fields below. Reject every
     * other form until its complete structural identity is modeled. */
    if (E->Kind != IrExIdent && E->Kind != IrExConst
        && E->Kind != IrExBinOp && E->Kind != IrExUnary
        && E->Kind != IrExIndex) return 0;
    /* Calls, assignments and increment/decrement can change the effective
     * address or memory contents while evaluating an expression. */
    if (E->Kind == IrExUnary && (E->Op == TokenIncrement || E->Op == TokenDecrement))
        return 0;
    h = *H;
    h = LinCseHashMix(h, (unsigned long long)E->Kind + 1ULL);
    h = LinCseHashMix(h, (unsigned long long)(unsigned int)E->Op);
    h = LinCseHashMix(h, (unsigned long long)(unsigned int)E->TypeFlags);
    h = LinCseHashMix(h, (unsigned long long)(unsigned int)E->ElemSize);
    h = LinCseHashMix(h, (unsigned long long)(unsigned int)(E->LocalSlot + 2));
    h = LinCseHashMix(h, (unsigned long long)(unsigned int)(E->ParamSlot + 2));
    if (E->Kind == IrExConst)
        h = LinCseHashMix(h, (unsigned long long)E->ConstVal);
    if (E->Ident != NULL) {
        for (p = (const unsigned char *)E->Ident; *p != 0; p++)
            h = LinCseHashMix(h, *p);
        h = LinCseHashMix(h, 0xffU);
    }
    *H = h;
    if (E->Left != NULL && !LinCseHashExprRec(E->Left, H, depth + 1)) return 0;
    if (E->Right != NULL && !LinCseHashExprRec(E->Right, H, depth + 1)) return 0;
    return 1;
}

static unsigned long long LinCseIndexKey(IrExpr *E)
{
    IrExpr *Root;
    unsigned long long h = 1469598103934675603ULL;
    if (E == NULL || E->Left == NULL
        || !((E->Kind == IrExIndex && E->Right != NULL)
             || (E->Kind == IrExUnary && E->Op == TokenAsterisk)))
        return 0;
    /* Aggregate indexing yields an address/blob, not a scalar load value;
     * it is both unsafe and unhelpful to value-number that result here. */
    if (E->PointeeType != NULL
        && (E->PointeeType->Base == TypeArray
            || E->PointeeType->Base == TypeStruct
            || E->PointeeType->Base == TypeUnion)) return 0;
    if (E->ElemSize > (int)sizeof(void *)) return 0;
    Root = E->Left;
    while (Root != NULL && Root->Kind == IrExIndex) Root = Root->Left;
    if (Root == NULL || Root->Kind != IrExIdent) return 0;
    if (Root->LocalSlot < 0 && Root->ParamSlot < 0 && Root->Ident == NULL) return 0;
    if (!LinCseHashExprRec(E, &h, 0)) return 0;
    /* Reserve zero as the invalid key and include a domain tag. */
    h = LinCseHashMix(h, 0x4c494e494e445843ULL);
    return h == 0 ? 1 : h;
}

int LowerRec(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next, int *Dest)
{
    unsigned long long key;
    int reg, before;
    /* An aggregate lvalue designator (s.m, a[i], *p, global) used as a value
     * is its object address, exactly as on the Tree path (see
     * IrAggregateObjectAddr): lower it as &E, never as a scalar load. */
    if (E != NULL && E->CanonicalType != NULL && IrExprDesignatesAggregateObject(E))
        return LinEmitAddressOf(E, Code, Cap, N, Next, Dest);
    key = LinCseIndexKey(E);
    if (key != 0) {
        extern int LinCseExprFind(unsigned long long key);
        reg = LinCseExprFind(key);
        if (reg >= 0) { *Dest = reg; return 1; }
    }
    before = *N;
    if (!LowerRecImpl(E, Code, Cap, N, Next, Dest)) return 0;
    /* Cache only a genuine final indexed value load, not an address, aggregate
     * base, store result, or a composite member access. */
    if (key != 0 && *N > before && Code[*N - 1].Op == IrLinLoadIndex) {
        extern void LinCseExprStore(unsigned long long key, int reg);
        LinCseExprStore(key, *Dest);
    }
    return 1;
}

/* Shared eligibility of the LoadLocal/…/StoreLocal → AddLocal /
 * AddStoreLocal fusions (IrLinPeephole): a full-width non-pointer,
 * non-array integer slot that is not const-qualified. The fused ops do not
 * carry StoreLocal's read-only check, so a const slot (`const int k; k++`)
 * must keep the unfused form and its diagnostic. Compile-time only. */
static int IrLinFuseScalarSlotOk(int slot, int allow_unsigned)
{
    IrCallFrame *Fr;
    struct ValueType *LT;
    if (IrCallDepth <= 0 || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return 0;
    if (CompLocalIsConst[slot])
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    LT = Fr->LocalFromType[slot] ? Fr->LocalFromType[slot] : Fr->LocalTyp[slot];
    if (Fr->LocalIsPtr[slot] || Fr->LocalArraySize[slot] > 0 || LT == NULL)
        return 0;
    if (LT->Base == TypeInt || LT->Base == TypeLongLong)
        return 1;
    return allow_unsigned
        && (LT->Base == TypeUnsignedInt || LT->Base == TypeUnsignedLongLong);
}

int IrLinPeephole(IrLinInst *Code, int N)
{
    int i, w;
    int map[IR_LIN_STMT_MAX];
    unsigned char target[IR_LIN_STMT_MAX];
    if (N <= 0 || N > IR_LIN_STMT_MAX) return N;
    memset(target, 0, (size_t)N);
    for (i = 0; i < N; i++)
        if ((Code[i].Op == IrLinBrZ || Code[i].Op == IrLinJmp || Code[i].Op == IrLinBrLT || Code[i].Op == IrLinBrCmp)
            && Code[i].Imm >= 0 && Code[i].Imm < N)
            target[(int)Code[i].Imm] = 1;
    /* Pass 1: decide keep/drop (drop Line when not debugging). */
    for (i = 0; i < N; i++)
        map[i] = -1;
    w = 0;
    for (i = 0; i < N; i++)
    {
        if (Code[i].Op == IrLinLine && !BabycDebugOn && !IrExecIsVmProd())
            continue;
        map[i] = w;
        /* Do not fold LoadGlobal + IncG without a real liveness proof:
         * the loaded register is the observable value of postfix ++. */
        /* Fuse LoadG + AddI+1 + StoreG same slot → IncG */
        if (i + 2 < N
            && Code[i].Op == IrLinLoadGlobal
            && Code[i+1].Op == IrLinAddI && Code[i+1].Imm == 1
            && Code[i+1].A == Code[i].Dest
            && Code[i+2].Op == IrLinStoreGlobal
            && Code[i].Imm == Code[i+2].Imm
            && Code[i+2].A == Code[i+1].Dest
            && !(Code[i+1].Op == IrLinLine) /* noop */)
        {
            IrLinInst In;
            In.Op = IrLinIncG; In.Dest = 0; In.A = 0; In.B = 0;
            In.Tok = 0; In.Imm = Code[i].Imm;
            Code[w++] = In;
            map[i] = map[i+1] = map[i+2] = w - 1;
            i += 2;
            continue;
        }
        if (i + 3 < N
            && Code[i].Op == IrLinLoadGlobal
            && Code[i+1].Op == IrLinImm && Code[i+1].Imm == 1
            && Code[i+2].Op == IrLinBin && Code[i+2].Tok == TokenPlus
            && Code[i+3].Op == IrLinStoreGlobal
            && Code[i].Imm == Code[i+3].Imm
            && Code[i+2].A == Code[i].Dest && Code[i+2].B == Code[i+1].Dest
            && Code[i+3].A == Code[i+2].Dest)
        {
            IrLinInst In;
            In.Op = IrLinIncG; In.Dest = 0; In.A = 0; In.B = 0;
            In.Tok = 0; In.Imm = Code[i].Imm;
            Code[w++] = In;
            map[i] = map[i+1] = map[i+2] = map[i+3] = w - 1;
            i += 3;
            continue;
        }
        /* Fuse a validated direct pointer-parameter element copy:
         * LoadIndex src; StoreIndex dst <- loaded value → CopyIndex.
         * Only same-slot, same-width direct p[i] metadata is admitted, and the
         * load result must be dead after the store. */
        if (i + 1 < N && !target[i+1] && IrExecIsVmProd()
            && Code[i].Op == IrLinLoadIndex
            && Code[i+1].Op == IrLinStoreIndex
            && Code[i+1].A == Code[i].Dest
            && Code[i].Dest != Code[i+1].B
            && IrLinRegDeadAfter(Code, N, i + 2, (int)Code[i].Dest))
        {
            int sm = (int)Code[i].Imm, dm = (int)Code[i+1].Imm;
            if (sm >= 0 && sm < IdxN && dm >= 0 && dm < IdxN
                && IdxMetaKind[sm] == 2 && IdxMetaKind[dm] == 2
                && IdxMetaSlot[sm] >= 0 && IdxMetaSlot[sm] == IdxMetaSlot[dm]
                && IdxMetaEs[sm] == IdxMetaEs[dm]
                && IdxMetaN[sm] == IdxMetaN[dm]
                && (IdxMetaEs[sm] == 1 || IdxMetaEs[sm] == 2
                    || IdxMetaEs[sm] == 4 || IdxMetaEs[sm] == 8))
            {
                IrLinInst In;
                In.Op = IrLinCopyIndex; In.Dest = Code[i].Dest;
                In.A = Code[i].A; In.B = Code[i+1].B; In.Tok = 0;
                In.Imm = (long long)sm | ((long long)dm << 5);
                Code[w++] = In;
                if (IrVmProfileOn()) VmProfFuseIndexCopy++;
                map[i] = map[i+1] = w - 1;
                i += 1;
                continue;
            }
        }
        /* Fuse local += local for a common accumulator pattern:
         * LoadLocal target; LoadLocal rhs; Bin(+); StoreLocal target.
         * The RHS load remains because it produces the operand register.
         * Skip when uninit checks are on: the fused form drops the LHS
         * LoadLocal diagnostic (795_vm_fused_uninit_guard). */
        if (i + 3 < N && !target[i] && !target[i+1] && !target[i+2] && !target[i+3]
            && IrExecIsVmProd() && !IrUninitChecksOn() && IrCallDepth > 0
            && Code[i].Op == IrLinLoadLocal && Code[i].Imm >= 0
            && Code[i].Imm < IR_CALL_MAX_LOCALS
            && Code[i+1].Op == IrLinLoadLocal && Code[i+1].Imm >= 0
            && Code[i+1].Imm < IR_CALL_MAX_LOCALS
            && Code[i+2].Op == IrLinBin && Code[i+2].Tok == TokenPlus
            && Code[i+2].Imm == 0
            && Code[i+2].A == Code[i].Dest && Code[i+2].B == Code[i+1].Dest
            && Code[i+3].Op == IrLinStoreLocal && Code[i+3].Imm == Code[i].Imm
            && Code[i+3].A == Code[i+2].Dest)
        {
            int slot = (int)Code[i].Imm;
            if (IrLinFuseScalarSlotOk(slot, 0))
            {
                IrLinInst Rhs = Code[i+1], In;
                int rhs_pc = w;
                Code[w++] = Rhs;
                In.Op = IrLinAddStoreLocal; In.Dest = Code[i+2].Dest;
                In.A = (unsigned char)slot; In.B = Rhs.Dest;
                In.Tok = TokenPlus; In.Imm = 0;
                Code[w++] = In;
                if (IrVmProfileOn()) VmProfFuseLocalAdd++;
                map[i] = map[i+1] = rhs_pc;
                map[i+2] = map[i+3] = w - 1;
                i += 3;
                continue;
            }
        }
        /* Fuse the already-peepholed form too:
         *   LoadLocal s; AddI +1; StoreLocal s → AddLocal s, +1.
         * Branches into the middle, uninitialized checks, pointers, arrays and
         * narrow types are intentionally excluded. */
        if (i + 2 < N && !target[i+1] && !target[i+2]
            && IrExecIsVmProd() && !IrUninitChecksOn() && IrCallDepth > 0
            && Code[i].Op == IrLinLoadLocal && Code[i].Imm >= 0
            && Code[i].Imm < IR_CALL_MAX_LOCALS
            && Code[i+1].Op == IrLinAddI && Code[i+1].Imm == 1
            && Code[i+1].A == Code[i].Dest
            && Code[i+2].Op == IrLinStoreLocal
            && Code[i+2].Imm == Code[i].Imm
            && Code[i+2].A == Code[i+1].Dest)
        {
            int slot = (int)Code[i].Imm;
            if (IrLinFuseScalarSlotOk(slot, 1))
            {
                IrLinInst In;
                In.Op = IrLinAddLocal; In.Dest = Code[i+1].Dest;
                In.A = (unsigned char)slot; In.B = 0; In.Tok = 0; In.Imm = 1;
                Code[w++] = In;
                if (IrVmProfileOn()) VmProfFuseLocalAdd++;
                map[i] = map[i+1] = map[i+2] = w - 1;
                i += 2;
                continue;
            }
        }
        /* Fuse the canonical scalar induction update:
         *   LoadLocal s; Imm 1; Bin(+); StoreLocal s
         * into AddLocal s, +1.  This is deliberately narrow: only an
         * initialized-check-free compilation, a known non-pointer int/uint
         * or long-long slot, and exact register/dataflow matches are accepted.
         * Other types and debug/uninitialized-check builds retain the original
         * instructions.  All four old PCs map to the fused instruction so
         * branch targets remain valid after compaction. */
        if (i + 3 < N && !target[i+1] && !target[i+2] && !target[i+3]
            && IrExecIsVmProd() && !IrUninitChecksOn() && IrCallDepth > 0
            && Code[i].Op == IrLinLoadLocal && Code[i].Imm >= 0
            && Code[i].Imm < IR_CALL_MAX_LOCALS
            && Code[i+1].Op == IrLinImm && Code[i+1].Imm == 1
            && Code[i+2].Op == IrLinBin && Code[i+2].Tok == TokenPlus
            && Code[i+2].Imm == 0
            && Code[i+2].A == Code[i].Dest
            && Code[i+2].B == Code[i+1].Dest
            && Code[i+3].Op == IrLinStoreLocal
            && Code[i+3].Imm == Code[i].Imm
            && Code[i+3].A == Code[i+2].Dest)
        {
            int slot = (int)Code[i].Imm;
            if (IrLinFuseScalarSlotOk(slot, 1))
            {
                IrLinInst In;
                In.Op = IrLinAddLocal; In.Dest = Code[i+2].Dest;
                In.A = (unsigned char)slot; In.B = 0; In.Tok = 0; In.Imm = 1;
                Code[w++] = In;
                if (IrVmProfileOn()) VmProfFuseLocalAdd++;
                map[i] = map[i+1] = map[i+2] = map[i+3] = w - 1;
                i += 3;
                continue;
            }
        }
        /* LoadX; Imm k; Bin+/- → LoadX; AddI */
        if (i + 2 < N
            && (Code[i].Op == IrLinLoadLocal || Code[i].Op == IrLinLoadGlobal)
            && Code[i+1].Op == IrLinImm
            && Code[i+1].Tok < 2 /* wide (2/3) and local-address (4) immediates are slot indices, not values */
            && Code[i+2].Op == IrLinBin
            && (Code[i+2].Tok == TokenPlus || Code[i+2].Tok == TokenMinus)
            && Code[i+2].A == Code[i].Dest && Code[i+2].B == Code[i+1].Dest)
        {
            long long imm = Code[i+1].Imm;
            if (Code[i+2].Tok == TokenMinus) imm = -imm;
            Code[w++] = Code[i];
            {
                IrLinInst In;
                In.Op = IrLinAddI; In.Dest = Code[i+2].Dest;
                In.A = Code[i].Dest; In.B = 0; In.Tok = 0; In.Imm = imm;
                Code[w++] = In;
            }
            map[i] = w - 2;
            map[i+1] = map[i+2] = w - 1;
            i += 2;
            continue;
        }
        Code[w++] = Code[i];
    }
    /* Pass 2: retarget branches to new indices */
    for (i = 0; i < w; i++)
    {
        if (Code[i].Op == IrLinBrZ || Code[i].Op == IrLinJmp || Code[i].Op == IrLinBrLT || Code[i].Op == IrLinBrCmp)
        {
            int t = (int)Code[i].Imm;
            if (t >= 0 && t < N)
            {
                int k;
                for (k = t; k < N; k++)
                    if (map[k] >= 0) { Code[i].Imm = map[k]; break; }
                if (k >= N)
                    Code[i].Imm = w;
            }
        }
    }
    /* Pass 3: drop unreachable after Ret (e.g. Jmp following Ret). */
    {
        unsigned char live[IR_LIN_STMT_MAX];
        int q[IR_LIN_STMT_MAX], qh = 0, qt = 0, nw;
        if (w > 0 && w <= IR_LIN_STMT_MAX)
        {
            for (i = 0; i < w; i++) live[i] = 0;
            q[qt++] = 0; live[0] = 1;
            while (qh < qt)
            {
                int pc = q[qh++];
                IrLinInst *In = &Code[pc];
                if (In->Op == IrLinRet)
                    continue;
                if (In->Op == IrLinJmp)
                {
                    int t = (int)In->Imm;
                    if (t >= 0 && t < w && !live[t]) { live[t] = 1; q[qt++] = t; }
                    continue;
                }
                if (In->Op == IrLinBrZ || In->Op == IrLinBrLT || In->Op == IrLinBrCmp)
                {
                    int t = (int)In->Imm;
                    if (t >= 0 && t < w && !live[t]) { live[t] = 1; q[qt++] = t; }
                    if (pc + 1 < w && !live[pc + 1]) { live[pc + 1] = 1; q[qt++] = pc + 1; }
                    continue;
                }
                if (pc + 1 < w && !live[pc + 1]) { live[pc + 1] = 1; q[qt++] = pc + 1; }
            }
            /* compact */
            for (i = 0; i < w; i++) map[i] = -1;
            nw = 0;
            for (i = 0; i < w; i++)
                if (live[i]) { map[i] = nw; Code[nw++] = Code[i]; }
            for (i = 0; i < nw; i++)
            {
                if (Code[i].Op == IrLinBrZ || Code[i].Op == IrLinJmp || Code[i].Op == IrLinBrLT || Code[i].Op == IrLinBrCmp)
                {
                    int t = (int)Code[i].Imm;
                    if (t >= 0 && t < w && map[t] >= 0)
                        Code[i].Imm = map[t];
                }
            }
            w = nw;
        }
    }
    return w;
}

