/* F6 emitter. 8-byte word: head (op, dest, a, b, tok) + int32 imm.
 * Imm that does not fit int32 is refused. BABYC_VM=1, default off. */
#include "picoc.h"
#include "ir_runtime.h"
#include "ir_lin.h"
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
static int VmTraceReject(void) {
    static int On = -1;
    if (On < 0) On = (getenv("BABYC_VM_TRACE") != NULL) ? 1 : 0;
    return On;
}
static void VmStoreWrap64(long long *out, unsigned long long bits)
{
    /* Preserve the target's 64-bit two's-complement bit pattern without
     * evaluating an overflowing signed C expression. */
    memcpy(out, &bits, sizeof(*out));
}

static int VmApplyBinOpFast(enum LexToken op, long long l, long long r,
                            int uns, int wide, long long *out)
{
    /* Inline the frequent, non-trapping scalar operators. Keep division,
     * remainder and shifts on the canonical path: their diagnostics and
     * width-specific semantics are subtle. */
    if (op == TokenSlash || op == TokenModulus || op == TokenShiftLeft || op == TokenShiftRight)
        return 0;
    if (!uns && !wide) {
        int a = (int)l, b = (int)r;
        switch (op) {
        case TokenPlus: *out = (int)(a + b); return 1;
        case TokenMinus: *out = (int)(a - b); return 1;
        case TokenAsterisk: *out = (int)(a * b); return 1;
        default: break;
        }
    }
    if (uns && !wide) {
        unsigned int a = (unsigned int)l, b = (unsigned int)r;
        switch (op) {
        case TokenPlus: *out = (unsigned int)(a + b); return 1;
        case TokenMinus: *out = (unsigned int)(a - b); return 1;
        case TokenAsterisk: *out = (unsigned int)(a * b); return 1;
        case TokenEqual: *out = a == b; return 1;
        case TokenNotEqual: *out = a != b; return 1;
        case TokenLessThan: *out = a < b; return 1;
        case TokenGreaterThan: *out = a > b; return 1;
        case TokenLessEqual: *out = a <= b; return 1;
        case TokenGreaterEqual: *out = a >= b; return 1;
        case TokenAmpersand: *out = a & b; return 1;
        case TokenArithmeticOr: *out = a | b; return 1;
        case TokenArithmeticExor: *out = a ^ b; return 1;
        default: return 0;
        }
    }
    switch (op) {
    case TokenPlus:
        if (uns) *out = (long long)((unsigned long long)l + (unsigned long long)r);
        else VmStoreWrap64(out, (unsigned long long)l + (unsigned long long)r);
        return 1;
    case TokenMinus:
        if (uns) *out = (long long)((unsigned long long)l - (unsigned long long)r);
        else VmStoreWrap64(out, (unsigned long long)l - (unsigned long long)r);
        return 1;
    case TokenAsterisk:
        if (uns) *out = (long long)((unsigned long long)l * (unsigned long long)r);
        else VmStoreWrap64(out, (unsigned long long)l * (unsigned long long)r);
        return 1;
    case TokenEqual: *out = l == r; return 1;
    case TokenNotEqual: *out = l != r; return 1;
    case TokenLessThan: *out = uns ? (unsigned long long)l < (unsigned long long)r : l < r; return 1;
    case TokenGreaterThan: *out = uns ? (unsigned long long)l > (unsigned long long)r : l > r; return 1;
    case TokenLessEqual: *out = uns ? (unsigned long long)l <= (unsigned long long)r : l <= r; return 1;
    case TokenGreaterEqual: *out = uns ? (unsigned long long)l >= (unsigned long long)r : l >= r; return 1;
    case TokenAmpersand: *out = l & r; return 1;
    case TokenArithmeticOr: *out = l | r; return 1;
    case TokenArithmeticExor: *out = l ^ r; return 1;
    default: return 0;
    }
}
int IrLowerCallSkipped(int i);
void IrLowerSetRet(long long v);
long long IrApplyBinOpRaw(struct ParseState *, enum LexToken, long long, long long, int, int);
long long IrLinWideImmAt(int);
int IrLinWideImmOn(int);
IrExpr *IrLowerGlobalAt(int i);
struct ParseState *IrLowerParser(void);
void IrLowerRestoreGlobs(void);
IrExpr *IrLowerIndexAt(int i);
struct Value *IrLowerCallFn(int i);
const char *IrLowerCallName(int i);
int IrLowerCallArgN(int i);
int IrLowerCallArgReg(int ci, int ai);
long long IrLowerCallHostProd(int ci, long long *Regs);
int IrLowerCallUserSafeProd(int ci);
int IrLowerCallAggregatePureProd(int ci);
struct ValueType *IrLowerCallLocalType(int slot);
long long IrLowerCallUserProd(int ci, long long *Regs);
extern IrStmt *IrLowerKernelStmt(int i);

typedef struct VmWord {
    unsigned int Head;
    int Imm;
} VmWord;

static int VmOn(void)
{
    static int On = -1;
    if (On < 0)
    {
        const char *e = getenv("BABYC_VM");
        On = (e != NULL && e[0] == '1' && e[1] == '\0') ? 1 : 0;
        if (IrExecIsVmProd())
            On = 1;
    }
    return On;
}

static int Emit(const IrLinInst *Code, int N, VmWord *Out, int Cap)
{
    int i;
    if (N > Cap) return 0;
    for (i = 0; i < N; i++)
    {
        if (Code[i].Imm != (long long)(int)Code[i].Imm) return 0;
        Out[i].Head = (unsigned)Code[i].Op
            | ((unsigned)Code[i].Dest << 8)
            | ((unsigned)Code[i].A << 16)
            | ((unsigned)Code[i].B << 24);
        Out[i].Imm = (int)Code[i].Imm;
    }
    return 1;
}

/* Per-execution-depth production VM state. Reusable static frames avoid both
 * copying the caller's bytecode and allocating buffers on ordinary calls. */
#define VM_PROD_MAX_DEPTH 8
#define VM_PROD_MEM_LOG_CAP 65536
#define VM_PROD_ARRAY_SNAP_CAP 32
struct VmProdMemUndo { void *Addr; int Eb; long long Old; unsigned char *Copy; size_t Bytes; };
struct VmProdArrayUndo { void *Base; size_t Bytes; unsigned char *Copy; };
struct VmProdContext {
    unsigned short TokTab[IR_LIN_STMT_MAX];
    unsigned char FlagTab[IR_LIN_STMT_MAX];
    long long VmImmTab[IR_LIN_STMT_MAX];
    const IrLinInst *CacheCode;
    int CacheN;
    IrLinInst CacheSource[IR_LIN_STMT_MAX];
    VmWord CacheWords[IR_LIN_STMT_MAX];
    unsigned short CacheTok[IR_LIN_STMT_MAX];
    unsigned char CacheFlag[IR_LIN_STMT_MAX];
    long long CacheImm[IR_LIN_STMT_MAX];
    struct VmProdMemUndo *MemLog;
    int MemLogCap, MemCount, MemAbort, TxnOn, HostCommitted;
    void *IdxAddr[32];
    long long IdxSave[32];
    int IdxEb[32];
    unsigned IdxDirty;
    struct Value *Global[32];
    union AnyValue GlobalSave[32];
    unsigned char GlobalInit[32];
    unsigned GlobalDirty;
    struct VmProdArrayUndo ArraySnap[VM_PROD_ARRAY_SNAP_CAP];
    int ArraySnapN;
    size_t ArraySnapBytes;
    int TerminalCall;
};
static struct VmProdContext VmProdContexts[VM_PROD_MAX_DEPTH];
static struct VmProdContext *VmProdCtx = &VmProdContexts[0];
#define TokTab (VmProdCtx->TokTab)
#define FlagTab (VmProdCtx->FlagTab)
#define VmImmTab (VmProdCtx->VmImmTab)
#define VmProdCacheCode (VmProdCtx->CacheCode)
#define VmProdCacheN (VmProdCtx->CacheN)
#define VmProdCacheSource (VmProdCtx->CacheSource)
#define VmProdCacheWords (VmProdCtx->CacheWords)
#define VmProdCacheTok (VmProdCtx->CacheTok)
#define VmProdCacheFlag (VmProdCtx->CacheFlag)
#define VmProdCacheImm (VmProdCtx->CacheImm)
#define VmProdMemLog (VmProdCtx->MemLog)
#define VmIdxAddr (VmProdCtx->IdxAddr)
#define VmIdxSave (VmProdCtx->IdxSave)
#define VmIdxEb (VmProdCtx->IdxEb)
#define VmIdxDirty (VmProdCtx->IdxDirty)
#define VmProdMemCount (VmProdCtx->MemCount)
#define VmProdMemAbort (VmProdCtx->MemAbort)
#define VmProdTxnOn (VmProdCtx->TxnOn)
#define VmProdHostCommitted (VmProdCtx->HostCommitted)
#define VmProdGlobal (VmProdCtx->Global)
#define VmProdGlobalSave (VmProdCtx->GlobalSave)
#define VmProdGlobalInit (VmProdCtx->GlobalInit)
#define VmProdGlobalDirty (VmProdCtx->GlobalDirty)
#define VmProdArraySnap (VmProdCtx->ArraySnap)
#define VmProdArraySnapN (VmProdCtx->ArraySnapN)
#define VmProdArraySnapBytes (VmProdCtx->ArraySnapBytes)
#define VmProdTerminalCall (VmProdCtx->TerminalCall)
static long long VmGSave[32];
static unsigned VmGDirty;


static int Emit2(const IrLinInst *Code, int N, VmWord *Out, int Cap)
{
    int i;
    if (N > Cap || N > IR_LIN_STMT_MAX) return 0;
    for (i = 0; i < N; i++)
    {
        if (Code[i].Op == IrLinImm && (Code[i].Tok == 2 || Code[i].Tok == 3))
        {
            if (Code[i].Imm < 0 || Code[i].Imm >= IR_LIN_STMT_MAX
                || !IrLinWideImmOn((int)Code[i].Imm)) return 0;
            VmImmTab[i] = IrLinWideImmAt((int)Code[i].Imm);
        }
        else
        {
            if (Code[i].Imm != (long long)(int)Code[i].Imm) return 0;
            VmImmTab[i] = Code[i].Imm;
        }
        Out[i].Head = (unsigned)Code[i].Op
            | ((unsigned)Code[i].Dest << 8)
            | ((unsigned)Code[i].A << 16)
            | ((unsigned)Code[i].B << 24);
        Out[i].Imm = (int)Code[i].Imm;
        TokTab[i] = (unsigned short)Code[i].Tok;
        FlagTab[i] = (unsigned char)(Code[i].Imm & (IR_LIN_F_UNSIGNED | IR_LIN_F_WIDE));
    }
    return 1;
}

/* Index-store undo for shadow: write during Exec, restore before tree. */

/* Production writes are transactional within each execution-depth context. */
#define VM_PROD_ARRAY_SNAP_ONE_MAX (64u * 1024u * 1024u)
#define VM_PROD_ARRAY_SNAP_TOTAL_MAX (128u * 1024u * 1024u)
static int VmProdExecDepth;

static int VmProdEnsureMemLog(void)
{
    int cap = VmProdCtx->MemLogCap;
    struct VmProdMemUndo *next;
    if (VmProdMemCount < cap) return 1;
    if (cap >= VM_PROD_MEM_LOG_CAP) return 0;
    if (cap == 0) cap = 64;
    else if (cap > VM_PROD_MEM_LOG_CAP / 2) cap = VM_PROD_MEM_LOG_CAP;
    else cap *= 2;
    next = (struct VmProdMemUndo *)realloc(VmProdCtx->MemLog,
                                           (size_t)cap * sizeof(*next));
    if (next == NULL) return 0;
    VmProdCtx->MemLog = next;
    VmProdCtx->MemLogCap = cap;
    return 1;
}

static size_t VmGlobalBytes(const struct Value *V)
{
    if (!V || !V->Typ) return 0;
    switch (V->Typ->Base)
    {
    case TypeChar: case TypeUnsignedChar: return sizeof(char);
    case TypeShort: case TypeUnsignedShort: return sizeof(short);
    case TypeInt: case TypeUnsignedInt: return sizeof(int);
    case TypeLong: case TypeUnsignedLong: return sizeof(long);
    case TypeLongLong: case TypeUnsignedLongLong: return sizeof(long long);
    default: return 0;
    }
}

static struct Value *VmGlobalObject(int gi)
{
    IrExpr *E;
    struct Value *V;
    if (gi < 0 || gi >= 32) return NULL;
    E = IrLowerGlobalAt(gi);
    V = E ? IrGlobalVal(IrLowerParser(), E) : NULL;
    if (!V || !V->Val || !V->Typ || V->IsVolatile) return NULL;
    return V;
}

static struct Value *VmGlobalScalar(int gi, int writing)
{
    struct Value *V = VmGlobalObject(gi);
    enum BaseType b;
    if (V == NULL) return NULL;
    b = V->Typ->Base;
    if (!(b == TypeChar || b == TypeUnsignedChar || b == TypeShort
          || b == TypeUnsignedShort || b == TypeInt || b == TypeUnsignedInt
          || b == TypeLong || b == TypeUnsignedLong || b == TypeLongLong
          || b == TypeUnsignedLongLong)) return NULL;
    if (writing && (V->IsConst || !V->IsLValue)) return NULL;
    return V;
}

static void VmWriteGlobal(struct Value *V, long long nv)
{
    switch (V->Typ->Base)
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
    default: break;
    }
    V->IsInitialized = TRUE;
}

static void VmProdRollbackGlobals(void)
{
    int i;
    for (i = 0; i < 32; ++i)
        if (VmProdGlobalDirty & (1u << i))
        {
            if (VmProdGlobal[i] && VmProdGlobal[i]->Val)
            {
                memcpy(VmProdGlobal[i]->Val, &VmProdGlobalSave[i], VmGlobalBytes(VmProdGlobal[i]));
                VmProdGlobal[i]->IsInitialized = (char)VmProdGlobalInit[i];
            }
            VmProdGlobal[i] = NULL;
        }
    VmProdGlobalDirty = 0;
}

static void VmProdRollbackMemory(void)
{
    while (VmProdMemCount > 0)
    {
        struct VmProdMemUndo *u = &VmProdMemLog[--VmProdMemCount];
        if (u->Copy != NULL)
        {
            if (u->Addr != NULL && u->Bytes > 0)
                memcpy(u->Addr, u->Copy, u->Bytes);
            free(u->Copy);
            u->Copy = NULL;
            u->Bytes = 0;
        }
        else
            IrStorePtrElem(IrLowerParser(), u->Eb, u->Addr, u->Old);
    }
}

static void VmProdCommitMemory(void)
{
    while (VmProdMemCount > 0)
    {
        struct VmProdMemUndo *u = &VmProdMemLog[--VmProdMemCount];
        free(u->Copy);
        u->Copy = NULL;
        u->Bytes = 0;
    }
}

static int VmProdLogBytes(void *addr, size_t bytes)
{
    struct VmProdMemUndo *u;
    unsigned char *copy;
    if (addr == NULL || bytes == 0 || bytes > 4096 || !VmProdEnsureMemLog())
        return 0;
    copy = (unsigned char *)malloc(bytes);
    if (copy == NULL) return 0;
    memcpy(copy, addr, bytes);
    u = &VmProdMemLog[VmProdMemCount++];
    u->Addr = addr;
    u->Eb = 0;
    u->Old = 0;
    u->Copy = copy;
    u->Bytes = bytes;
    return 1;
}

/* Stable global objects are also valid aggregate storage. Iterate the actual
 * global table rather than trusting a pointer value produced by the IR. */
static int VmProdGlobalRange(void *addr, size_t bytes, struct Value **value_out)
{
    struct ParseState *parser = IrLowerParser();
    uintptr_t a = (uintptr_t)addr;
    int hi;
    if (value_out) *value_out = NULL;
    if (parser == NULL || parser->pc == NULL || addr == NULL || bytes == 0
        || a > UINTPTR_MAX - bytes) return 0;
    for (hi = 0; hi < parser->pc->GlobalTable.Size; ++hi)
    {
        struct TableEntry *entry;
        for (entry = parser->pc->GlobalTable.HashTable[hi]; entry != NULL; entry = entry->Next)
        {
            struct Value *v = entry->p.v.Val;
            uintptr_t b;
            size_t n;
            if (v == NULL || v->Val == NULL || v->Typ == NULL || v->Typ->Sizeof <= 0)
                continue;
            b = (uintptr_t)v->Val;
            n = (size_t)v->Typ->Sizeof;
            if (b <= UINTPTR_MAX - n && a >= b && a + bytes <= b + n)
            {
                if (value_out) *value_out = v;
                return 1;
            }
        }
    }
    return 0;
}

/* Aggregate stores are admitted only when their complete range belongs to
 * frame-owned storage or a known global object. This is deliberately stricter
 * than accepting arbitrary pointer bits from a register. */
static int VmProdOwnedRange(void *addr, size_t bytes)
{
    uintptr_t a = (uintptr_t)addr;
    int i;
    IrCallFrame *fr = IrCallDepth > 0 ? IrCallStack[IrCallDepth - 1] : NULL;
    if (fr == NULL || addr == NULL || bytes == 0 || a > UINTPTR_MAX - bytes)
        return 0;
    for (i = 0; i < IR_CALL_MAX_LOCALS; ++i)
    {
        uintptr_t b;
        size_t n = 0;
        struct ValueType *t;
        if (!fr->LocalOwned[i] || fr->LocalVal[i] == 0) continue;
        b = (uintptr_t)fr->LocalVal[i];
        if (fr->LocalArraySize[i] > 0 && fr->LocalElemSize[i] > 0)
            n = (size_t)fr->LocalArraySize[i] * (size_t)fr->LocalElemSize[i];
        else
        {
            t = fr->LocalFromType[i] ? fr->LocalFromType[i] : fr->LocalTyp[i];
            if (t != NULL && t->Sizeof > 0)
                n = (size_t)t->Sizeof;
            else if (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion))
                n = 64; /* type present but Sizeof unset — still owned blob */
            else
                n = (size_t)sizeof(long long); /* scalar owned / missing type */
        }
        if (n > 0 && b <= UINTPTR_MAX - n && a >= b && a + bytes <= b + n)
            return 1;
    }
    return VmProdGlobalRange(addr, bytes, NULL);
}

static int VmProdMemberInitialized(void *addr, int bytes)
{
    uintptr_t a = (uintptr_t)addr;
    int i, k;
    struct Value *global_value = NULL;
    IrCallFrame *fr = IrCallDepth > 0 ? IrCallStack[IrCallDepth - 1] : NULL;
    if (!IrSafeChecksOn()) return 1;
    if (addr == NULL || bytes <= 0 || a > UINTPTR_MAX - (size_t)bytes)
        return 0;
    if (VmProdGlobalRange(addr, (size_t)bytes, &global_value))
        return global_value != NULL && global_value->IsInitialized;
    if (fr == NULL) return 0;
    for (i = 0; i < IR_CALL_MAX_LOCALS; ++i)
    {
        uintptr_t b;
        size_t n = 0;
        struct ValueType *t;
        int off, lim;
        if (!fr->LocalOwned[i] || fr->LocalVal[i] == 0) continue;
        b = (uintptr_t)fr->LocalVal[i];
        if (fr->LocalArraySize[i] > 0 && fr->LocalElemSize[i] > 0)
            n = (size_t)fr->LocalArraySize[i] * (size_t)fr->LocalElemSize[i];
        else
        {
            t = fr->LocalFromType[i] ? fr->LocalFromType[i] : fr->LocalTyp[i];
            if (t != NULL && t->Sizeof > 0)
                n = (size_t)t->Sizeof;
            else if (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion))
                n = 64; /* type present but Sizeof unset — still owned blob */
            else
                n = (size_t)sizeof(long long); /* scalar owned / missing type */
        }
        if (n == 0 || b > UINTPTR_MAX - n || a < b || a + (size_t)bytes > b + n)
            continue;
        /* Whole-object init (no per-byte map): LocalInit[i]=1 means fully set.
         * Member stores often only flip LocalInit without allocating InitMap;
         * requiring a map then false-positives on *p / p->m into that object. */
        if (fr->LocalInitMap[i] == NULL)
            return fr->LocalInit[i] ? 1 : 0;
        off = (int)(a - b);
        lim = IrFrameInitMapLen(fr, i);
        if (IrFrameIsByteMap(fr, i))
        {
            if (off < 0 || off + bytes > lim) return 0;
            for (k = 0; k < bytes; ++k)
                if (!fr->LocalInitMap[i][off + k]) return 0;
            return 1;
        }
        if (fr->LocalElemSize[i] <= 0) return 0;
        off /= fr->LocalElemSize[i];
        return off >= 0 && off < lim && fr->LocalInitMap[i][off];
    }
    return 0;
}

static int VmMemberIndexInBounds(IrExpr *member, int count)
{
    IrExpr *indices[IR_MAX_ARRAY_DIMS];
    IrExpr *idx, *root;
    IrCallFrame *fr = NULL;
    int nidx = 0, slot = -1, dim, i;
    if (member == NULL || member->Kind != IrExMember || count <= 0) return 0;
    idx = member->Left;
    root = idx;
    while (root != NULL && root->Kind == IrExIndex)
    {
        if (nidx >= IR_MAX_ARRAY_DIMS || root->Right == NULL) return 0;
        indices[nidx++] = root;
        root = root->Left;
    }
    if (nidx == 0) return 1;
    if (root != NULL && root->Kind == IrExIdent && root->LocalSlot >= 0
        && root->LocalSlot < IR_CALL_MAX_LOCALS && IrCallDepth > 0)
    {
        slot = root->LocalSlot;
        fr = IrCallStack[IrCallDepth - 1];
    }
    /* AST index nodes are nested from the last subscript back to the first.
     * Validate each dimension independently; checking only the flattened
     * element count would incorrectly accept e.g. a[2][0] for a[2][3]. */
    for (i = nidx - 1, dim = 0; i >= 0; --i, ++dim)
    {
        IrExpr *sub = indices[i]->Right;
        long long value;
        int bound = count;
        if (sub->Kind == IrExConst) value = sub->ConstVal;
        else if (sub->Kind == IrExIdent && IrLowerParser() != NULL)
            value = IrEvalExpr(IrLowerParser(), sub);
        else return 0;
        if (fr != NULL && fr->LocalNDims[slot] > dim && fr->LocalDims[slot][dim] > 0)
            bound = fr->LocalDims[slot][dim];
        else if (fr == NULL && root != NULL && root->Kind == IrExIdent
                 && root->LocalSlot < 0 && root->ParamSlot < 0)
        {
            struct Value *gv = IrGlobalVal(IrLowerParser(), root);
            struct ValueType *gt = gv ? gv->Typ : NULL;
            int d;
            for (d = 0; d < dim && gt != NULL && gt->Base == TypeArray; ++d)
                gt = gt->FromType;
            if (gt != NULL && gt->Base == TypeArray && gt->ArraySize > 0)
                bound = gt->ArraySize;
        }
        if (value < 0 || value >= bound)
        {
            /* A rejected compact instruction must not fall through to the
             * linear VM with the same invalid lvalue: the legacy member path
             * does not consistently diagnose every dimension before forming
             * the address. In safe mode, report the violation here, before
             * any dereference or aggregate copy can occur. Only constant and
             * plain-identifier subscripts are admitted by this helper, so
             * reading the value above cannot replay expression side effects. */
            if (IrSafeChecksOn())
            {
                struct ParseState *parser = IrLowerParser();
                if (parser != NULL)
                {
                    int line = sub->SourceLine > 0
                        ? sub->SourceLine : member->SourceLine;
                    int column = sub->CharacterPos;
                    parser->Line = line;
                    if (column > 0) parser->CharacterPos = column;
                    if (column <= 0 && root != NULL && root->Kind == IrExIdent
                        && root->Ident != NULL)
                        ParserSetCaretOnIndexAccess(parser, root->Ident);
                }
                ProgramFail(parser, DiagnosticMsgArrayIndexOutOfBoundsSized,
                            (int)value, bound);
            }
            return 0;
        }
    }
    return 1;
}

static int VmProdMarkMemberInitialized(void *addr, int bytes)
{
    uintptr_t a = (uintptr_t)addr;
    int i, k;
    IrCallFrame *fr = IrCallDepth > 0 ? IrCallStack[IrCallDepth - 1] : NULL;
    if (addr == NULL || bytes <= 0 || a > UINTPTR_MAX - (size_t)bytes) return 0;
    if (VmProdGlobalRange(addr, (size_t)bytes, NULL)) return 1;
    if (fr == NULL) return 0;
    for (i = 0; i < IR_CALL_MAX_LOCALS; ++i)
    {
        uintptr_t b;
        size_t n = 0;
        struct ValueType *t;
        int off, lim;
        if (!fr->LocalOwned[i] || fr->LocalVal[i] == 0) continue;
        b = (uintptr_t)fr->LocalVal[i];
        if (fr->LocalArraySize[i] > 0 && fr->LocalElemSize[i] > 0)
            n = (size_t)fr->LocalArraySize[i] * (size_t)fr->LocalElemSize[i];
        else
        {
            t = fr->LocalFromType[i] ? fr->LocalFromType[i] : fr->LocalTyp[i];
            if (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion) && t->Sizeof > 0)
                n = (size_t)t->Sizeof;
        }
        if (n == 0 || b > UINTPTR_MAX - n || a < b || a + (size_t)bytes > b + n) continue;
        /* An aggregate member write must not promote the entire object to
         * initialized. Lazily create the same byte map used by the linear VM.
         * This work occurs only on a member store (never on a successful load
         * or per-opcode dispatch); when uninitialized checks are disabled, keep
         * the old constant-time whole-object bookkeeping. */
        if (fr->LocalInitMap[i] == NULL)
        {
            size_t map_bytes = n;
            unsigned char *map;
            if (!IrSafeChecksOn())
            {
                fr->LocalInit[i] = 1;
                return 1;
            }
            if (map_bytes == 0 || map_bytes >= 65536) return 0;
            map = (unsigned char *)calloc(map_bytes, 1);
            if (map == NULL) return 0;
            if (fr->LocalInit[i]) memset(map, 1, map_bytes);
            fr->LocalInitMap[i] = map;
        }
        off = (int)(a - b);
        lim = IrFrameInitMapLen(fr, i);
        if (IrFrameIsByteMap(fr, i))
        {
            if (off < 0 || off + bytes > lim
                || !VmProdLogBytes(fr->LocalInitMap[i] + off, (size_t)bytes)) return 0;
            for (k = 0; k < bytes; ++k) fr->LocalInitMap[i][off + k] = 1;
        }
        else
        {
            if (fr->LocalElemSize[i] <= 0) return 0;
            off /= fr->LocalElemSize[i];
            if (off < 0 || off >= lim || !VmProdLogBytes(fr->LocalInitMap[i] + off, 1)) return 0;
            fr->LocalInitMap[i][off] = 1;
        }
        fr->LocalInit[i] = 1;
        return 1;
    }
    return 0;
}

static int VmIndexParamSafe(IrExpr *ix)
{
    IrCallFrame *fr;
    IrExpr *root;
    struct ValueType *pt, *et;
    int ps, b;
    if (ix == NULL || ix->Kind != IrExIndex || ix->Left == NULL
        || ix->Left->Kind != IrExIdent || IrCallDepth <= 0)
        return 0;
    root = ix->Left;
    ps = root->ParamSlot;
    fr = IrCallStack[IrCallDepth - 1];
    if (fr == NULL || ps < 0 || ps >= fr->NumParams || ps >= IR_CALL_MAX_PARAMS
        || !fr->ParamIsPtr[ps])
        return 0;
    pt = fr->ParamTyp[ps];
    if (pt == NULL || pt->Base != TypePointer || pt->FromType == NULL)
        return 0;
    et = pt->FromType;
    b = et->Base;
    if (!(b == TypeChar || b == TypeUnsignedChar || b == TypeShort
          || b == TypeUnsignedShort || b == TypeInt || b == TypeUnsignedInt
          || b == TypeLong || b == TypeUnsignedLong || b == TypeLongLong
          || b == TypeUnsignedLongLong))
        return 0;
    return et->Sizeof > 0 && et->Sizeof <= (int)sizeof(long long)
        && (ix->ElemSize == 0 || ix->ElemSize == et->Sizeof);
}

/* Global integer arrays have stable storage and can be accessed transactionally.
 * For nested arrays, the lowerer emits a flattened scalar index; this helper
 * therefore returns leaf-element count/stride. Pointer parameters retain their
 * separate path, and local arrays remain rejected until their initialization
 * and bounds state can be represented by the compact VM. */
static int VmIndexGlobalArrayInfo(IrExpr *ix, int writing, int *count,
                                  struct Value **value_out, void **base_out,
                                  size_t *bytes_out)
{
    struct Value *V;
    struct ValueType *et, *t;
    IrExpr *root;
    void *base = NULL;
    int eb = 0, es = 0, b, dims = 1, nested = 0;
    size_t bytes;
    if (count) *count = 0;
    if (value_out) *value_out = NULL;
    if (base_out) *base_out = NULL;
    if (bytes_out) *bytes_out = 0;
    if (ix == NULL || ix->Kind != IrExIndex || ix->Left == NULL)
        return 0;
    root = ix->Left;
    while (root != NULL && root->Kind == IrExIndex)
    {
        nested = 1;
        root = root->Left;
    }
    if (root == NULL || root->Kind != IrExIdent || root->LocalSlot >= 0
        || root->ParamSlot >= 0 || root->Ident == NULL)
        return 0;
    /* Compact multidimensional support is limited to the lowerer's flattened
     * global-array access. Its index is a flat element index, so count and
     * stride must describe leaf elements, never row-sized subarrays. */
    V = IrGlobalVal(IrLowerParser(), root);
    if (V == NULL || V->Typ == NULL || V->Typ->Base != TypeArray
        || V->IsVolatile || (writing && (V->IsConst || !V->IsLValue))
        || V->Typ->ArraySize <= 0 || V->Typ->FromType == NULL)
        return 0;
    t = V->Typ;
    while (t != NULL && t->Base == TypeArray)
    {
        if (t->ArraySize <= 0 || dims > INT_MAX / t->ArraySize)
            return 0;
        dims *= t->ArraySize;
        t = t->FromType;
    }
    et = t;
    if (et == NULL) return 0;
    b = et->Base;
    if (!(b == TypeChar || b == TypeUnsignedChar || b == TypeShort
          || b == TypeUnsignedShort || b == TypeInt || b == TypeUnsignedInt
          || b == TypeLong || b == TypeUnsignedLong || b == TypeLongLong
          || b == TypeUnsignedLongLong))
        return 0;
    if (et->Sizeof <= 0 || et->Sizeof > (int)sizeof(long long)
        || (!nested && ix->ElemSize > 0 && ix->ElemSize != et->Sizeof)
        || !IrResolvePtr(IrLowerParser(), root, &base, &eb, &es)
        || base == NULL)
        return 0;
    /* IrResolvePtr(root) may report the size of a row for multidimensional
     * arrays. Override it with the scalar leaf's actual type and stride. */
    es = et->Sizeof;
    eb = et->Base;
    if ((size_t)dims > ((size_t)-1) / (size_t)et->Sizeof)
        return 0;
    bytes = V->Typ->Sizeof > 0 ? (size_t)V->Typ->Sizeof
        : (size_t)dims * (size_t)et->Sizeof;
    if (bytes < (size_t)dims * (size_t)et->Sizeof)
        return 0;
    if (count) *count = dims;
    if (value_out) *value_out = V;
    if (base_out) *base_out = base;
    if (bytes_out) *bytes_out = bytes;
    return 1;
}

static int VmIndexResolveGlobal(IrExpr *ix, int writing, int *count,
                                void **base_out, int *eb_out, int *es_out)
{
    struct Value *V = NULL;
    void *base = NULL;
    size_t bytes = 0;
    int n = 0;
    if (!VmIndexGlobalArrayInfo(ix, writing, &n, &V, &base, &bytes)
        || !V || !V->Typ || !base)
        return 0;
    {
        struct ValueType *t = V->Typ;
        while (t && t->Base == TypeArray) t = t->FromType;
        if (!t || t->Sizeof <= 0 || t->Sizeof > (int)sizeof(long long))
            return 0;
        if (count) *count = n;
        if (base_out) *base_out = base;
        if (eb_out) *eb_out = t->Base;
        if (es_out) *es_out = t->Sizeof;
    }
    (void)bytes;
    return 1;
}

static int VmIndexGlobalArraySafe(IrExpr *ix, int writing, int *count)
{
    return VmIndexGlobalArrayInfo(ix, writing, count, NULL, NULL, NULL);
}

static IrExpr *VmIndexRoot(IrExpr *ix)
{
    IrExpr *root = ix;
    while (root != NULL && root->Kind == IrExIndex) root = root->Left;
    return root;
}

static int VmScalarArrayElemType(struct ValueType *t)
{
    if (t == NULL) return 0;
    return t->Base == TypeChar || t->Base == TypeUnsignedChar
        || t->Base == TypeShort || t->Base == TypeUnsignedShort
        || t->Base == TypeInt || t->Base == TypeUnsignedInt
        || t->Base == TypeLong || t->Base == TypeUnsignedLong
        || t->Base == TypeLongLong || t->Base == TypeUnsignedLongLong;
}

/* Find a preceding, deliberately restricted local-array declaration. This is
 * used only during preflight, before the kernel has allocated its storage. */
static int VmLocalArrayDeclInfo(IrExpr *ix, const IrLinInst *Code, int upto,
                                int *slot_out, int *count_out, int *size_out)
{
    IrExpr *root = VmIndexRoot(ix);
    int i, slot;
    if (slot_out) *slot_out = -1;
    if (count_out) *count_out = 0;
    if (size_out) *size_out = 0;
    if (root == NULL || root->Kind != IrExIdent || root->LocalSlot < 0
        || root->LocalSlot >= IR_CALL_MAX_LOCALS || Code == NULL || upto < 0)
        return 0;
    slot = root->LocalSlot;
    for (i = 0; i < upto; ++i)
    {
        const IrLinInst *in = &Code[i];
        IrStmt *d;
        struct ValueType *t;
        int es;
        if (in->Op != IrLinKernel || in->Tok != 7 || in->Imm < 0 || in->Imm > INT_MAX)
            continue;
        d = IrLowerKernelStmt((int)in->Imm);
        if (d == NULL || d->LocalSlot != slot || d->DeclArraySize <= 0
            || d->DeclNDims > IR_MAX_ARRAY_DIMS || d->DeclInitVals != NULL || d->DeclInitExprs != NULL)
            continue;
        t = d->DeclFromType;
        if (t != NULL && t->Base == TypeArray) t = t->FromType;
        if (!(VmScalarArrayElemType(t) || (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion)))
            || t->Sizeof <= 0 || t->Sizeof > 255)
            continue;
        es = d->DeclElemSize > 0 ? d->DeclElemSize : t->Sizeof;
        if (es != t->Sizeof || (ix->ElemSize > 0 && ix->ElemSize != es))
            continue;
        if (slot_out) *slot_out = slot;
        if (count_out) *count_out = d->DeclArraySize;
        if (size_out) *size_out = es;
    }
    return count_out != NULL && *count_out > 0;
}

static int VmIndexLocalOwned(IrExpr *ix, int *count, void **base, int *eb, int *es)
{
    IrExpr *root = VmIndexRoot(ix);
    IrCallFrame *fr;
    int slot;
    if (count) *count = 0;
    if (base) *base = NULL;
    if (eb) *eb = 0;
    if (es) *es = 0;
    if (root == NULL || root->Kind != IrExIdent || root->LocalSlot < 0
        || root->LocalSlot >= IR_CALL_MAX_LOCALS || IrCallDepth <= 0)
        return 0;
    slot = root->LocalSlot;
    fr = IrCallStack[IrCallDepth - 1];
    if (fr == NULL || !fr->LocalOwned[slot] || fr->LocalVal[slot] == 0
        || fr->LocalArraySize[slot] <= 0 || fr->LocalElemSize[slot] == 0)
        return 0;
    if (count) *count = fr->LocalArraySize[slot];
    if (base) *base = (void *)(uintptr_t)fr->LocalVal[slot];
    if (es) *es = fr->LocalElemSize[slot];
    if (eb)
    {
        struct ValueType *t = fr->LocalFromType[slot] ? fr->LocalFromType[slot] : fr->LocalTyp[slot];
        if (t != NULL) *eb = t->Base;
    }
    return 1;
}

static int VmIndexSafe(IrExpr *ix, int writing, int *count)
{
    if (count) *count = 0;
    if (VmIndexParamSafe(ix)) return 1;
    if (VmIndexLocalOwned(ix, count, NULL, NULL, NULL)) return 1;
    return VmIndexGlobalArraySafe(ix, writing, count);
}

/* Member expressions (s.m / a[i].m) carry their own field offset and type;
 * treating them as an array index loses that information. Admit only fields
 * rooted in a preceding, VM-owned local struct or struct array declaration. */
static int VmLocalOwnedDeclBefore(const IrLinInst *Code, int upto, int slot)
{
    int i, found = 0;
    if (Code == NULL || slot < 0 || slot >= IR_CALL_MAX_LOCALS) return 0;
    for (i = 0; i < upto; ++i)
    {
        const IrLinInst *in = &Code[i];
        IrStmt *d;
        if (in->Op != IrLinKernel || (in->Tok != 7 && in->Tok != 8)
            || in->Imm < 0 || in->Imm > INT_MAX) continue;
        d = IrLowerKernelStmt((int)in->Imm);
        if (d != NULL && d->LocalSlot == slot) found = 1;
    }
    return found;
}

static int VmMemberRootInfo(IrExpr *member, const IrLinInst *Code, int upto,
                            int *slot_out, int *count_out, int *elem_size_out)
{
    IrExpr *root, *idx;
    int slot, i, count = 1, es = 0, found = 0;
    if (slot_out) *slot_out = -1;
    if (count_out) *count_out = 0;
    if (elem_size_out) *elem_size_out = 0;
    if (member == NULL || member->Kind != IrExMember || Code == NULL) return 0;
    root = member->Left;
    idx = NULL;
    if (root != NULL && root->Kind == IrExIndex)
    {
        idx = root;
        while (root != NULL && root->Kind == IrExIndex) root = root->Left;
        if (root == NULL || root->Kind != IrExIdent) return 0;
        /* Only constants and plain scalar slots are admitted: evaluating an
         * arbitrary index expression through the interpreter could replay effects. */
        if (idx->Right == NULL) return 0;
        if (idx->Right->Kind != IrExConst
            && !(idx->Right->Kind == IrExIdent
                 && (idx->Right->LocalSlot >= 0 || idx->Right->ParamSlot >= 0))) return 0;
    }
    if (root == NULL || root->Kind != IrExIdent) return 0;
    if (root->LocalSlot < 0 && root->ParamSlot < 0 && root->Ident != NULL)
    {
        struct Value *gv = IrGlobalVal(IrLowerParser(), root);
        struct ValueType *gt = gv ? gv->Typ : NULL;
        int total = 1;
        while (gt != NULL && gt->Base == TypeArray)
        {
            if (gt->ArraySize <= 0 || total > INT_MAX / gt->ArraySize) return 0;
            total *= gt->ArraySize;
            gt = gt->FromType;
        }
        if (gt == NULL || (gt->Base != TypeStruct && gt->Base != TypeUnion)
            || gt->Sizeof <= 0 || gt->Sizeof > 4096) return 0;
        if (idx != NULL && idx->Right->Kind == IrExConst
            && (idx->Right->ConstVal < 0 || idx->Right->ConstVal >= total)) return 0;
        if (count_out) *count_out = total;
        if (elem_size_out) *elem_size_out = gt->Sizeof;
        return 1;
    }
    if (root->LocalSlot < 0 || root->LocalSlot >= IR_CALL_MAX_LOCALS) return 0;
    slot = root->LocalSlot;
    for (i = 0; i < upto; ++i)
    {
        const IrLinInst *in = &Code[i];
        IrStmt *d;
        struct ValueType *t;
        if (in->Op != IrLinKernel || (in->Tok != 7 && in->Tok != 8)
            || in->Imm < 0 || in->Imm > INT_MAX) continue;
        d = IrLowerKernelStmt((int)in->Imm);
        if (d == NULL || d->LocalSlot != slot || d->DeclInitExprs != NULL) continue;
        t = d->DeclFromType;
        if (in->Tok == 7)
        {
            if (d->DeclArraySize <= 0 || d->DeclNDims > IR_MAX_ARRAY_DIMS) continue;
            if (t != NULL && t->Base == TypeArray) t = t->FromType;
            if (t == NULL || (t->Base != TypeStruct && t->Base != TypeUnion)) continue;
            count = d->DeclArraySize;
            es = d->DeclElemSize > 0 ? d->DeclElemSize : t->Sizeof;
        }
        else
        {
            if (t == NULL || (t->Base != TypeStruct && t->Base != TypeUnion)) continue;
            count = 1;
            es = t->Sizeof;
        }
        found = es > 0 && es <= 4096;
    }
    if (!found) return 0;
    if (idx != NULL && idx->Right->Kind == IrExConst
        && (idx->Right->ConstVal < 0 || idx->Right->ConstVal >= count)) return 0;
    if (slot_out) *slot_out = slot;
    if (count_out) *count_out = count;
    if (elem_size_out) *elem_size_out = es;
    return 1;
}


static void VmProdFreeArraySnapshots(int restore)
{
    int i;
    for (i = VmProdArraySnapN - 1; i >= 0; --i)
    {
        if (restore && VmProdArraySnap[i].Base && VmProdArraySnap[i].Copy
            && VmProdArraySnap[i].Bytes > 0)
            memcpy(VmProdArraySnap[i].Base, VmProdArraySnap[i].Copy,
                   VmProdArraySnap[i].Bytes);
        free(VmProdArraySnap[i].Copy);
        VmProdArraySnap[i].Base = NULL;
        VmProdArraySnap[i].Copy = NULL;
        VmProdArraySnap[i].Bytes = 0;
    }
    VmProdArraySnapN = 0;
    VmProdArraySnapBytes = 0;
}

static int VmProdHasArraySnapshot(void *base)
{
    int i;
    for (i = 0; i < VmProdArraySnapN; ++i)
        if (VmProdArraySnap[i].Base == base) return 1;
    return 0;
}

static int VmProdPrepareArraySnapshots(const IrLinInst *Code, int N)
{
    int i;
    VmProdFreeArraySnapshots(0);
    for (i = 0; i < N; ++i)
    {
        IrExpr *ix;
        struct Value *V = NULL;
        void *base = NULL;
        size_t bytes = 0;
        int count = 0;
        unsigned char *copy;
        if (Code[i].Op != IrLinStoreIndex || Code[i].Tok == 2 || Code[i].Tok == 3
            || Code[i].Imm < 0 || Code[i].Imm >= 32)
            continue;
        ix = IrLowerIndexAt((int)Code[i].Imm);
        if (ix != NULL && ix->Kind == IrExMember)
            continue; /* member stores log their exact field bytes transactionally */
        if (!VmIndexGlobalArrayInfo(ix, 1, &count, &V, &base, &bytes))
            continue; /* pointer-parameter stores use the element undo log */
        if (!V || !base || bytes == 0 || bytes > VM_PROD_ARRAY_SNAP_ONE_MAX
            || bytes > VM_PROD_ARRAY_SNAP_TOTAL_MAX - VmProdArraySnapBytes)
        {
            VmProdFreeArraySnapshots(0);
            return 0;
        }
        if (VmProdHasArraySnapshot(base)) continue;
        if (VmProdArraySnapN >= VM_PROD_ARRAY_SNAP_CAP)
        {
            VmProdFreeArraySnapshots(0);
            return 0;
        }
        copy = (unsigned char *)malloc(bytes);
        if (copy == NULL)
        {
            VmProdFreeArraySnapshots(0);
            return 0;
        }
        memcpy(copy, base, bytes);
        VmProdArraySnap[VmProdArraySnapN].Base = base;
        VmProdArraySnap[VmProdArraySnapN].Bytes = bytes;
        VmProdArraySnap[VmProdArraySnapN].Copy = copy;
        VmProdArraySnapN++;
        VmProdArraySnapBytes += bytes;
    }
    return 1;
}

static void Step(const VmWord *W, unsigned short tok, long long *R, long long *Locals, int NLocals, int *Ip)
{
    unsigned h = W->Head;
    unsigned op = h & 255, dest = (h >> 8) & 255, a = (h >> 16) & 255, b = (h >> 24) & 255;
    int next = *Ip + 1;
    long long imm = VmImmTab[*Ip];
    switch (op)
    {
    case IrLinImm: R[dest] = imm; break;
    case IrLinLoadLocal:
        R[dest] = (imm >= 0 && imm < NLocals) ? Locals[imm] : 0;
        break;
    case IrLinStoreLocal:
        if (imm >= 0 && imm < NLocals)
        {
            Locals[imm] = R[a];
            if (IrCallDepth > 0 && imm < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *fr = IrCallStack[IrCallDepth - 1];
                fr->LocalInit[imm] = 1;
                if (!(fr->LocalOwned[imm] && fr->LocalArraySize[imm] <= 0
                      && !fr->LocalIsPtr[imm]))
                    fr->LocalVal[imm] = Locals[imm];
            }
        }
        break;
    case IrLinJmp: next = (int)imm; break;
    case IrLinBrZ: if (R[a] == 0) next = (int)imm; break;
    case IrLinBrCmp: {
        long long cv;
        if (!VmApplyBinOpFast((enum LexToken)tok, R[a], R[b], (dest & 1) != 0, 0, &cv))
            cv = IrApplyBinOpRaw(IrLowerParser(), (enum LexToken)tok, R[a], R[b], (dest & 1) != 0, 0);
        if (cv != 0) next = (int)imm;
        break;
    }
    case IrLinRet: break;
    case IrLinLoadGlobal:
    {
        IrExpr *E = IrLowerGlobalAt((int)imm);
        struct Value *V = E ? IrGlobalVal(IrLowerParser(), E) : NULL;
        R[dest] = V ? IrValueAsInt64(V) : 0;
        break;
    }
    case IrLinStoreGlobal:
    {
        IrExpr *E = IrLowerGlobalAt((int)imm);
        struct Value *V = E ? IrGlobalVal(IrLowerParser(), E) : NULL;
        if (V && V->Val && imm >= 0 && imm < 32)
        {
            if (!(VmGDirty & (1u << imm)))
                VmGSave[imm] = IrValueAsInt64(V);
            VmGDirty |= 1u << imm;
            ExpressionAssignInt(IrLowerParser(), V, (PTR_TYPE_INT_SIGNED)R[a], FALSE);
        }
        break;
    }
    case IrLinUnary:
        if (tok == TokenUnaryNot) R[dest] = !R[a];
        else if (tok == TokenUnaryExor) R[dest] = ~R[a];
        else if (tok == TokenMinus) R[dest] = -R[a];
        else if (tok == TokenCast && imm == 1) {
            union { long long l; double d; } u; u.l = R[a];
            R[dest] = (long long)u.d;
        }
        else if (tok == TokenCast && imm == 2) {
            union { long long l; double d; } u; u.d = (double)R[a];
            R[dest] = u.l;
        }
        else if (tok == TokenCast && imm >= 0x100 && imm < 0x100 + Type_Type)
            R[dest] = IrNarrowToBaseType((enum BaseType)(imm - 0x100), R[a]);
        else R[dest] = R[a];
        break;
    case IrLinBin:
        if (tok == TokenPlus) R[dest] = R[a] + R[b];
        else if (tok == TokenMinus) R[dest] = R[a] - R[b];
        else if (tok == TokenAsterisk) R[dest] = R[a] * R[b];
        else if (tok == TokenSlash) R[dest] = R[b] ? R[a] / R[b] : 0;
        else if (tok == TokenModulus) R[dest] = R[b] ? R[a] % R[b] : 0;
        else if (tok == TokenAmpersand) R[dest] = R[a] & R[b];
        else if (tok == TokenArithmeticOr) R[dest] = R[a] | R[b];
        else if (tok == TokenArithmeticExor) R[dest] = R[a] ^ R[b];
        else if (tok == TokenShiftLeft)
            R[dest] = (R[b] < 0 || R[b] >= 63) ? 0 : (R[a] << R[b]);
        else if (tok == TokenShiftRight)
            R[dest] = (R[b] < 0 || R[b] >= 63) ? 0 : (R[a] >> R[b]);
        else if (tok == TokenEqual) R[dest] = R[a] == R[b];
        else if (tok == TokenNotEqual) R[dest] = R[a] != R[b];
        else if (tok == TokenLessThan) R[dest] = R[a] < R[b];
        else if (tok == TokenGreaterThan) R[dest] = R[a] > R[b];
        else if (tok == TokenLessEqual) R[dest] = R[a] <= R[b];
        else if (tok == TokenGreaterEqual) R[dest] = R[a] >= R[b];
        else R[dest] = 0;
        break;
    case IrLinLoadIndex:
    {
        /* Index / Member / *p load via lower IdxTab. Never IrResolveMember. */
        IrExpr *IX = IrLowerIndexAt((int)imm);
        void *Addr = NULL, *Base = NULL;
        int eb = 0, es = 0;
        if (IX && IX->Kind == IrExMember && IrLowerParser())
        {
            if (IX->Left && IX->Left->Kind == IrExIdent && IX->Left->LocalSlot >= 0
                && IrCallDepth > 0 && IX->Ident != NULL)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                int ls = IX->Left->LocalSlot;
                struct ValueType *ST = (ls < IR_CALL_MAX_LOCALS) ? Fr->LocalTyp[ls] : NULL;
                struct Value *MV = NULL;
                if (ST && ST->Members
                    && TableGet(ST->Members, IX->Ident, &MV, NULL, NULL, NULL)
                    && MV && MV->Val)
                {
                    void *BaseM = (void *)(intptr_t)Fr->LocalVal[ls];
                    if (BaseM)
                    {
                        char *P = (char *)BaseM + MV->Val->Integer;
                        es = (MV->Typ && MV->Typ->Base == TypeChar) ? 1 : (int)sizeof(int);
                        eb = (es == 1) ? TypeChar : TypeInt;
                        R[dest] = IrLoadPtrElem(eb, P);
                        break;
                    }
                }
            }
            R[dest] = 0;
            break;
        }
        if (IX && IrLowerParser()
            && IX->Kind == IrExIndex && IX->Left != NULL)
        {
            /* Nested a[i][j] flattened at lower: resolve root Ident only.
             * R[a] is already the flat index; es is leaf element size. */
            IrExpr *RRoot = IX->Left;
            while (RRoot != NULL && RRoot->Kind == IrExIndex)
                RRoot = RRoot->Left;
            if (RRoot == NULL)
                RRoot = IX->Left;
            if (IrResolvePtr(IrLowerParser(), RRoot, &Base, &eb, &es)
                && Base && es > 0 && es <= (int)sizeof(long long))
            {
                char *P = (char *)Base + (ptrdiff_t)R[a] * es;
                if (eb == 0 && es == 1)
                    eb = (IX->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedChar : TypeChar;
                else if (eb == 0 && es == 2)
                    eb = (IX->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedShort : TypeShort;
                else if (eb == 0)
                    eb = (IX->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedInt : TypeInt;
                R[dest] = IrLoadPtrElem(eb, P);
            }
            else
                R[dest] = 0;
        }
        else if (IX && IX->Kind == IrExUnary && IX->Op == TokenAsterisk
                 && IrLowerParser()
                 && IrAddrOf(IrLowerParser(), IX, &Addr, &eb, &es)
                 && Addr && es > 0 && es <= (int)sizeof(long long))
        {
            if (eb == 0 && es == 1)
                eb = (IX->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedChar : TypeChar;
            else if (eb == 0 && es == 2)
                eb = (IX->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedShort : TypeShort;
            else if (eb == 0)
                eb = (IX->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedInt : TypeInt;
            R[dest] = IrLoadPtrElem(eb, Addr);
        }
        else
            R[dest] = 0;
        break;
    }
    case IrLinStoreIndex:
        {
            IrExpr *IX = IrLowerIndexAt((int)imm);
            void *Base = NULL;
            int eb = 0, es = 0;
            int slot = (int)imm;
            char *P = NULL;
            if (IX && IX->Kind == IrExMember && IrLowerParser()
                && IX->Left && IX->Left->Kind == IrExIdent
                && IX->Left->LocalSlot >= 0 && IrCallDepth > 0 && IX->Ident
                && slot >= 0 && slot < 32)
            {
                IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
                int ls = IX->Left->LocalSlot;
                struct ValueType *ST = (ls < IR_CALL_MAX_LOCALS) ? Fr->LocalTyp[ls] : NULL;
                struct Value *MV = NULL;
                if (ST && ST->Members
                    && TableGet(ST->Members, IX->Ident, &MV, NULL, NULL, NULL)
                    && MV && MV->Val)
                {
                    void *BaseM = (void *)(intptr_t)Fr->LocalVal[ls];
                    if (BaseM)
                    {
                        P = (char *)BaseM + MV->Val->Integer;
                        es = (MV->Typ && MV->Typ->Base == TypeChar) ? 1 : (int)sizeof(int);
                        eb = (es == 1) ? TypeChar : TypeInt;
                    }
                }
            }
            else if (IX && IrLowerParser()
                && IX->Kind == IrExIndex && IX->Left != NULL
                && IrResolvePtr(IrLowerParser(), IX->Left, &Base, &eb, &es)
                && Base && es > 0 && es <= (int)sizeof(long long)
                && slot >= 0 && slot < 32)
            {
                P = (char *)Base + (ptrdiff_t)R[b] * es;
                if (eb == 0 && es == 1) eb = TypeChar;
                else if (eb == 0 && es == 2) eb = TypeShort;
                else if (eb == 0) eb = TypeInt;
            }
            else if (IX && IX->Kind == IrExUnary && IX->Op == TokenAsterisk
                     && IrLowerParser() && slot >= 0 && slot < 32)
            {
                void *Addr = NULL;
                if (IrAddrOf(IrLowerParser(), IX, &Addr, &eb, &es)
                    && Addr && es > 0 && es <= (int)sizeof(long long))
                {
                    P = (char *)Addr;
                    if (eb == 0 && es == 1) eb = TypeChar;
                    else if (eb == 0 && es == 2) eb = TypeShort;
                    else if (eb == 0) eb = TypeInt;
                }
            }
            if (P && slot >= 0 && slot < 32)
            {
                if (VmProdTxnOn)
                {
                    if (!VmProdEnsureMemLog())
                    {
                        VmProdMemAbort = 1;
                        break;
                    }
                    VmProdMemLog[VmProdMemCount].Addr = P;
                    VmProdMemLog[VmProdMemCount].Eb = eb;
                    VmProdMemLog[VmProdMemCount].Old = IrLoadPtrElem(eb, P);
                    VmProdMemLog[VmProdMemCount].Copy = NULL;
                    VmProdMemLog[VmProdMemCount].Bytes = 0;
                    ++VmProdMemCount;
                }
                if (!(VmIdxDirty & (1u << slot)))
                {
                    VmIdxAddr[slot] = P;
                    VmIdxEb[slot] = eb;
                    VmIdxSave[slot] = IrLoadPtrElem(eb, P);
                    VmIdxDirty |= 1u << slot;
                }
                else
                {
                    VmIdxAddr[slot] = P;
                    VmIdxEb[slot] = eb;
                }
                IrStorePtrElem(IrLowerParser(), eb, P, R[a]);
                R[a] = IrLoadPtrElem(eb, P);
            }
        }
        break;
    case IrLinLine:
        break;
    default: break;
    }
    *Ip = next;
}

static int Exec(const VmWord *Code, int N, long long *Locals, int NLocals)
{
    long long R[IR_LIN_REGS];
    int i, ip, steps;
    for (i = 0; i < IR_LIN_REGS; i++) R[i] = 0;
    for (ip = 0, steps = 0; ip >= 0 && ip < N && steps < 2000000; steps++)
        Step(&Code[ip], TokTab[ip], R, Locals, NLocals, &ip);
    return steps < 2000000;
}

static int VmMatch, VmMis, VmN, VmBytes;
static unsigned long long VmProdAttempts, VmProdRuns;
static long long VmHold[128];
static int VmHoldOn, VmHoldDepth;

void IrVmShadow(const IrLinInst *Code, int N, const long long *Before, int Depth)
{
    VmWord Words[256];
    long long Copy[128];
    int i;
    (void)i;
    if (!VmOn() || Depth <= 0 || N <= 0 || N > 256)
        return;
    if (!Emit2(Code, N, Words, 256))
        return;
    VmBytes += N * (int)sizeof(VmWord);
    memcpy(Copy, Before, sizeof(Copy));
    VmGDirty = 0;
    VmIdxDirty = 0;
    if (!Exec(Words, N, Copy, 128))
        return;
    if (VmGDirty)
    {
        int gi;
        for (gi = 0; gi < 32; gi++)
            if (VmGDirty & (1u << gi))
            {
                IrExpr *E = IrLowerGlobalAt(gi);
                struct Value *V = E ? IrGlobalVal(IrLowerParser(), E) : NULL;
                if (V && V->Val)
                    ExpressionAssignInt(IrLowerParser(), V, (PTR_TYPE_INT_SIGNED)VmGSave[gi], FALSE);
            }
        VmGDirty = 0;
    }
    if (VmIdxDirty && IrLowerParser())
    {
        int si;
        for (si = 0; si < 32; si++)
            if ((VmIdxDirty & (1u << si)) && VmIdxAddr[si])
                IrStorePtrElem(IrLowerParser(), VmIdxEb[si], VmIdxAddr[si], VmIdxSave[si]);
        VmIdxDirty = 0;
    }
    memcpy(VmHold, Copy, sizeof(VmHold));
    VmHoldOn = 1;
    VmHoldDepth = Depth;
}

void IrVmCommit(int Depth)
{
    int i, same;
    if (!VmHoldOn || Depth != VmHoldDepth || Depth <= 0)
        return;
    VmHoldOn = 0;
    same = 1;
    for (i = 0; i < IR_CALL_MAX_LOCALS && i < 128; i++)
        if (!IrLowerCallSkipped(i) && VmHold[i] != IrCallStack[Depth - 1]->LocalVal[i])
            same = 0;
    VmN++;
    if (same) VmMatch++; else VmMis++;
}

void IrPrintVmStats(void)
{
    const char *e = getenv("BABYC_VM");
    if (!VmOn())
        return;
    /* Linear/bytecode production counters (PATH axis). */
    if (IrExecIsVmProd()
        && (e == NULL || e[0] != '1' || e[1] != '\0'))
    {
        const char *p = getenv("BABYC_VM_PROFILE");
        if (p != NULL && p[0] == '1' && p[1] == '\0')
            fprintf(stderr, "[babyc-bytecode-vm-prod] attempts=%llu runs=%llu\n",
                (unsigned long long)VmProdAttempts, (unsigned long long)VmProdRuns);
        return;
    }
    fprintf(stderr, "[babyc-bytecode-vm] runs=%d match=%d mismatch=%d bytes=%d\n",
        VmN, VmMatch, VmMis, VmBytes);
}


/* The production bytecode VM admits scalar locals, supported indexed memory,
 * terminal printf, and a narrow fixed-arity integer-scalar user-call bridge.
 * Preflight validates the whole stream before execution; indexed stores and
 * scalar-global writes are undo-logged so a safety-limit abort can restore
 * state before the established executor is retried. */
static int VmScalarSlot(int slot, long long *Locals, int NLocals)
{
    IrCallFrame *Fr;
    struct ValueType *T;
    int b;
    if (slot < 0 || slot >= NLocals || slot >= IR_CALL_MAX_LOCALS
        || IrCallDepth <= 0 || Locals == NULL)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    if (Fr == NULL || Fr->LocalVal != Locals || Fr->LocalIsPtr[slot]
        || Fr->LocalOwned[slot] || Fr->LocalArraySize[slot] != 0)
        return 0;
    T = Fr->LocalFromType[slot] ? Fr->LocalFromType[slot] : Fr->LocalTyp[slot];
    if (T == NULL) T = IrLowerCallLocalType(slot);
    if (T == NULL) return 0;
    b = T->Base;
    return b == TypeChar || b == TypeUnsignedChar
        || b == TypeShort || b == TypeUnsignedShort
        || b == TypeInt || b == TypeUnsignedInt
        || b == TypeLong || b == TypeUnsignedLong
        || b == TypeLongLong || b == TypeUnsignedLongLong;
}

static int VmScalarParam(int slot)
{
    IrCallFrame *Fr;
    struct ValueType *T;
    int b;
    if (slot < 0 || slot >= IR_CALL_MAX_PARAMS || IrCallDepth <= 0)
        return 0;
    Fr = IrCallStack[IrCallDepth - 1];
    if (Fr == NULL || slot >= Fr->NumParams || Fr->ParamIsPtr[slot]) return 0;
    T = Fr->ParamTyp[slot];
    if (T == NULL) return 0;
    b = T->Base;
    return b == TypeChar || b == TypeUnsignedChar
        || b == TypeShort || b == TypeUnsignedShort
        || b == TypeInt || b == TypeUnsignedInt
        || b == TypeLong || b == TypeUnsignedLong
        || b == TypeLongLong || b == TypeUnsignedLongLong;
}

static long long VmNormalizeLocal(int slot, long long v)
{
    IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
    struct ValueType *T;
    if (slot >= 0 && slot < IR_CALL_MAX_LOCALS
        && (Fr->LocalIsPtr[slot] || Fr->LocalIsFptr[slot] || Fr->LocalOwned[slot]))
        return v;
    T = Fr->LocalFromType[slot] ? Fr->LocalFromType[slot] : Fr->LocalTyp[slot];
    if (T == NULL) T = IrLowerCallLocalType(slot);
    if (T == NULL) return v;
    if (T->Base == TypePointer || T->Base == TypeFunction
        || T->Base == TypeStruct || T->Base == TypeUnion
        || T->Base == TypeArray)
        return v;
    switch (T->Base)
    {
    case TypeChar: return (char)v;
    case TypeUnsignedChar: return (unsigned char)v;
    case TypeShort: return (short)v;
    case TypeUnsignedShort: return (unsigned short)v;
    case TypeInt: return (int)v;
    case TypeUnsignedInt: return (unsigned int)v;
    case TypeLong: return (long)v;
    case TypeUnsignedLong: return (long long)(unsigned long)v;
    case TypeLongLong: return v;
    case TypeUnsignedLongLong: return (long long)(unsigned long long)v;
    default: return v;
    }
}

static int VmCallSupported(int ci)
{
    struct Value *FV = IrLowerCallFn(ci);
    int i, na;
    if (FV == NULL || FV->Typ == NULL || FV->Typ->Base != TypeFunction || FV->Val == NULL)
        return 0;
    na = IrLowerCallArgN(ci);
    if (FV->Val->FuncDef.Intrinsic != NULL)
    {
        if (IrLowerCallName(ci) == NULL || strcmp(IrLowerCallName(ci), "printf") != 0
            || na < 1 || na > 8) return 0;
        for (i = 0; i < na; i++)
            if (IrLowerCallArgReg(ci, i) < 0 || IrLowerCallArgReg(ci, i) >= IR_LIN_REGS)
                return 0;
        return 1;
    }
    return IrLowerCallUserSafeProd(ci) || IrLowerCallAggregatePureProd(ci);
}

/* Admit calls only in a non-trapping tail. After the first side effect, the
 * remaining instructions may use immediates, scalar local/global loads and
 * stores, preflight-approved calls, and return. Branches, indexed accesses,
 * and trap-capable arithmetic remain excluded to prevent partial-side-effect
 * fallback/replay. */
static int VmTerminalCallSafe(const IrLinInst *Code, int N, int pc)
{
    unsigned char written[IR_CALL_MAX_LOCALS];
    IrCallFrame *Fr = IrCallDepth > 0 ? IrCallStack[IrCallDepth - 1] : NULL;
    int i, saw_ret = 0;
    memset(written, 0, sizeof(written));
    if (Code == NULL || Fr == NULL || pc < 0 || pc >= N
        || Code[pc].Op != IrLinCall || Code[pc].Imm < 0 || Code[pc].Imm > 32767
        || !VmCallSupported((int)Code[pc].Imm)) return 0;
    /* Preflight runs before any instruction executes, so LocalInit does not
     * yet reflect straight-line stores preceding the first terminal call.
     * Seed definite writes from that prefix. If control flow is present, do
     * not infer initialization from lexical order alone. */
    {
        int straight = 1;
        for (i = 0; i < pc; ++i)
            if (Code[i].Op == IrLinJmp || Code[i].Op == IrLinBrZ
                || Code[i].Op == IrLinBrLT || Code[i].Op == IrLinBrCmp || Code[i].Op == IrLinRet)
            { straight = 0; break; }
        if (straight)
            for (i = 0; i < pc; ++i)
            {
                const IrLinInst *pre = &Code[i];
                int slot = -1;
                if (pre->Op == IrLinStoreLocal) slot = (int)pre->Imm;
                else if (pre->Op == IrLinAddLocal || pre->Op == IrLinAddStoreLocal)
                    slot = (int)pre->A;
                if (slot >= 0 && slot < IR_CALL_MAX_LOCALS)
                    written[slot] = 1;
            }
    }
    for (i = pc + 1; i < N; i++)
    {
        const IrLinInst *in = &Code[i];
        switch (in->Op)
        {
        case IrLinLine: case IrLinImm: break;
        /* Pure scalar work between terminal calls is safe: the whole stream
         * has already been preflighted before execution, so allowing these
         * instructions cannot cause a partial-output replay on fallback.
         * This also covers several printf calls whose arguments are computed
         * between calls (e.g. narrow ++/-- edge matrices). */
        case IrLinBin:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS
                || in->B >= IR_LIN_REGS || in->Imm < 0
                || (in->Imm & ~(long long)(IR_LIN_F_UNSIGNED | IR_LIN_F_WIDE)) != 0)
                return 0;
            break;
        case IrLinUnary:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS
                || !(in->Tok == TokenMinus || in->Tok == TokenUnaryNot
                     || in->Tok == TokenUnaryExor
                     || (in->Tok == TokenCast && in->Imm >= 0x100
                         && in->Imm < 0x100 + Type_Type))) return 0;
            break;
        case IrLinAddI:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS
                || in->Imm != (long long)(int)in->Imm) return 0;
            break;
        case IrLinLoadGlobal:
            if (in->Dest >= IR_LIN_REGS || in->Imm < 0 || in->Imm >= 32
                || VmGlobalScalar((int)in->Imm, 0) == NULL) return 0;
            break;
        case IrLinLoadLocal:
            if (in->Dest >= IR_LIN_REGS) return 0;
            if (in->Imm < 0)
            {
                if (!VmScalarParam((int)(-(in->Imm + 1)))) return 0;
            }
            else if (in->Imm >= IR_CALL_MAX_LOCALS
                || !VmScalarSlot((int)in->Imm, Fr->LocalVal, IR_CALL_MAX_LOCALS)
                || (!written[in->Imm] && !Fr->LocalInit[in->Imm])) return 0;
            break;
        case IrLinStoreLocal:
            if (in->A >= IR_LIN_REGS || in->Imm < 0 || in->Imm >= IR_CALL_MAX_LOCALS
                || !VmScalarSlot((int)in->Imm, Fr->LocalVal, IR_CALL_MAX_LOCALS)) return 0;
            written[in->Imm] = 1;
            break;
        case IrLinCall:
            if (in->Imm < 0 || in->Imm > 32767
                || !VmCallSupported((int)in->Imm)) return 0;
            break;
        case IrLinRet:
            if (in->A >= IR_LIN_REGS) return 0;
            saw_ret = 1;
            break;
        default: return 0;
        }
    }
    return saw_ret;
}

static const char *VmOpName(unsigned op)
{
    switch (op) {
    case IrLinImm: return "imm";
    case IrLinLoadLocal: return "load-local";
    case IrLinStoreLocal: return "store-local";
    case IrLinBin: return "bin";
    case IrLinUnary: return "unary";
    case IrLinJmp: return "jmp";
    case IrLinBrZ: return "brz";
    case IrLinBrCmp: return "brcmp";
    case IrLinRet: return "ret";
    case IrLinLoadGlobal: return "load-global";
    case IrLinStoreGlobal: return "store-global";
    case IrLinLoadIndex: return "load-index";
    case IrLinStoreIndex: return "store-index";
    case IrLinLine: return "line";
    case IrLinCall: return "call";
    case IrLinKernel: return "kernel";
    case IrLinIncG: return "inc-global";
    case IrLinAddI: return "add-imm";
    case IrLinAddRegLocal: return "add-reg-local";
    case IrLinAddLocal: return "add-local";
    case IrLinBrLT: return "brlt";
    case IrLinAddStoreLocal: return "add-store-local";
    case IrLinCopyIndex: return "copy-index";
    default: return "unknown";
    }
}

static int IrVmExecProdInner(const IrLinInst *Code, int N, long long *Locals, int NLocals)
{
    const VmWord *Words;
    const unsigned short *prodTok;
    const unsigned char *prodFlag;
    const long long *prodImm;
    int cache_match;
    long long R[IR_LIN_REGS];
    long long Before[IR_CALL_MAX_LOCALS];
    unsigned char InitBefore[IR_CALL_MAX_LOCALS];
    unsigned char LocalTouchedMap[IR_CALL_MAX_LOCALS];
    int LocalTouched[IR_CALL_MAX_LOCALS];
    int LocalTouchedN = 0;
    int TrackLocalWrites = 0, SparseLocalBackup = 0;
    int i, ip, steps, ret = 0;
    if (Code == NULL || N <= 0 || N > IR_LIN_STMT_MAX || Locals == NULL
        || NLocals <= 0 || NLocals > IR_CALL_MAX_LOCALS || IrCallDepth <= 0
        || IrCallStack[IrCallDepth - 1] == NULL
        || IrCallStack[IrCallDepth - 1]->LocalVal != Locals
        || VmProdExecDepth >= VM_PROD_MAX_DEPTH)
        return 0;
    VmProdAttempts++;
    VmProdTerminalCall = 0;
    VmProdFreeArraySnapshots(0);
    /* Same as IrExecLinLocals: drop compile-time LocalInit pollution (790). */
    if (IrSafeChecksOn())
    {
        int zi;
        for (zi = 0; zi < IR_CALL_MAX_LOCALS; zi++)
            IrCallStack[IrCallDepth - 1]->LocalInit[zi] = 0;
    }
    cache_match = VmProdCacheCode == Code && VmProdCacheN == N;
    /* Sparse snapshots are worthwhile only for larger frames; keep the common
     * small-frame path free of bitmap/list bookkeeping. */
    TrackLocalWrites = NLocals >= 16;
    if (TrackLocalWrites) memset(LocalTouchedMap, 0, (size_t)NLocals);

#define VM_TOUCH_LOCAL(slot_) do { \
        int vm_slot_ = (slot_); \
        if (TrackLocalWrites && !LocalTouchedMap[vm_slot_]) { \
            LocalTouchedMap[vm_slot_] = 1; \
            LocalTouched[LocalTouchedN++] = vm_slot_; \
        } \
    } while (0)
#define VM_RESTORE_LOCALS() do { \
        int vm_k_; \
        if (SparseLocalBackup) { \
            for (vm_k_ = 0; vm_k_ < LocalTouchedN; ++vm_k_) { \
                int vm_slot_ = LocalTouched[vm_k_]; \
                { IrCallFrame *vm_fr_ = IrCallStack[IrCallDepth - 1]; \
                  if (!(vm_fr_->LocalOwned[vm_slot_] && vm_fr_->LocalVal[vm_slot_] != 0 \
                        && vm_fr_->LocalArraySize[vm_slot_] <= 0)) \
                      Locals[vm_slot_] = Before[vm_slot_]; \
                  vm_fr_->LocalInit[vm_slot_] = InitBefore[vm_slot_]; } \
            } \
        } else { \
            { IrCallFrame *vm_fr_ = IrCallStack[IrCallDepth - 1]; \
              for (vm_k_ = 0; vm_k_ < NLocals; ++vm_k_) { \
                /* Owned blob: Locals aliases LocalVal; keep DECL payload. */ \
                if (!(vm_fr_->LocalOwned[vm_k_] && vm_fr_->LocalVal[vm_k_] != 0 \
                      && vm_fr_->LocalArraySize[vm_k_] <= 0)) \
                    Locals[vm_k_] = Before[vm_k_]; \
                vm_fr_->LocalInit[vm_k_] = InitBefore[vm_k_]; \
              } } \
        } \
    } while (0)

    /* Verify the whole stream before executing it.  No fallback after partial
     * writes: that would run side effects twice. */
#define VM_REJECT() do { if (VmTraceReject()) fprintf(stderr, "[vm-reject] line=%d idx=%d n=%d op=%u(%s) tok=%u imm=%lld\n", __LINE__, i, N, (unsigned)in->Op, VmOpName(in->Op), (unsigned)in->Tok, in->Imm); return 0; } while (0)
    for (i = 0; i < N; ++i)
    {
        const IrLinInst *in = &Code[i];
        if (cache_match && (VmProdCacheSource[i].Op != in->Op
            || VmProdCacheSource[i].Dest != in->Dest
            || VmProdCacheSource[i].A != in->A
            || VmProdCacheSource[i].B != in->B
            || VmProdCacheSource[i].Tok != in->Tok
            || VmProdCacheSource[i].Imm != in->Imm))
            cache_match = 0;
        switch (in->Op)
        {
        case IrLinImm:
            if (in->Tok == 4)
            {
                if (in->Imm < 0 || in->Imm >= NLocals
                    || !VmLocalOwnedDeclBefore(Code, i, (int)in->Imm)) VM_REJECT();
            }
            if (in->Tok == 2 || in->Tok == 3)
            {
                if (in->Imm < 0 || in->Imm >= IR_LIN_STMT_MAX
                    || !IrLinWideImmOn((int)in->Imm)) VM_REJECT();
                if (cache_match && VmProdCacheImm[i] != IrLinWideImmAt((int)in->Imm))
                    cache_match = 0;
            }
            else if (in->Imm != (long long)(int)in->Imm) VM_REJECT();
            if (in->Dest >= IR_LIN_REGS) VM_REJECT();
            break;
        case IrLinLoadLocal:
            if (in->Dest >= IR_LIN_REGS) VM_REJECT();
            if (in->Imm < 0)
            {
                int ps = (int)(-(in->Imm + 1));
                if (!VmScalarParam(ps)) VM_REJECT();
            }
            else if (in->Imm >= NLocals
                     || (!VmScalarSlot((int)in->Imm, Locals, NLocals)
                         && !VmLocalOwnedDeclBefore(Code, i, (int)in->Imm))) VM_REJECT();
            break;
        case IrLinStoreLocal:
            if (in->A >= IR_LIN_REGS || in->Imm < 0 || in->Imm >= NLocals
                || !VmScalarSlot((int)in->Imm, Locals, NLocals)) VM_REJECT();
            VM_TOUCH_LOCAL((int)in->Imm);
            break;
        case IrLinBin:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS || in->B >= IR_LIN_REGS
                || in->Imm < 0 || (in->Imm & ~(long long)(IR_LIN_F_UNSIGNED | IR_LIN_F_WIDE)) != 0)
                VM_REJECT();
            break;
        case IrLinUnary:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS
                || !(in->Tok == TokenMinus || in->Tok == TokenUnaryNot
                     || in->Tok == TokenUnaryExor
                     || (in->Tok == TokenCast && in->Imm >= 0x100
                         && in->Imm < 0x100 + Type_Type))) VM_REJECT();
            break;
        case IrLinJmp:
            if (in->Imm < 0 || in->Imm > N + 2LL) VM_REJECT();
            break;
        case IrLinBrZ:
            if (in->A >= IR_LIN_REGS || in->Imm < 0 || in->Imm > N + 2LL) VM_REJECT();
            break;
        case IrLinBrCmp:
            if (in->A >= IR_LIN_REGS || in->B >= IR_LIN_REGS
                || in->Imm < 0 || in->Imm > N + 2LL
                || (in->Dest & ~1) != 0
                || !(in->Tok == TokenEqual || in->Tok == TokenNotEqual
                     || in->Tok == TokenLessThan || in->Tok == TokenGreaterThan
                     || in->Tok == TokenLessEqual || in->Tok == TokenGreaterEqual)) VM_REJECT();
            break;
        case IrLinBrLT:
            if (in->A >= IR_LIN_REGS || in->B >= IR_LIN_REGS
                || in->Imm < 0 || in->Imm > N + 2LL) VM_REJECT();
            break;
        case IrLinRet:
            if (in->A >= IR_LIN_REGS) VM_REJECT();
            break;
        case IrLinAddI:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS
                || in->Imm != (long long)(int)in->Imm) VM_REJECT();
            break;
        case IrLinAddRegLocal:
            if (in->Dest >= IR_LIN_REGS || in->A >= IR_LIN_REGS
                || in->B >= NLocals || !VmScalarSlot((int)in->B, Locals, NLocals)
                || (int)in->Tok <= (int)TypeVoid || (int)in->Tok >= (int)Type_Type
                || in->Imm != (long long)(int)in->Imm) VM_REJECT();
            VM_TOUCH_LOCAL((int)in->B);
            break;
        case IrLinAddLocal:
            if (in->Dest >= IR_LIN_REGS || in->A >= NLocals
                || !VmScalarSlot((int)in->A, Locals, NLocals)
                || in->Imm != (long long)(int)in->Imm) VM_REJECT();
            VM_TOUCH_LOCAL((int)in->A);
            break;
        case IrLinAddStoreLocal:
            if (in->Dest >= IR_LIN_REGS || in->A >= NLocals || in->B >= IR_LIN_REGS
                || !VmScalarSlot((int)in->A, Locals, NLocals)) VM_REJECT();
            VM_TOUCH_LOCAL((int)in->A);
            break;
        case IrLinLoadGlobal:
        case IrLinStoreGlobal:
        case IrLinIncG:
            if (in->Imm < 0 || in->Imm >= 32
                || (in->Op == IrLinStoreGlobal && in->A >= IR_LIN_REGS))
                VM_REJECT();
            if (in->Op == IrLinLoadGlobal && in->Tok == 1)
            {
                struct Value *gv = VmGlobalObject((int)in->Imm);
                if (gv == NULL || (gv->Typ->Base != TypeArray
                    && gv->Typ->Base != TypeStruct && gv->Typ->Base != TypeUnion))
                    VM_REJECT();
            }
            else if (VmGlobalScalar((int)in->Imm, in->Op != IrLinLoadGlobal) == NULL)
                VM_REJECT();
            break;
        case IrLinLoadIndex:
        case IrLinStoreIndex:
        {
            IrExpr *ix;
            int array_count = 0;
            if (in->Op == IrLinStoreIndex && in->Tok == 3)
            {
                /* Struct/union assignment is an explicit byte copy: A is the
                 * source blob, B the destination address, Imm the exact size. */
                if (in->A >= IR_LIN_REGS || in->B >= IR_LIN_REGS
                    || in->Imm <= 0 || in->Imm > 4096) VM_REJECT();
                break;
            }
            /* Tok=8 is an indexed-member store: Dest indexes IdxTab, B is
             * the computed destination address, A is the value, Imm is width. */
            if (in->Tok == 8)
            {
                IrExpr *member;
                int member_count = 1;
                if (in->Dest >= 32 || in->Imm <= 0 || in->Imm > 8) VM_REJECT();
                member = IrLowerIndexAt((int)in->Dest);
                if (member == NULL || member->Kind != IrExMember
                    || !VmMemberRootInfo(member, Code, i, NULL, &member_count, NULL)) VM_REJECT();
                break;
            }
            /* Tok=7 is a multidimensional struct-member load: B indexes
             * IdxTab, while Imm is the field width. Validate this layout
             * explicitly; interpreting Imm as the metadata slot is a latent
             * width/metadata alias. */
            if (in->Tok == 7)
            {
                IrExpr *member;
                int member_count = 1;
                if (in->B >= 32 || in->Imm <= 0 || in->Imm > 8) VM_REJECT();
                member = IrLowerIndexAt((int)in->B);
                if (member == NULL || member->Kind != IrExMember
                    || !VmMemberRootInfo(member, Code, i, NULL, &member_count, NULL)) VM_REJECT();
                break;
            }
            /* Tok=2 is a direct pointer/member access: Imm is the element
             * width, not an index-metadata slot. The executor handles this
             * form before consulting IdxTab, so preflight must do the same. */
            if (in->Tok == 2)
            {
                long long width = in->Imm > 8 ? (in->Imm & 0xF) : in->Imm;
                if (width <= 0 || width > (long long)sizeof(long long)) VM_REJECT();
                /* Strict p[i] loads may pack source line/column above the width
                 * nibble. Validate the packed coordinates before execution. */
                if (in->Imm > 8 && (((in->Imm >> 32) & 0xFFFF) == 0
                    || ((in->Imm >> 16) & 0xFFFF) == 0)) VM_REJECT();
                if (in->Op == IrLinLoadIndex && in->Dest >= IR_LIN_REGS) VM_REJECT();
                if (in->Op == IrLinLoadIndex && in->A >= IR_LIN_REGS) VM_REJECT();
                if (in->Op == IrLinStoreIndex && in->A >= IR_LIN_REGS) VM_REJECT();
                if (in->Op == IrLinStoreIndex && in->B >= IR_LIN_REGS) VM_REJECT();
                break;
            }
            if (in->Op == IrLinLoadIndex && in->Dest >= IR_LIN_REGS) VM_REJECT();
            if (in->Op == IrLinLoadIndex && in->A >= IR_LIN_REGS) VM_REJECT();
            if (in->Op == IrLinStoreIndex && in->A >= IR_LIN_REGS) VM_REJECT();
            if (in->Op == IrLinStoreIndex && in->B >= IR_LIN_REGS) VM_REJECT();
            if (in->Imm < 0 || in->Imm >= 32) VM_REJECT();
            ix = IrLowerIndexAt((int)in->Imm);
            if (ix != NULL && ix->Kind == IrExMember)
            {
                int ms = 0, mc = 0, mes = 0;
                if (!VmMemberRootInfo(ix, Code, i, &ms, &mc, &mes)
                    || (ix->ElemSize > 0 && ix->ElemSize > (int)sizeof(long long))) VM_REJECT();
                /* Direct member metadata contains the field offset; runtime
                 * resolves the complete member lvalue instead of indexing it. */
                break;
            }
            if (!VmIndexSafe(ix, in->Op == IrLinStoreIndex, &array_count)
                && !VmLocalArrayDeclInfo(ix, Code, i, NULL, &array_count, NULL)) VM_REJECT();
            {
                void *base = NULL;
                int eb = 0, es = 0;
                if (!VmIndexResolveGlobal(ix, in->Op == IrLinStoreIndex,
                                          &array_count, &base, &eb, &es))
                {
                    int local_slot = -1, local_count = 0, local_size = 0;
                    if (VmLocalArrayDeclInfo(ix, Code, i, &local_slot, &local_count, &local_size))
                    {
                        array_count = local_count;
                        es = local_size;
                        if (ix->ElemSize > 0 && es != ix->ElemSize) VM_REJECT();
                    }
                    else
                    {
                        if (ix->Left->Kind == IrExIndex
                            || !IrResolvePtr(IrLowerParser(), ix->Left, &base, &eb, &es)
                            || base == NULL || es <= 0 || es > (int)sizeof(long long)) VM_REJECT();
                        if (ix->ElemSize > 0 && es != ix->ElemSize) VM_REJECT();
                    }
                }
                else if (ix->Left->Kind != IrExIndex
                         && ix->ElemSize > 0 && es != ix->ElemSize) VM_REJECT();
            }
            break;
        }
        case IrLinKernel:
        {
            IrStmt *d;
            struct ValueType *t;
            int es, j;
            if ((in->Tok != 7 && in->Tok != 8) || in->Imm < 0 || in->Imm > INT_MAX)
                VM_REJECT();
            d = IrLowerKernelStmt((int)in->Imm);
            if (d == NULL || d->LocalSlot < 0 || d->LocalSlot >= NLocals
                || d->DeclInitExprs != NULL)
                VM_REJECT();
            t = d->DeclFromType;
            if (in->Tok == 7)
            {
                if (d->DeclArraySize <= 0 || d->DeclNDims > IR_MAX_ARRAY_DIMS || d->DeclInitVals != NULL)
                    VM_REJECT();
                if (t != NULL && t->Base == TypeArray) t = t->FromType;
                es = d->DeclElemSize > 0 ? d->DeclElemSize : (t != NULL ? t->Sizeof : 0);
                if (!(VmScalarArrayElemType(t) || (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion)))
                    || es <= 0 || es > 255 || es != t->Sizeof) VM_REJECT();
            }
            else
            {
                if (t == NULL || (t->Base != TypeStruct && t->Base != TypeUnion)
                    || t->Sizeof <= 0 || t->Sizeof > 4096 || d->DeclArraySize > 1
                    || d->DeclInitVals != NULL) VM_REJECT();
            }
            /* This initial aggregate slice is intentionally straight-line:
             * no skipped declaration/branch can strand owned storage on abort. */
            for (j = 0; j < N; ++j)
                if (Code[j].Op == IrLinJmp || Code[j].Op == IrLinBrZ || Code[j].Op == IrLinBrLT || Code[j].Op == IrLinBrCmp)
                    VM_REJECT();
            break;
        }
        case IrLinLine:
            break;
        case IrLinCall:
            if (IrLowerCallAggregatePureProd((int)in->Imm))
                break; /* proven pure constructor; later abort may safely replay */
            if (!VmTerminalCallSafe(Code, N, i)) VM_REJECT();
            VmProdTerminalCall = 1;
            break;
        default:
            VM_REJECT();
        }
    }

#undef VM_REJECT
    if (cache_match)
    {
        Words = VmProdCacheWords;
        prodTok = VmProdCacheTok;
        prodFlag = VmProdCacheFlag;
        prodImm = VmProdCacheImm;
    }
    else
    {
        if (!Emit2(Code, N, VmProdCacheWords, IR_LIN_STMT_MAX)) return 0;
        memcpy(VmProdCacheSource, Code, (size_t)N * sizeof(Code[0]));
        memcpy(VmProdCacheTok, TokTab, (size_t)N * sizeof(TokTab[0]));
        memcpy(VmProdCacheFlag, FlagTab, (size_t)N * sizeof(FlagTab[0]));
        memcpy(VmProdCacheImm, VmImmTab, (size_t)N * sizeof(VmImmTab[0]));
        VmProdCacheCode = Code;
        VmProdCacheN = N;
        Words = VmProdCacheWords;
        prodTok = VmProdCacheTok;
        prodFlag = VmProdCacheFlag;
        prodImm = VmProdCacheImm;
    }
    if (!VmProdPrepareArraySnapshots(Code, N))
        return 0;
    /* For large frames with sparse writes, avoid copying untouched locals.
     * Dense/small frames retain the lower-overhead bulk-copy path. */
    SparseLocalBackup = TrackLocalWrites && LocalTouchedN * 2 < NLocals;
    if (SparseLocalBackup)
    {
        for (i = 0; i < LocalTouchedN; ++i)
        {
            int slot = LocalTouched[i];
            Before[slot] = Locals[slot];
            InitBefore[slot] = IrCallStack[IrCallDepth - 1]->LocalInit[slot];
        }
    }
    else
    {
        memcpy(Before, Locals, (size_t)NLocals * sizeof(Before[0]));
        for (i = 0; i < NLocals; ++i)
            InitBefore[i] = IrCallStack[IrCallDepth - 1]->LocalInit[i];
    }
    for (i = 0; i < IR_LIN_REGS; ++i) R[i] = 0;
    VmProdMemCount = 0;
    VmProdMemAbort = 0;
    VmProdHostCommitted = 0;
    VmProdGlobalDirty = 0;
    memset(VmProdGlobal, 0, sizeof(VmProdGlobal));
    VmProdTxnOn = 1;
    ++VmProdExecDepth;
    for (ip = 0, steps = 0; ip >= 0 && ip < N
         && (steps < (VmProdTerminalCall ? 50000000 : 2000000) || VmProdHostCommitted); ++steps)
    {
        const VmWord *w = &Words[ip];
        unsigned h = w->Head;
        unsigned op = h & 255, d = (h >> 8) & 255, a = (h >> 16) & 255, b = (h >> 24) & 255;
        long long imm = prodImm[ip];
        int next = ip + 1;
        switch (op)
        {
        case IrLinImm:
            if (prodTok[ip] == 4)
            {
                int slot = (int)imm;
                IrCallFrame *fr = IrCallStack[IrCallDepth - 1];
                if (slot < 0 || slot >= NLocals || !fr->LocalOwned[slot]
                    || fr->LocalVal[slot] == 0) VmProdMemAbort = 1;
                else R[d] = fr->LocalVal[slot];
            }
            else R[d] = imm;
            break;
        case IrLinLoadLocal:
            if (imm >= 0 && IrSafeChecksOn()
                && !IrCallStack[IrCallDepth - 1]->LocalOwned[imm]
                && !IrCallStack[IrCallDepth - 1]->LocalInit[imm])
            {
                VM_RESTORE_LOCALS();
                VmProdRollbackMemory();
                VmProdRollbackGlobals();
                VmProdFreeArraySnapshots(1);
                VmProdTxnOn = 0;
                return 0; /* fallback replays the canonical uninitialized diagnostic */
            }
            /* Owned blob: address is LocalVal (same contract as Lin Step). */
            if (imm >= 0 && imm < IR_CALL_MAX_LOCALS
                && IrCallStack[IrCallDepth - 1]->LocalOwned[imm]
                && IrCallStack[IrCallDepth - 1]->LocalVal[imm] != 0)
            {
                R[d] = IrCallStack[IrCallDepth - 1]->LocalVal[imm];
            }
            else
            {
                R[d] = (imm < 0)
                    ? IrCallStack[IrCallDepth - 1]->ParamVal[-(imm + 1)]
                    : Locals[imm];
            }
            break;
        case IrLinStoreLocal:
            /* Locals[] may be the live LocalVal[] array (Prod entry).
             * Owned struct/union: never overwrite the blob pointer with a
             * scalar RHS — that is the 619 LocalVal=0 after g.n=/stores.
             * Struct-by-value assign uses Tok/memcpy paths, not this scalar
             * store. */
            if (IrCallDepth > 0 && imm >= 0 && imm < IR_CALL_MAX_LOCALS)
            {
                IrCallFrame *fr = IrCallStack[IrCallDepth - 1];
                int keep_blob = fr->LocalOwned[imm] && fr->LocalArraySize[imm] <= 0
                    && !fr->LocalIsPtr[imm]
                    && fr->LocalVal[imm] != 0
                    && (fr->LocalTyp[imm] != NULL
                        && (fr->LocalTyp[imm]->Base == TypeStruct
                            || fr->LocalTyp[imm]->Base == TypeUnion));
                if (keep_blob)
                {
                    /* Blob address stays in LocalVal/Locals; mark init only.
                     * Field stores go through StoreIndex, not StoreLocal. */
                    fr->LocalInit[imm] = 1;
                }
                else
                {
                    Locals[imm] = VmNormalizeLocal((int)imm, R[a]);
                    fr->LocalInit[imm] = 1;
                    fr->LocalVal[imm] = Locals[imm];
                }
            }
            else
                Locals[imm] = VmNormalizeLocal((int)imm, R[a]);
            break;
        case IrLinUnary:
            if (prodTok[ip] == TokenUnaryNot) R[d] = !R[a];
            else if (prodTok[ip] == TokenUnaryExor) R[d] = ~R[a];
            else if (prodTok[ip] == TokenMinus) R[d] = -R[a];
            else if (prodTok[ip] == TokenCast && prodImm[ip] >= 0x100
                     && prodImm[ip] < 0x100 + Type_Type)
                R[d] = IrNarrowToBaseType((enum BaseType)(prodImm[ip] - 0x100), R[a]);
            else R[d] = R[a];
            break;
        case IrLinBin:
            /* Preserve canonical divide/modulo-by-zero diagnostics. Abort before
             * this instruction so all prior VM writes can be rolled back and the
             * established executor can replay the operation exactly once. */
            if ((prodTok[ip] == TokenSlash || prodTok[ip] == TokenModulus) && R[b] == 0)
            {
                VmProdMemAbort = 1;
                break;
            }
            if (!VmApplyBinOpFast((enum LexToken)prodTok[ip], R[a], R[b],
                                  (prodFlag[ip] & IR_LIN_F_UNSIGNED) != 0,
                                  (prodFlag[ip] & IR_LIN_F_WIDE) != 0, &R[d]))
                R[d] = IrApplyBinOpRaw(IrLowerParser(), (enum LexToken)prodTok[ip],
                                       R[a], R[b], (prodFlag[ip] & IR_LIN_F_UNSIGNED) != 0,
                                       (prodFlag[ip] & IR_LIN_F_WIDE) != 0);
            break;
        case IrLinJmp: next = (int)imm; break;
        case IrLinBrZ: if (R[a] == 0) next = (int)imm; break;
        case IrLinBrCmp: {
            long long cv;
            if (!VmApplyBinOpFast((enum LexToken)prodTok[ip], R[a], R[b], (d & 1) != 0, 0, &cv))
                cv = IrApplyBinOpRaw(IrLowerParser(), (enum LexToken)prodTok[ip], R[a], R[b], (d & 1) != 0, 0);
            if (cv != 0) next = (int)imm;
            break;
        }
        case IrLinBrLT: if (R[a] < R[b]) next = (int)imm; break;
        case IrLinRet:
            IrLowerSetRet(IrLowerVmStabilizeAggregateReturn(R[a]));
            ret = 1;
            ip = N;
            continue;
        case IrLinAddI: R[d] = R[a] + imm; break;
        case IrLinAddRegLocal: {
            long long nv = R[a] + imm;
            enum BaseType bt = (enum BaseType)prodTok[ip];
            if (bt == TypeChar || bt == TypeUnsignedChar || bt == TypeShort || bt == TypeUnsignedShort)
                nv = IrNarrowToBaseType(bt, nv);
            else if (bt == TypeInt) nv = (long long)(int)nv;
            else if (bt == TypeUnsignedInt) nv = (long long)(unsigned int)nv;
            else if (bt == TypeEnum) nv = (long long)(int)nv;
            R[a] = nv; R[d] = nv; break;
        }
        case IrLinAddLocal:
            if (IrSafeChecksOn() && !IrCallStack[IrCallDepth - 1]->LocalInit[a])
            {
                VM_RESTORE_LOCALS();
                VmProdRollbackMemory();
                VmProdRollbackGlobals();
                VmProdFreeArraySnapshots(1);
                VmProdTxnOn = 0;
                return 0;
            }
            Locals[a] += imm;
            if (prodTok[ip] == TypeChar || prodTok[ip] == TypeUnsignedChar
                || prodTok[ip] == TypeShort || prodTok[ip] == TypeUnsignedShort)
                Locals[a] = IrNarrowToBaseType((enum BaseType)prodTok[ip], Locals[a]);
            else
                Locals[a] = VmNormalizeLocal((int)a, Locals[a]);
            IrCallStack[IrCallDepth - 1]->LocalInit[a] = 1;
            R[d] = Locals[a];
            break;
        case IrLinAddStoreLocal:
            if (IrSafeChecksOn() && !IrCallStack[IrCallDepth - 1]->LocalInit[a])
            {
                VM_RESTORE_LOCALS();
                VmProdRollbackMemory();
                VmProdRollbackGlobals();
                VmProdFreeArraySnapshots(1);
                VmProdTxnOn = 0;
                return 0;
            }
            R[d] = Locals[a] + R[b];
            Locals[a] = VmNormalizeLocal((int)a, R[d]);
            IrCallStack[IrCallDepth - 1]->LocalInit[a] = 1;
            break;
        case IrLinLoadGlobal:
        {
            if (prodTok[ip] == 1)
            {
                struct Value *V = VmGlobalObject((int)imm);
                if (!V || (V->Typ->Base != TypeArray
                    && V->Typ->Base != TypeStruct && V->Typ->Base != TypeUnion))
                    VmProdMemAbort = 1;
                else R[d] = (long long)(uintptr_t)V->Val;
            }
            else
            {
                struct Value *V = VmGlobalScalar((int)imm, 0);
                if (!V) VmProdMemAbort = 1;
                else R[d] = IrValueAsInt64(V);
            }
            break;
        }
        case IrLinStoreGlobal:
        {
            int gi = (int)imm;
            struct Value *V = VmGlobalScalar(gi, 1);
            if (!V) { VmProdMemAbort = 1; break; }
            if (!(VmProdGlobalDirty & (1u << gi)))
            {
                VmProdGlobal[gi] = V;
                memcpy(&VmProdGlobalSave[gi], V->Val, VmGlobalBytes(V));
                VmProdGlobalInit[gi] = (unsigned char)V->IsInitialized;
                VmProdGlobalDirty |= 1u << gi;
            }
            VmWriteGlobal(V, R[a]);
            break;
        }
        case IrLinIncG:
        {
            int gi = (int)imm;
            struct Value *V = VmGlobalScalar(gi, 1);
            unsigned long long bits;
            long long nextValue;
            if (!V) { VmProdMemAbort = 1; break; }
            if (!(VmProdGlobalDirty & (1u << gi)))
            {
                VmProdGlobal[gi] = V;
                memcpy(&VmProdGlobalSave[gi], V->Val, VmGlobalBytes(V));
                VmProdGlobalInit[gi] = (unsigned char)V->IsInitialized;
                VmProdGlobalDirty |= 1u << gi;
            }
            bits = (unsigned long long)IrValueAsInt64(V) + 1ULL;
            memcpy(&nextValue, &bits, sizeof(nextValue));
            VmWriteGlobal(V, nextValue);
            break;
        }
        case IrLinLoadIndex:
        {
            /* Tok=7: multidimensional struct-member load. The emitter has
             * already computed the exact field address in R[A]; B, not Imm,
             * identifies the source AST used for bounds/init diagnostics.
             * Imm is the member width. Treating Imm as an IdxTab index aliases
             * every access to whichever AST happens to occupy that slot. */
            if (prodTok[ip] == 7)
            {
                void *ptr = (void *)(uintptr_t)R[a];
                int es7 = (int)imm, eb7 = TypeInt;
                IrExpr *member = b < 32 ? IrLowerIndexAt((int)b) : NULL;
                if (ptr == NULL || es7 <= 0 || es7 > 8 || member == NULL
                    || member->Kind != IrExMember)
                {
                    VmProdMemAbort = 1;
                    break;
                }
                {
                    int member_count = 1;
                    if (!VmMemberRootInfo(member, Code, ip, NULL, &member_count, NULL)
                        || !VmMemberIndexInBounds(member, member_count))
                    {
                        VmProdMemAbort = 1;
                        break;
                    }
                }
                if (!VmProdMemberInitialized(ptr, es7)
                    && VmProdOwnedRange(ptr, (size_t)es7))
                {
                    VmProdMemAbort = 1;
                    break;
                }
                if (member->BitWidth > 0)
                    R[d] = IrBitExtract(ptr, member->BitOffset, member->BitWidth,
                                        member->BitSigned);
                else
                {
                    if (es7 == 1) eb7 = (member->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedChar : TypeChar;
                    else if (es7 == 2) eb7 = (member->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedShort : TypeShort;
                    else if (es7 == 8) eb7 = TypePointer;
                    else eb7 = (member->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedInt : TypeInt;
                    R[d] = IrLoadPtrElem(eb7, ptr);
                }
                break;
            }
            /* Tok=2: *p / p->m pure path — address in R[a], es in Imm.
             * Must run before IdxTab lookup (Imm is es, not meta index). */
            if (prodTok[ip] == 2)
            {
                void *ptr = (void *)(uintptr_t)R[a];
                int es2 = imm > 8 ? (int)(imm & 0xF) : (int)imm;
                int eb2;
                if (ptr == NULL)
                {
                    VmProdMemAbort = 1;
                    break;
                }
                if (es2 == 1) eb2 = TypeChar;
                else if (es2 == 2) eb2 = TypeShort;
                else if (es2 == 8) eb2 = TypePointer;
                else eb2 = TypeInt;
                if (!VmProdMemberInitialized(ptr, es2 > 0 ? es2 : 4)
                    && VmProdOwnedRange(ptr, (size_t)(es2 > 0 ? es2 : 4)))
                {
                    /* Do not suppress uninitialized reads from a tracked local
                     * aggregate. Untracked heap/foreign pointers retain the
                     * established permissive pointer-load behavior. */
                    VmProdMemAbort = 1;
                    break;
                }
                R[d] = IrLoadPtrElem(eb2, ptr);
                break;
            }
            IrExpr *ix = IrLowerIndexAt((int)imm);
            void *base = NULL;
            int eb = 0, es = 0, array_count = 0;
            if (ix != NULL && ix->Kind == IrExMember)
            {
                int member_count = 1;
                if (!VmMemberRootInfo(ix, Code, ip, NULL, &member_count, NULL)
                    || !VmMemberIndexInBounds(ix, member_count)
                    || !IrResolvePtr(IrLowerParser(), ix, &base, &eb, &es)
                    || base == NULL || es <= 0 || es > 4096
                    || !VmProdMemberInitialized(base, es))
                {
                    VmProdMemAbort = 1;
                    break;
                }
                if (ix->BitWidth > 0)
                    R[d] = IrBitExtract(base, ix->BitOffset, ix->BitWidth, ix->BitSigned);
                else
                    R[d] = IrLoadPtrElem(eb, base);
                break;
            }
            if (!ix || !VmIndexSafe(ix, 0, &array_count)
                || !(VmIndexResolveGlobal(ix, 0, &array_count, &base, &eb, &es)
                     || VmIndexLocalOwned(ix, &array_count, &base, &eb, &es)
                     || (ix->Left->Kind != IrExIndex
                         && IrResolvePtr(IrLowerParser(), ix->Left, &base, &eb, &es)))
                || !base || es <= 0 || es > (int)sizeof(long long))
            {
                VmProdMemAbort = 1;
                break;
            }
            if (array_count > 0 && IrSafeChecksOn()
                && (R[a] < 0 || R[a] >= array_count))
            {
                VmProdMemAbort = 1;
                break;
            }
            if (eb == 0 && es == 1)
                eb = (ix->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedChar : TypeChar;
            else if (eb == 0 && es == 2)
                eb = (ix->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedShort : TypeShort;
            else if (eb == 0)
                eb = (ix->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedInt : TypeInt;
            {
                void *elem = (char *)base + (ptrdiff_t)R[a] * es;
                /* Preserve canonical uninitialized-element diagnostics by aborting
                 * transactionally before the compact VM commits any side effects. */
                if (!VmProdMemberInitialized(elem, es))
                {
                    VmProdMemAbort = 1;
                    break;
                }
                R[d] = IrLoadPtrElem(eb, elem);
            }
            break;
        }
        case IrLinStoreIndex:
        {
            /* Tok=8: indexed struct-member store. Validate the full index
             * chain from the AST slot before touching memory, then log and
             * commit through the same transactional/init-map helpers as other
             * compact stores. */
            if (prodTok[ip] == 8)
            {
                void *ptr = (void *)(uintptr_t)R[b];
                int es8 = (int)imm, eb8 = TypeInt;
                IrExpr *member = d < 32 ? IrLowerIndexAt((int)d) : NULL;
                if (ptr == NULL || es8 <= 0 || es8 > 8 || member == NULL
                    || member->Kind != IrExMember)
                {
                    VmProdMemAbort = 1;
                    break;
                }
                {
                    int member_count = 1;
                    if (!VmMemberRootInfo(member, Code, ip, NULL, &member_count, NULL)
                        || !VmMemberIndexInBounds(member, member_count))
                    {
                        VmProdMemAbort = 1;
                        break;
                    }
                }
                if (!VmProdOwnedRange(ptr, (size_t)es8) || !VmProdEnsureMemLog())
                {
                    VmProdMemAbort = 1;
                    break;
                }
                if (es8 == 1) eb8 = (member->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedChar : TypeChar;
                else if (es8 == 2) eb8 = (member->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedShort : TypeShort;
                else if (es8 == 8) eb8 = TypePointer;
                else eb8 = (member->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedInt : TypeInt;
                VmProdMemLog[VmProdMemCount].Addr = ptr;
                VmProdMemLog[VmProdMemCount].Eb = eb8;
                VmProdMemLog[VmProdMemCount].Old = IrLoadPtrElem(eb8, ptr);
                VmProdMemLog[VmProdMemCount].Copy = NULL;
                VmProdMemLog[VmProdMemCount].Bytes = 0;
                ++VmProdMemCount;
                IrStorePtrElem(IrLowerParser(), eb8, ptr, R[a]);
                if (!VmProdMarkMemberInitialized(ptr, es8)) VmProdMemAbort = 1;
                break;
            }
            /* Tok=2: *p = v / p->m = v pure — addr in R[b], value R[a], es Imm. */
            if (prodTok[ip] == 2)
            {
                void *ptr = (void *)(uintptr_t)R[b];
                int es2 = (int)imm;
                int eb2;
                if (ptr == NULL || es2 <= 0)
                {
                    VmProdMemAbort = 1;
                    break;
                }
                if (es2 == 1) eb2 = TypeChar;
                else if (es2 == 2) eb2 = TypeShort;
                else if (es2 == 8) eb2 = TypePointer;
                else eb2 = TypeInt;
                if (!VmProdEnsureMemLog())
                {
                    VmProdMemAbort = 1;
                    break;
                }
                VmProdMemLog[VmProdMemCount].Addr = ptr;
                VmProdMemLog[VmProdMemCount].Eb = eb2;
                VmProdMemLog[VmProdMemCount].Old = IrLoadPtrElem(eb2, ptr);
                VmProdMemLog[VmProdMemCount].Copy = NULL;
                VmProdMemLog[VmProdMemCount].Bytes = 0;
                ++VmProdMemCount;
                IrStorePtrElem(IrLowerParser(), eb2, ptr, R[a]);
                (void)VmProdMarkMemberInitialized(ptr, es2);
                break;
            }
            if (prodTok[ip] == 3)
            {
                void *src = (void *)(uintptr_t)R[a];
                void *dst = (void *)(uintptr_t)R[b];
                size_t bytes = (size_t)imm;
                if (src == NULL || dst == NULL || bytes == 0 || bytes > 4096
                    || !VmProdOwnedRange(dst, bytes)
                    || !(VmProdOwnedRange(src, bytes)
                         || IrLowerVmIsAggregateReturnRange((long long)(uintptr_t)src, bytes))
                    || !VmProdLogBytes(dst, bytes))
                {
                    VmProdMemAbort = 1;
                    break;
                }
                /* Struct assignment permits self-assignment and aliases; use
                 * overlap-safe semantics rather than memcpy's overlap UB. */
                memmove(dst, src, bytes);
                {
                    IrCallFrame *fr = IrCallStack[IrCallDepth - 1];
                    int si;
                    uintptr_t da = (uintptr_t)dst;
                    for (si = 0; si < IR_CALL_MAX_LOCALS; ++si)
                    {
                        uintptr_t ba;
                        size_t total = 0;
                        if (!fr->LocalOwned[si] || fr->LocalVal[si] == 0) continue;
                        ba = (uintptr_t)fr->LocalVal[si];
                        if (fr->LocalArraySize[si] > 0 && fr->LocalElemSize[si] > 0)
                            total = (size_t)fr->LocalArraySize[si] * (size_t)fr->LocalElemSize[si];
                        else
                        {
                            struct ValueType *t = fr->LocalFromType[si] ? fr->LocalFromType[si] : fr->LocalTyp[si];
                            if (t != NULL && (t->Base == TypeStruct || t->Base == TypeUnion) && t->Sizeof > 0)
                                total = (size_t)t->Sizeof;
                        }
                        if (total == 0 || ba > UINTPTR_MAX - total || da < ba || da + bytes > ba + total)
                            continue;
                        fr->LocalInit[si] = 1;
                        if (fr->LocalInitMap[si] != NULL)
                        {
                            int off = (int)(da - ba), k, lim = IrFrameInitMapLen(fr, si);
                            if (IrFrameIsByteMap(fr, si))
                            {
                                if (off < 0 || off + (int)bytes > lim
                                    || !VmProdLogBytes(fr->LocalInitMap[si] + off, bytes))
                                {
                                    VmProdMemAbort = 1;
                                    break;
                                }
                                for (k = 0; k < (int)bytes; ++k)
                                    fr->LocalInitMap[si][off + k] = 1;
                            }
                            else if (fr->LocalElemSize[si] > 0)
                            {
                                int first = off / fr->LocalElemSize[si];
                                int last = (off + (int)bytes - 1) / fr->LocalElemSize[si];
                                if (first < 0 || last >= lim || last < first
                                    || !VmProdLogBytes(fr->LocalInitMap[si] + first,
                                                       (size_t)(last - first + 1)))
                                {
                                    VmProdMemAbort = 1;
                                    break;
                                }
                                for (k = first; k <= last; ++k)
                                    fr->LocalInitMap[si][k] = 1;
                            }
                        }
                        break;
                    }
                }
                break;
            }
            IrExpr *ix = IrLowerIndexAt((int)imm);
            void *base = NULL;
            int eb = 0, es = 0, array_count = 0;
            char *addr;
            if (ix != NULL && ix->Kind == IrExMember)
            {
                int member_count = 1;
                if (!VmMemberRootInfo(ix, Code, ip, NULL, &member_count, NULL)
                    || !VmMemberIndexInBounds(ix, member_count)
                    || !IrResolvePtr(IrLowerParser(), ix, &base, &eb, &es)
                    || base == NULL || es <= 0 || es > 4096
                    || !VmProdOwnedRange(base, (size_t)es))
                {
                    VmProdMemAbort = 1;
                    break;
                }
                addr = (char *)base;
                if (!VmProdEnsureMemLog())
                {
                    VmProdMemAbort = 1;
                    break;
                }
                VmProdMemLog[VmProdMemCount].Addr = addr;
                VmProdMemLog[VmProdMemCount].Eb = eb;
                VmProdMemLog[VmProdMemCount].Old = IrLoadPtrElem(eb, addr);
                VmProdMemLog[VmProdMemCount].Copy = NULL;
                VmProdMemLog[VmProdMemCount].Bytes = 0;
                ++VmProdMemCount;
                if (ix->BitWidth > 0)
                    IrBitInsert(addr, ix->BitOffset, ix->BitWidth, R[a]);
                else
                    IrStorePtrElem(IrLowerParser(), eb, addr, R[a]);
                if (!VmProdMarkMemberInitialized(addr, ix->BitWidth > 0 ? 4 : es))
                {
                    VmProdMemAbort = 1;
                    break;
                }
                break;
            }
            if (!ix || !VmIndexSafe(ix, 1, &array_count)
                || !(VmIndexResolveGlobal(ix, 1, &array_count, &base, &eb, &es)
                     || VmIndexLocalOwned(ix, &array_count, &base, &eb, &es)
                     || (ix->Left->Kind != IrExIndex
                         && IrResolvePtr(IrLowerParser(), ix->Left, &base, &eb, &es)))
                || !base || es <= 0 || es > (int)sizeof(long long))
            {
                VmProdMemAbort = 1;
                break;
            }
            if (array_count > 0 && IrSafeChecksOn()
                && (R[b] < 0 || R[b] >= array_count))
            {
                VmProdMemAbort = 1;
                break;
            }
            if (eb == 0 && es == 1)
                eb = (ix->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedChar : TypeChar;
            else if (eb == 0 && es == 2)
                eb = (ix->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedShort : TypeShort;
            else if (eb == 0)
                eb = (ix->TypeFlags & IR_TF_UNSIGNED) ? TypeUnsignedInt : TypeInt;
            addr = (char *)base + (ptrdiff_t)R[b] * es;
            if (array_count <= 0 || VmIndexLocalOwned(ix, NULL, NULL, NULL, NULL))
            {
                if (!VmProdEnsureMemLog())
                {
                    VmProdMemAbort = 1;
                    break;
                }
                VmProdMemLog[VmProdMemCount].Addr = addr;
                VmProdMemLog[VmProdMemCount].Eb = eb;
                VmProdMemLog[VmProdMemCount].Old = IrLoadPtrElem(eb, addr);
                VmProdMemLog[VmProdMemCount].Copy = NULL;
                VmProdMemLog[VmProdMemCount].Bytes = 0;
                ++VmProdMemCount;
            }
            else if (!VmProdHasArraySnapshot(base))
            {
                VmProdMemAbort = 1;
                break;
            }
            IrStorePtrElem(IrLowerParser(), eb, addr, R[a]);
            if (!VmProdMarkMemberInitialized(addr, es))
            {
                VmProdMemAbort = 1;
                break;
            }
            R[a] = IrLoadPtrElem(eb, addr);
            break;
        }
        case IrLinKernel:
            if (!IrLowerVmExecArrayDecl(&Code[ip], R, Locals, NLocals))
                VmProdMemAbort = 1;
            break;
        case IrLinCall:
            /* Pure aggregate constructors have no externally visible effects
             * and may be replayed if a later compact operation aborts. Other
             * calls remain commit points and are admitted only as safe tails. */
            if (!IrLowerCallAggregatePureProd((int)imm))
                VmProdHostCommitted = 1;
            {
                struct Value *FV = IrLowerCallFn((int)imm);
                if (FV != NULL && FV->Val != NULL
                    && FV->Val->FuncDef.Intrinsic != NULL)
                    R[d] = IrLowerCallHostProd((int)imm, R);
                else
                    R[d] = IrLowerCallUserProd((int)imm, R);
            }
            break;
        case IrLinLine: break;
        default:
            /* Fail closed even if a future emitter/preflight change violates
             * the invariant; take the normal rollback path and release depth. */
            VmProdMemAbort = 1;
            break;
        }
        if (VmProdMemAbort)
            break;
        ip = next;
    }
    /* Restore the entire local scalar frame if the safety limit is hit; the
     * admitted instruction set has no external side effects, so fallback is
     * then safe and will not replay a mutation. */
    if ((steps >= (VmProdTerminalCall ? 50000000 : 2000000) && !VmProdHostCommitted)
        || VmProdMemAbort)
    {
        VM_RESTORE_LOCALS();
        VmProdRollbackMemory();
        VmProdRollbackGlobals();
        VmProdFreeArraySnapshots(1);
        VmProdTxnOn = 0;
        --VmProdExecDepth;
        return 0;
    }
    VmProdTxnOn = 0;
    --VmProdExecDepth;
    VmProdCommitMemory(); /* commit: memory writes are now authoritative */
    VmProdFreeArraySnapshots(0);
    VmProdGlobalDirty = 0; /* commit: scalar globals are authoritative */
    memset(VmProdGlobal, 0, sizeof(VmProdGlobal));
    (void)ret;
#undef VM_RESTORE_LOCALS
#undef VM_TOUCH_LOCAL
    VmProdRuns++;
    return 1;
}

/* Select a reusable context by nesting depth. The wrapper restores the parent
 * context on every exit, including preflight rejection and transactional abort. */
int IrVmExecProd(const IrLinInst *Code, int N, long long *Locals, int NLocals)
{
    struct VmProdContext *Parent = VmProdCtx;
    struct VmProdContext *Child;
    int depth = VmProdExecDepth;
    int result;
    if (depth < 0 || depth >= VM_PROD_MAX_DEPTH) return 0;
    Child = &VmProdContexts[depth];
    VmProdCtx = Child;
    result = IrVmExecProdInner(Code, N, Locals, NLocals);
    VmProdCtx = Parent;
    return result;
}
