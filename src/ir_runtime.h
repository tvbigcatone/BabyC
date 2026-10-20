/* ir_runtime.h - B.3: tight runtime surface for Body-IR execution
 *
 * Goal: ir_exec.c (and future backends) depend on this + ir.h rather than
 * the full interpreter.h for frame/local truth.
 *
 * B.1 + B.2 + B.1c + B.3-pack CHIUSO 2026-09-29 (criterion 5.1):
 * B.1 + B.2 COMPLETE (criterion 5.1):
 *   On LocalSlot/ParamSlot >= 0 paths, VariableGet is not used for load/resolve.
 *   Residual Gets are annotated global-only or shadow-bridge (dual-write only).
 *
 * Done:
 *   B.1.0-B.1.2 - frame truth, IrFrameResolveIdent, local Get = 0 on hot path
 *   B.1.3       - no VariableDefine for scalar/ptr/fptr/struct Body-IR locals
 *   B.2.1-B.2.3 - resolve/load/store/index from LocalTyp/LocalVal
 *   B.3 p1-p3   - this header; IrPackIntArg/IrPackPtrArg from frame types
 *   Dual-write  - ExpressionInitMarkAddr only if VariableDefined + ElemInit
 *   B.1c        - BABYC_IR_SHADOW=0 disables dual-write (default ON)
 *
 * --- Bridge still required from interpreter.h (ir_exec.c) ---
 *   VariableGet / VariableDefined  - globals, optional shadow, func resolve
 *   VariableDefine                 - token-body params; non-slot scalar fallback
 *   VariableAllocValueFromType     - Intrinsic ParamArray (ABI needs Value*)
 *   HeapPush/Pop/AllocStack        - call frames for Intrinsic/token
 *   ExpressionCoerce* / AssignInt  - Intrinsic arg coerce
 *   ExpressionInitMarkAddr         - dual-write when Variable shadow exists
 *   ProgramFail / TypeAdd / Lex*   - errors, pointer types, fast-for
 *
 * Future: shrink ir_bridge.h (still pulls interpreter.h for types).
 * Stdio printf/scanf both use Param[] indexing (no contiguous Value walk).
 */
#ifndef BABYC_IR_RUNTIME_H
#define BABYC_IR_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>


#include "ir.h"

/* Forward decls - avoid pulling interpreter.h here when possible */
struct ParseState;
struct Value;
struct ValueType;

typedef struct FastForCond {
    int Active;
    char *VarName;
    enum LexToken Op;
    int BoundIsVar;
    long long Bound;
    char *BoundVarName;
    int VarSlot;
    int BoundSlot;
    struct Value *CachedVar;   /* F3: global cond left */
    struct Value *CachedBound; /* F3: global cond bound */
} FastForCond;

typedef struct FastForInc {
    int Active;
    char *VarName;
    int VarSlot;
    struct Value *CachedVar;   /* F3: global induction */
} FastForInc;

#define IR_CALL_MAX_DEPTH 4096
#define IR_CALL_MAX_PARAMS 16 /* = IR_LIN_CALL_ARGS and tree intrinsic limit */
#define IR_CALL_MAX_LOCALS 128

typedef struct IrCallAbiSlot {
    struct ValueType *Type;
    union {
        long long I;
        unsigned long long U;
        double D;
        void *P;
    } Payload;
} IrCallAbiSlot;

typedef struct IrCallFrame {
    char *ParamName[IR_CALL_MAX_PARAMS];
    long long ParamVal[IR_CALL_MAX_PARAMS];
    char ParamIsPtr[IR_CALL_MAX_PARAMS];
    struct ValueType *ParamTyp[IR_CALL_MAX_PARAMS];
    int NumParams;
    /* Current function's complete variadic ABI payload; points into the
     * synchronous caller's argument vector and is valid until return. */
    IrCallAbiSlot *CallAbiArguments;
    IrCallAbiSlot CallAbiStorage[IR_CALL_MAX_PARAMS + 32];
    int CallAbiArgumentCount;
    int CallAbiFixedArgumentCount;
    int CallAbiIsVariadic;
    long long LocalVal[IR_CALL_MAX_LOCALS];
    char LocalInit[IR_CALL_MAX_LOCALS];
    char LocalIsStatic[IR_CALL_MAX_LOCALS];
    int LocalStaticLine[IR_CALL_MAX_LOCALS];
    char LocalIsConst[IR_CALL_MAX_LOCALS];
    char *LocalName[IR_CALL_MAX_LOCALS];
    char LocalIsPtr[IR_CALL_MAX_LOCALS];
    char LocalIsFptr[IR_CALL_MAX_LOCALS];
    char LocalElemIsPtr[IR_CALL_MAX_LOCALS];
    char LocalOwned[IR_CALL_MAX_LOCALS];
    unsigned char *LocalInitMap[IR_CALL_MAX_LOCALS];
    int LocalElemSize[IR_CALL_MAX_LOCALS];
    unsigned short LocalPointeeSize[IR_CALL_MAX_LOCALS]; /* sizeof(*T), 0 if unknown/too large */
    struct ValueType *LocalFromType[IR_CALL_MAX_LOCALS];
    struct ValueType *LocalTyp[IR_CALL_MAX_LOCALS]; /* B.1 frame type of truth */
    int LocalArraySize[IR_CALL_MAX_LOCALS];
    unsigned char LocalNDims[IR_CALL_MAX_LOCALS];
    int LocalDims[IR_CALL_MAX_LOCALS][IR_MAX_ARRAY_DIMS];
    unsigned LocalStackCharge[IR_CALL_MAX_LOCALS];
    int NumLocals;
    int NumOwnedLocals; /* exact count maintained by IrFrameSetLocalOwned */
    unsigned char LocalExtentKnown;
    long long ReturnVal;
    int HasReturn;
    char CompactClear;
    /* G5: bump arena for small array/struct payloads + init maps */
    char *Arena;
    unsigned ArenaUsed;
    unsigned ArenaCap;
} IrCallFrame;

/* Materialize the compact IR call ABI in typed slots owned by the callee
 * frame.  The legacy scalar evaluator transports register bits as long long;
 * callers with richer expression metadata can replace Type/Payload before
 * entering the body. */
static inline void IrCallAbiBindSlots(IrCallFrame *F, const long long *Values,
    struct ValueType **ArgTypes, int Count, struct ValueType **FixedTypes, int FixedCount)
{
    int i, n = Count;
    if (n > IR_CALL_MAX_PARAMS + 32) n = IR_CALL_MAX_PARAMS + 32;
    F->CallAbiArguments = F->CallAbiStorage;
    for (i = 0; i < n; ++i) {
        struct ValueType *T = (i < FixedCount && FixedTypes != NULL)
            ? FixedTypes[i] : (ArgTypes != NULL ? ArgTypes[i] : NULL);
        long long V = Values != NULL ? Values[i] : 0;
        F->CallAbiStorage[i].Type = T;
        if (T != NULL && T->Base == TypePointer)
            F->CallAbiStorage[i].Payload.P = (void *)(intptr_t)V;
#ifndef NO_FP
        else if (T != NULL && T->Base == TypeFP)
            memcpy(&F->CallAbiStorage[i].Payload.D, &V, sizeof(V));
#endif
        else if (T != NULL && (T->Base == TypeUnsignedInt
            || T->Base == TypeUnsignedLong || T->Base == TypeUnsignedLongLong
            || T->Base == TypeUnsignedChar || T->Base == TypeUnsignedShort))
            F->CallAbiStorage[i].Payload.U = (unsigned long long)V;
        else
            F->CallAbiStorage[i].Payload.I = V;
    }
}

extern size_t IrStackChargeBytes;
extern IrCallFrame *IrCallStack[IR_CALL_MAX_DEPTH];
extern int IrCallDepth;
extern const char *IrDebugFunc[IR_CALL_MAX_DEPTH];
extern struct Value *IrCurrentFunc[IR_CALL_MAX_DEPTH];
/* Record function name for bt after IrFramePush. Hint wins if non-NULL. */
/* 0/1 cached at first query; never getenv on hot path after. */
int BabycDebugEnabled(void);
void IrDebugSetFunc(struct ParseState *Parser, struct Value *FuncValue, const char *Hint);
void IrLowerOnFramePush(void);
void IrLowerOnFramePop(void);
extern char *IrGotoTarget;

/* D1: frame lifecycle (impl in ir_frame.c) */
IrCallFrame *IrFramePush(struct ParseState *Parser);
void IrFramePop(void);
/* Native guest-thread exits must restore the global Linear execution controls
 * normally restored by FnCodeRun epilogues. */
typedef struct IrLinThreadContext {
    struct ParseState *Parser;
    int StepAbort;
    int RetSeen;
    int Div0;
    int ExecDepth;
    int BabycExecDepth;
    int SkippedCall;
    long long RetVal;
    unsigned char CallSkip[128];
} IrLinThreadContext;
void IrLinThreadContextCapture(IrLinThreadContext *context);
void IrLinThreadContextRestore(const IrLinThreadContext *context);
/* Save/restore the linear execution frame stack at native guest-thread exits. */
int LinFrameDepth(void);
void LinFrameRestoreDepth(int depth);
void IrFrameFreeOwned(IrCallFrame *F);
/* Shared frame-local metadata contract: record slots and obtain a safe scan extent. */
void IrFrameNoteLocalSlot(IrCallFrame *F, int Slot);
void IrFrameSetLocalExtent(IrCallFrame *F, int Extent);
void IrFrameMarkAddressInitialized(struct ParseState *Parser, void *Addr, int Size);
void IrFrameSetLocalOwned(IrCallFrame *F, int Slot, int Owned);
int IrFrameLocalExtent(const IrCallFrame *F);
void *IrFrameArenaAlloc(IrCallFrame *F, size_t n);
IrCallFrame *IrTokenCallSaveFrames(int Depth);
void IrTokenCallRestoreFrames(IrCallFrame *Copy, int Depth);

/* Body-IR static local persist (impl in ir_frame.c) */
typedef struct IrStaticLocalEntry {
    char Name[64];
    int Line;
    long long Val;
    int Inited;
} IrStaticLocalEntry;
IrStaticLocalEntry *IrStaticLocalGet(const char *Name, int Line, int Create);

/* Pure ops (D1 step 2 — impl in ir_ops.c) */
long long IrApplyBinOpFlags(struct ParseState *Parser, enum LexToken Op,
                            long long L, long long R,
                            int Unsigned, int Wide, int SourceLine);
long long IrApplyBinOp(struct ParseState *Parser, enum LexToken Op,
                       long long L, long long R, IrExpr *E);
long long IrApplyUnary(enum LexToken Op, long long L);
long long IrNarrowToLocalType(IrCallFrame *Fr, int Slot, long long V);
long long IrNarrowToBaseType(enum BaseType Base, long long V);
int IrTypeIsUnsignedBase(enum BaseType B);


enum BaseType IrIntegerPromotionBase(enum BaseType B);
enum BaseType IrUsualArithmeticCommonIntegerBase(enum BaseType L, enum BaseType R);
long long IrValueAsInt64(struct Value *Val);
/* Type of expr (slot/param/global); impl in ir_exec.c */
struct ValueType *IrExprType(struct ParseState *Parser, IrExpr *E);

/* Call / FuncMeta (D1 step 4 — impl in ir_call.c) */
typedef struct IrFuncMeta {
    struct Value *Func;
    const char *Name; /* cached global key; avoids GlobalTable scan */
    signed char MemoElig;
    signed char MonoElig;
    signed char VmTreeSkip; /* small non-recursive body is cheaper on tree from depth 0 */
    signed char IsLeaf;
    signed char TplReady;
    signed char IsThinKernel;
    short TplNLocals;
    unsigned char SlotKind[IR_CALL_MAX_LOCALS];
    unsigned char SlotWidth[IR_CALL_MAX_LOCALS];
    IrExpr *LeafExpr;
    IrStmt *LeafStmt;
} IrFuncMeta;
IrFuncMeta *IrFuncMetaGet(struct Value *F);
void IrMetaSeedFromBody(IrFuncMeta *Meta, void *BodyIr);
int IrIsLeafReturn(IrStmt *Body, IrExpr **RetExprOut, IrStmt **RetStmtOut);
struct Value *IrPackAlloc(struct ParseState *Parser, struct ValueType *Typ, int DataSize);
struct Value *IrPackReturnValue(struct ParseState *Parser, struct ValueType *Typ);
struct Value *IrPackIntArg(struct ParseState *Parser, long long V, IrExpr *A);
struct Value *IrPackPtrArg(struct ParseState *Parser, long long Bits, IrExpr *A);
void IrExecuteFunctionBodyNoArgs(struct ParseState *Parser, struct Value *FuncValue);

int IrMonoIsEligible(void *BodyIr, int NumParams);
int IrFuncUsesFP(struct Value *FuncValue);
long long IrMonoRun(struct Value *Self, long long *P, int NP);
int IrMemoEligible(struct Value *FuncValue, int ArgCount);
int IrMemoLookup(struct Value *Func, long long *Args, int N, long long *Out);
void IrMemoStore(struct Value *Func, long long *Args, int N, long long Result);
int IrCollectLocals(IrStmt *S, const char **Names, int Cap, int N);
int IrBodyTouchesGlobal(struct ParseState *Parser, IrStmt *S, struct Value *Self, const char **LocalNames, int NLocals);


/* Memory primitives (D1 step 3 — impl in ir_mem.c) */
unsigned long long IrBitMask(int Width);
long long IrBitExtract(const void *Container, int BitOffset, int BitWidth, int IsSigned);
void IrBitInsert(void *Container, int BitOffset, int BitWidth, long long Value);
int IrPtrElemSizeOf(IrExpr *E);
long long IrLoadPtrElem(int ElemBase, void *Addr);
void IrStorePtrElem(struct ParseState *Parser, int ElemBase, void *Addr, long long V);
/* Zero-cost scalar writes (header inline). Safe on cold Decl paths and
 * if ever used from hot code — no call boundary. */
static inline void IrWriteScalarBase(void *addr, enum BaseType B, long long cv)
{
    if (addr == NULL) return;
    if (B == TypeChar || B == TypeUnsignedChar)
        *(unsigned char *)addr = (unsigned char)cv;
    else if (B == TypeShort || B == TypeUnsignedShort)
        *(unsigned short *)addr = (unsigned short)cv;
    else if (B == TypeLong || B == TypeUnsignedLong)
        *(long *)addr = (long)cv;
    else if (B == TypeLongLong || B == TypeUnsignedLongLong)
        *(long long *)addr = cv;
    else if (B == TypeFP)
    {
#ifndef NO_FP
        union { long long l; double d; } u;
        u.l = cv;
        *(double *)addr = u.d;
#endif
    }
    else if (B == TypePointer)
        /* Pointer member/element (struct { char *s; } x = { "lit" }).
         * Previously skipped, so brace-init left the pointer NULL. */
        *(void **)addr = (void *)(intptr_t)cv;
    else if (B != TypeStruct && B != TypeUnion && B != TypeArray
             && B != TypeFunction)
        *(int *)addr = (int)cv;
}
static inline void IrWriteMemberScalar(void *addr, struct Value *MV, long long cv)
{
    enum BaseType B;
    if (addr == NULL || MV == NULL) return;
    if (MV->BitWidth > 0)
    {
        IrBitInsert(addr, MV->BitOffset, MV->BitWidth, cv);
        return;
    }
    B = MV->Typ ? MV->Typ->Base : TypeInt;
    if (B == TypeFP && MV->Typ != NULL && MV->Typ->Sizeof == (int)sizeof(float))
    {
#ifndef NO_FP
        union { long long l; double d; } u;
        u.l = cv;
        *(float *)addr = (float)u.d;
#endif
        return;
    }
    IrWriteScalarBase(addr, B, cv);
}

/* ---- P0 zero-cost shared helpers (header inline) ----
 * GUARD: prefer these over re-coding cascades in stmt/lin/eval/emit. */

/* Peel TypeArray layers → leaf type (or NULL). */
static inline struct ValueType *IrTypePeelArray(struct ValueType *T)
{
    int guard = 0;
    while (T != NULL && T->Base == TypeArray && T->FromType != NULL && guard++ < 16)
        T = T->FromType;
    return T;
}

/* Frame stores a compact 16-bit pointee stride. Values outside that range
 * stay unknown there and must be resolved from the type/compile metadata. */
#define IR_FRAME_POINTEE_SIZE_MAX 65535
static inline unsigned short IrFramePointeeSizeValue(int size)
{
    return (size > 0 && size <= IR_FRAME_POINTEE_SIZE_MAX)
        ? (unsigned short)size : (unsigned short)0;
}

/* Canonical frame accessors for pointee stride. Keep reads/writes through
 * these helpers so the 16-bit storage policy is applied consistently. */
static inline int IrFrameGetPointeeSize(const IrCallFrame *Fr, int slot)
{
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return 0;
    return (int)Fr->LocalPointeeSize[slot];
}

static inline void IrFrameSetPointeeSize(IrCallFrame *Fr, int slot, int size)
{
    if (Fr == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS)
        return;
    Fr->LocalPointeeSize[slot] = IrFramePointeeSizeValue(size);
}

/* Write integer-ish value by element size (1/2/4/8). Cold Decl + hot ok. */
static inline void IrWriteByElemSize(void *addr, int es, long long cv)
{
    if (addr == NULL || es <= 0) return;
    if (es >= (int)sizeof(long long))
        *(long long *)addr = cv;
    else if (es >= (int)sizeof(long))
        *(long *)addr = (long)cv;
    else if (es >= (int)sizeof(int))
        *(int *)addr = (int)cv;
    else if (es >= (int)sizeof(short))
        *(short *)addr = (short)cv;
    else
        *(unsigned char *)addr = (unsigned char)cv;
}

/* Resolve Decl element size cascade (compile + DECL_ARRAY Step). */
static inline int IrResolveDeclElemSize(int decl_es, struct ValueType *FromType)
{
    int es = decl_es > 0 ? decl_es : 0;
    struct ValueType *FT;
    if (es > 0)
        return es;
    FT = FromType;
    /* DeclFromType may itself be an array type in reconstructed declarations.
     * Resolve the leaf before consulting storage metadata; otherwise array
     * Sizeof (or the generic int fallback) can become the indexed stride. */
    FT = IrTypePeelArray(FT);
    if (FT != NULL && FT->Base == TypeFP)
        return TypeFPStorageSize(FT);
    if (FT != NULL && (FT->Base == TypeStruct || FT->Base == TypeUnion)
        && FT->Sizeof > 0)
        return FT->Sizeof;
    if (FT != NULL && FT->Sizeof > 0 && FT->Sizeof <= 8)
        return FT->Sizeof;
    if (FT != NULL && FT->FromType != NULL)
    {
        struct ValueType *Elem = IrTypePeelArray(FT->FromType);
        if (Elem != NULL && Elem->Base == TypeFP)
            return TypeFPStorageSize(Elem);
        if (Elem != NULL && Elem->Sizeof > 0 && Elem->Sizeof <= 8
            && FT->Base != TypeStruct && FT->Base != TypeUnion)
            return Elem->Sizeof;
    }
    return (int)sizeof(int);
}

/* Mark InitMap [0,n) fully initialized. */
static inline void IrMarkInitMapRange(unsigned char *imap, int n)
{
    int i;
    if (imap == NULL || n <= 0) return;
    for (i = 0; i < n; i++)
        imap[i] = 1;
}

/* Load integer-ish value by element size. is_unsigned selects zero-extend. */
static inline long long IrLoadByElemSize(const void *addr, int es, int is_unsigned)
{
    if (addr == NULL || es <= 0) return 0;
    if (es == 1)
        return is_unsigned
            ? (long long)*(const unsigned char *)addr
            : (long long)*(const signed char *)addr;
    if (es == 2)
        return is_unsigned
            ? (long long)*(const unsigned short *)addr
            : (long long)*(const short *)addr;
    if (es == 4)
        return is_unsigned
            ? (long long)*(const unsigned int *)addr
            : (long long)*(const int *)addr;
    if (es >= 8)
        return is_unsigned
            ? (long long)*(const unsigned long long *)addr
            : *(const long long *)addr;
    return (long long)*(const int *)addr;
}

/* Map element size → BaseType (signed/unsigned). */
static inline enum BaseType IrBaseFromElemSize(int es, int is_unsigned)
{
    if (es == 1)
        return is_unsigned ? TypeUnsignedChar : TypeChar;
    if (es == 2)
        return is_unsigned ? TypeUnsignedShort : TypeShort;
    if (es == 8)
        return is_unsigned ? TypeUnsignedLongLong : TypeLongLong;
    return is_unsigned ? TypeUnsignedInt : TypeInt;
}

/* Bounds: n<=0 means unknown size (skip). Zero-cost hot predicate. */

/* ========== Unified execution config (P0.26) ==========
 * Three orthogonal axes (preferred env):
 *   BABYC_PATH=token|tree|linear|bytecode   (default tree)
 *   BABYC_ACCEL=0|1                         (default 1; shape+kernel)
 *   BABYC_STRICT=0|1                        (default 0; no tree residual)
 *
 * Legacy aliases (still honoured):
 *   BABYC_NO_IR=1              → PATH=token
 *   BABYC_EXEC=tree|vm|vm_strict|both|ir
 *     vm → PATH=linear;  vm_strict → PATH=linear + STRICT=1
 *   BABYC_BYTECODE_VM=1        → PATH=bytecode (with linear base)
 *   BABYC_VM_STRICT=1          → STRICT=1
 *   BABYC_NO_SHAPE_FP / BABYC_NO_KERNEL → ACCEL=0 when either set and ACCEL unset
 */
typedef enum {
    BABYC_PATH_TOKEN = 0,
    BABYC_PATH_TREE = 1,
    BABYC_PATH_LINEAR = 2,
    BABYC_PATH_BYTECODE = 3
} BabycPathKind;

void BabycExecConfigInit(void);          /* optional; getters self-init */
BabycPathKind BabycPathGet(void);
int BabycAccelOn(void);                  /* shape FP + kernels */
int BabycStrictOn(void);                 /* no silent tree residual */
/* Compatibility wrappers (same as before): */
int IrExecIsVmProd(void);                /* PATH is linear or bytecode */
int IrVmStrictOn(void);                  /* policy: reject any residual Tree body */
int IrVmLinearFeaturesOn(void);           /* semantic lowering feature set for Linear/bytecode */

static inline int IrArrayIndexInBounds(long long idx, int n)
{
    if (n <= 0)
        return 1;
    return idx >= 0 && idx < (long long)n;
}

/* H9: byte-map readiness — all [off, off+es) marked init. Zero-cost when imap NULL. */
static inline int IrInitMapBytesReady(const unsigned char *imap, int off, int es, int map_len)
{
    int i;
    if (imap == NULL || es <= 0)
        return 1;
    if (off < 0)
        return 0;
    for (i = 0; i < es; i++)
    {
        int b = off + i;
        if (b >= map_len || imap[b] == 0)
            return 0;
    }
    return 1;
}
/* Cold Decl only — designated + sequential brace pack (tree/linear).
 * GUARD: all struct/union brace-init must go through IrPackStructBraceInit
 * (or the two helpers it calls). Do not re-expand collect/sort/write loops
 * in ir_stmt / ir_lin_exec — that reintroduces dual maintenance. */
/* Top-level members sorted by (offset, bit offset, decl index); union by
 * decl index. Names optional. Cold Decl only (ir_mem.c). */
int IrCollectSortedMembers(struct ValueType *ST, struct Value **MemberList,
                           int *Offsets, char **Names, int cap);
/* Brace-init placement: Out[vi] = target of initializer vi (byte offset
 * from the object base; MV for member writes incl. bitfield/float, else EB
 * for array-member elements). Off < 0 → no target. Returns values consumed.
 * Shared by IrPackStructBraceInit and the linear non-const lowering. */
typedef struct IrBracePlace { int Off; struct Value *MV; enum BaseType EB; } IrBracePlace;
int IrBraceInitPlan(struct ValueType *ST, char **Names, int Count,
                    int narr, int esz, IrBracePlace *Out);
/* Zero-cost scan: any designated .name= in init list? */
static inline int IrBraceInitHasDesignated(char **Names, int Count)
{
    int i;
    if (Names == NULL || Count <= 0)
        return 0;
    for (i = 0; i < Count; i++)
        if (Names[i] != NULL)
            return 1;
    return 0;
}
/* Unified cold entry: designated if any name, else sequential (possibly array).
 * Returns 1 if any value was applied. narr/esz for struct-array sequential. */
int IrPackStructBraceInit(struct ParseState *Parser, void *mem,
                          struct ValueType *ST,
                          const long long *Vals, char **Names,
                          IrExpr **Exprs, int Count,
                          int narr, int esz);
/* Cold Decl: int dst[] = src; / int dst[N] = src; (DeclInitCount==-1).
 * GUARD: tree + linear must call this — no parallel memcpy paths. */
int IrCopyArrayIdentInit(struct ParseState *Parser, IrCallFrame *Fr,
                         void *dst_mem, int n, int es, IrExpr *SrcE,
                         unsigned char *imap);

/* C7: memcpy struct/union blob + optional InitMap full-mark (cold Decl/assign). */
void IrCopyStructBlob(void *dst, const void *src, int sz,
                      unsigned char *imap, int map_len);
/* C8: free previous owned + calloc; set LocalVal/LocalOwned. Returns mem or NULL. */
void *IrAllocOwnedBlob(IrCallFrame *Fr, int slot, size_t nbytes);

int IrFrameIsByteMap(IrCallFrame *Fr, int slot);
int IrFrameInitMapLen(IrCallFrame *Fr, int slot);
void IrFrameMarkBytes(IrCallFrame *Fr, int slot, void *Addr, int Size);


/* Frame resolve (B.1.1) - no VariableGet on slots */
int IrFrameGetByName(const char *Name, long long *Out);
int IrFrameSetByName(const char *Name, long long V);
int IrFrameResolveIdent(struct ParseState *Parser, IrExpr *E,
                        long long *ValOut, void **AddrOut,
                        struct ValueType **TypOut);

void IrExecuteFunctionBody(struct ParseState *Parser, struct Value *FuncValue,
                           struct Value **ParamArray, int ArgCount);
void IrExecuteFunction(struct ParseState *Parser, struct Value *FuncValue,
                       struct Value **ParamArray, int ArgCount);
long long IrEvalExprTree(struct ParseState *Parser, IrExpr *E);
void IrExecuteStmtTree(struct ParseState *Parser, IrStmt *S);
/* Slow path (lowering shadow / debug). Large stack frames live there only. */
long long IrEvalExprHook(struct ParseState *Parser, IrExpr *E);
void IrExecuteStmtHook(struct ParseState *Parser, IrStmt *S);
/* 0 unless BABYC_LOWER/BABYC_EXEC/BABYC_DEBUG: set once at startup (IrHookInit). */
extern int IrHookOn;
void IrHookInit(void);
#if defined(__GNUC__)
#define IR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define IR_UNLIKELY(x) (x)
#endif
static inline long long IrEvalExpr(struct ParseState *Parser, IrExpr *E)
{
    if (IR_UNLIKELY(IrHookOn))
        return IrEvalExprHook(Parser, E);
    return IrEvalExprTree(Parser, E);
}
static inline void IrExecuteStmt(struct ParseState *Parser, IrStmt *S)
{
    if (IR_UNLIKELY(IrHookOn))
    {
        IrExecuteStmtHook(Parser, S);
        return;
    }
    IrExecuteStmtTree(Parser, S);
}


/* Helpers used by ir_eval/ir_stmt (exported from ir_exec during D1-6) */
int IrAddrOf(struct ParseState *Parser, IrExpr *E, void **AddrOut, int *ElemBaseOut, int *ElemSizeOut);
int IrResolvePtr(struct ParseState *Parser, IrExpr *Base, void **AddrOut, int *ElemBaseOut, int *ElemSizeOut);
int IrResolveMember(struct ParseState *Parser, IrExpr *E, void **AddrOut, int *ElemBaseOut);
int IrTypeIsCopyableAggregate(struct ValueType *T);
int IrExprDesignatesAggregateObject(IrExpr *E);
int IrAggregateObjectAddr(struct ParseState *Parser, IrExpr *E, void **AddrOut);
int IrMemberPtrElemSize(struct ParseState *Parser, IrExpr *E);
struct Value *IrGlobalVal(struct ParseState *Parser, IrExpr *E);
struct Value *IrGlobalValSoft(struct ParseState *Parser, IrExpr *E);
struct Value *IrGlobalIdentSoft(struct ParseState *Parser, const char *Ident);
struct Value *IrGlobalCacheFill(struct ParseState *Parser, const char *Ident, struct Value **Cache);
int IrAssignTargetIsStringLiteral(IrExpr *E);
int IrLookupParam(const char *Name, long long *Out);
int IrAssignTargetIsConst(struct ParseState *Parser, IrExpr *Left);
void IrFailReadOnlyAssign(struct ParseState *Parser, int Line, int CharPos);
/* Pointer-valued expression by compile-time stamps (host varargs packing:
 * Pointer vs Integer field, printf %s/%p). String constant, TF_PTR without
 * TF_FP, or a ternary with a pointer-valued arm (z ? z : "?",
 * c ? f() : g() with char *f()). Shared by the tree Intrinsic arg packer
 * and the linear CallArgIsPtr classifier. */
static inline int IrExprPtrValuedTF(const IrExpr *A, int depth)
{
    if (A == NULL || depth > 8 || (A->TypeFlags & IR_TF_FP))
        return 0;
    if ((A->Kind == IrExConst && A->Op == TokenStringConstant)
        || (A->TypeFlags & IR_TF_PTR))
        return 1;
    if (A->Kind == IrExTernary)
        return IrExprPtrValuedTF(A->Right, depth + 1)
            || (A->Args != NULL && A->ArgCount >= 1
                && IrExprPtrValuedTF(A->Args[0], depth + 1));
    return 0;
}
/* Compile-time half of IrAssignTargetIsConst: lvalue whose root object is
 * stamped IR_TF_CONST (const local; a.m / a[i] of a const aggregate).
 * Stores through *p are not the const object itself. Used by fast paths
 * that must not bypass the runtime read-only diagnostic. */
static inline int IrLvalueRootConstTF(const IrExpr *X)
{
    int depth = 0;
    while (X != NULL && depth++ < 32)
    {
        if (X->TypeFlags & IR_TF_CONST)
            return 1;
        if (X->Kind == IrExUnary && X->Op == TokenAsterisk)
            return 0;
        if (X->Kind == IrExMember && X->Op == TokenArrow)
            return 0; /* p->m: pointee, not the pointer object */
        if (X->Kind == IrExMember || X->Kind == IrExIndex)
            X = X->Left;
        else
            return 0;
    }
    return 0;
}
int IrExprIsFP(struct ParseState *Parser, IrExpr *E);
int IrExprYieldsPointer(struct ParseState *Parser, IrExpr *A);
long long IrFPBits(double d);
/* Preserve IEEE bits when FP values cross the IR call boundary. */
#ifndef NO_FP
#define IR_RET_BITS(V) (((V)->Typ != NULL && (V)->Typ->Base == TypeFP) ? IrFPBits((V)->Val->FP) : IrValueAsInt64(V))
#else
#define IR_RET_BITS(V) IrValueAsInt64(V)
#endif
double IrBitsFP(long long l);
/* Inverse of IR_RET_BITS: store IR value bits (IEEE bits for FP, address for
 * pointers, integer otherwise) into a scalar Value of base B. Shared by the
 * Body-IR -> token ReturnValue publish and linear scalar global stores.
 * Returns 0 for non-scalar bases (struct/union/array: caller copies). */
static inline int IrValueStoreBits(struct Value *V, enum BaseType B, long long nv)
{
    switch (B)
    {
        case TypeChar: V->Val->Character = (char)nv; break;
        case TypeUnsignedChar: V->Val->UnsignedCharacter = (unsigned char)nv; break;
        case TypeShort: V->Val->ShortInteger = (short)nv; break;
        case TypeUnsignedShort: V->Val->UnsignedShortInteger = (unsigned short)nv; break;
        case TypeInt: V->Val->Integer = (int)nv; break;
        case TypeUnsignedInt: V->Val->UnsignedInteger = (unsigned int)nv; break;
        case TypeLong: V->Val->LongInteger = (long)nv; break;
        case TypeUnsignedLong: V->Val->UnsignedLongInteger = (unsigned long)nv; break;
        case TypeLongLong: V->Val->LongLongInteger = nv; break;
        case TypeUnsignedLongLong: V->Val->UnsignedLongLongInteger = (unsigned long long)nv; break;
#ifndef NO_FP
        case TypeFP: V->Val->FP = IrBitsFP(nv); break;
#endif
        case TypePointer:
        case TypeFunction:
            V->Val->Pointer = (void *)(intptr_t)nv; break;
        case TypeStruct:
        case TypeUnion:
        case TypeArray:
            return 0;
        default: V->Val->Integer = (int)nv; break;
    }
    return 1;
}
void IrCaretOnIndex(struct ParseState *Parser, IrExpr *E);
int IrFrameLeafInit(IrCallFrame *Fr, int slot, void *Addr, int Size);
void IrMarkAllPtrArgsEscaped(struct ParseState *Parser, IrExpr *Call);
struct Value *IrPackFPArg(struct ParseState *Parser, long long Bits);
void IrHostCoerceParams(struct ParseState *Parser, struct Value *FuncValue,
                        struct Value **ParamArray, int ArgCount);
long long IrCallUserFunc(struct ParseState *Parser, const char *Name, long long *ArgVals, struct ValueType **ArgTypes, int ArgCount);
long long IrCallUserFuncCached(struct ParseState *Parser, struct Value *FuncValue, long long *ArgVals, struct ValueType **ArgTypes, int ArgCount);
int IrTryFuseEval(struct ParseState *Parser, IrExpr *E, long long *Out);
int IrSafeChecksOn(void);
int IrUninitChecksOn(void); /* alias → IrSafeChecksOn */
void IrShadowMarkAddr(struct ParseState *Parser, const char *Ident, void *Addr, int Size);
void IrShadowMarkAddrEx(struct ParseState *Parser, const char *Ident, void *Addr, int Size, int WholeIfNoMap);
struct Value *IrShadowGet(struct ParseState *Parser, const char *Ident);
void IrValueMarkAddr(struct Value *SV, void *Addr, int Size);
void IrValueMarkAll(struct Value *SV);
int IrVarTableLookup(Picoc *pc, const char *Ident, struct Value **Out);
int IrVarTableDefined(Picoc *pc, const char *Ident);
struct Value *IrCalleeLookupOrFail(struct ParseState *Parser, const char *Name,
                                   int Line, int CharPos);
int IrVarTableOutOfScope(Picoc *pc, const char *Ident);
void IrVarTableGet(struct ParseState *Parser, const char *Ident, struct Value **Out);
void IrExecuteBodyFlat(struct ParseState *Parser, IrStmt *S);
void IrClearStickyTree(IrStmt *S);
int IrDispatchForFp(struct ParseState *Parser, IrStmt *S, int k);
int IrProbeForFp(struct ParseState *Parser, IrStmt *S);
int IrStmtTouchesFP(IrStmt *S);
int IrKernelFpEnabledFor(IrStmt *S);
int IrFrameHasReturn(void);
int IrLicmLeadingConstAssigns(struct ParseState *Parser, IrStmt *ForS, IrStmt **BodyList, int BodyN);
int IrModeAllowsContinue(struct ParseState *Parser);
int IrScopeBegin(struct ParseState *Parser, int *Prev);
void IrScopeEnd(struct ParseState *Parser, int ScopeID, int Prev);
void IrStackOverflowSetCaretEx(struct ParseState *Parser, int MinN, int PreferN);
/* Shared tree/linear automatic-object stack budget (ir_stmt.c). */
void IrChargeLocalArrayOrFail(struct ParseState *Parser, IrCallFrame *Fr, int Slot,
                              size_t nbytes, int Line, int CharPos,
                              const char *Ident, int DeclCount);
int IrTryFastPtrScan(struct ParseState *Parser, IrStmt *S);
int IrTryFastPtrScanBound(struct ParseState *Parser, IrStmt *S);
int IrTryFastPtrScanFor(struct ParseState *Parser, IrStmt *S);
int IrTryFastWhileChain(struct ParseState *Parser, IrStmt *S);
int IrTryFastForAfterInit(struct ParseState *Parser, IrStmt *S);
int IrTryFastPtrScanPred(struct ParseState *Parser, IrStmt *S);
int IrTryFastInsertShift(struct ParseState *Parser, IrStmt *S);
int IrTryKernelWhile(struct ParseState *Parser, IrStmt *S);
int IrTryKernelFor(struct ParseState *Parser, IrStmt *S);
int IrTryFastMatmul3(struct ParseState *Parser, IrStmt *S);
int IrTryTightFor(struct ParseState *Parser, IrStmt *S);
int IrTryTightLoopVars(struct ParseState *Parser, IrLoop *Loop);
int IrFrameSlotByName(const char *Name);
int IrMatchSum3Shape(IrStmt *Body, char **OutA, char **OutB, char **OutC);
int IrMatchScoreShape(IrStmt *Body, char **OutA, char **OutB, char **OutC, char **OutOp);
int IrExprCachedGlobal(struct ParseState *Parser, IrExpr *E, struct Value **Out);
int IrExprIdentIsPtr(IrExpr *X, IrCallFrame *F);
int IrTokenFastAllowed(struct ParseState *Parser);
int IrLocalPtrElemIsChar(IrCallFrame *Fr, int pslot);
int IrTryIterCallAccum(struct ParseState *Parser, IrStmt *S);
int IrTryDenseForBody(struct ParseState *Parser, IrStmt *S);
int IrResolveMemberLayout(struct ValueType *ST, const char *Name,
                          int *OffOut, int *EsOut, int *EbOut, int *IsPtrOut);
int IrStructMemberOffset(struct ValueType *ST, const char *Name, int *OffOut, int *BaseOut, int *IsPtrOut);
int IrRelopLL(enum LexToken Op, long long L, long long R, long long *Out);
int IrTryFastPtrWalk(struct ParseState *Parser, IrStmt *S);
int IrTryFastArrayAccum(struct ParseState *Parser, IrStmt *S);
int IrTryFastArrayFill(struct ParseState *Parser, IrStmt *S);
int IrKernelResolveBase(struct ParseState *Parser, IrExpr *Base, IrCallFrame *F, void **PtrOut, int *ElemSizeOut);

extern int IrInLoopBody;
extern char *IrHostStackBase;
extern size_t IrHostStackBudget;
extern int IrHostStackBudgetInited;
extern long long g_fp_thin;
extern long IrFpPtrScanHit;
extern long IrFpPtrScanMiss;
extern long IrFpPtrScanPredHit;
extern long IrFpKernelWhileHit;
extern long IrFpForHit;

/* Cross-TU prototypes (silence implicit declaration warnings). */
int IrKernelStmtOk(IrStmt *S, int depth);
int IrKernelExecStmt(struct ParseState *Parser, IrStmt *S, IrCallFrame *F, int *flow);
void IrKernelGCacheReset(void);
void IrLowerGotoFinished(struct ParseState *Parser);
int IrExprTouchesGlobal(struct ParseState *Parser, IrExpr *E,
                        struct Value *Self, const char **LocalNames, int NLocals);
/* Shape-matched microbench shortcuts (isort insert-shift, string_proc scan).
 * Default ON. BABYC_NO_SHAPE_FP=1 disables (honest noscorciabili tests). */
int IrShapeFpEnabled(void);


/* F4 — VM contract (tree executor and future ir_vm). No new Kind.
 * Ops:  IrApplyBinOp, IrApplyUnary, IrNarrowToLocalType, IrValueAsInt64,
 *       IrTypeIsUnsignedBase
 * Mem:  IrLoadPtrElem, IrStorePtrElem, IrAddrOf, IrResolvePtr, IrBitMask,
 *       IrBitExtract, IrBitInsert, IrAssignTargetIsStringLiteral
 * Bind: IrGlobalVal (expr CachedVal), IrGlobalCacheFill (name cache)
 * Pack: IrPackAlloc, IrPackReturnValue, IrPackIntArg, IrPackPtrArg, IrPackFPArg
 * Frame: IrFramePush/Pop, IrCallStack, IrCallDepth
 * Token path and func resolve stay on the bridge (IrVarTableGet).
 */


/* Host call / binop used by linear Step (ir_lin_exec) */
long long IrCallHostFromRegs(struct ParseState *Parser, struct Value *FuncValue,
    long long *ArgVals, int ArgCount, const unsigned char *IsPtr,
    const unsigned char *IsFp);
long long IrApplyBinOpRaw(struct ParseState *Parser, enum LexToken Op,
    long long Left, long long Right, int LeftUnsigned, int RightUnsigned);

#endif /* BABYC_IR_RUNTIME_H */

int IrTryFastDoCountdown(struct ParseState *Parser, IrStmt *S);
int IrTryFastDoCountdownLight(long long *LLoc, long long *LParam, int nloc, int nparam, IrStmt *S);
