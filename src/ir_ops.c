/* ir_ops.c — pure Body-IR operators (D1 step 2 extract from ir_exec.c)
 *
 * Owns: canonical-type-driven binop/unary semantics and narrowing.
 * Frame stack via ir_runtime; ProgramFail via bridge.
 */
#include "picoc.h"
#include <stdint.h>
#include <limits.h>
#include "ir_runtime.h"
#include "ir_bridge.h"
#include "ir_storage.h"


static unsigned long long IrCanonicalInvalidNodeCount;
static int IrCanonicalAtexitRegistered;
int IrCanonicalGateEnabled;
static void IrCanonicalReportAtExit(void)
{
    fprintf(stderr, "[babyc-canonical] invalid_nodes=%llu\n", IrCanonicalInvalidNodeCount);
}
void IrCanonicalCheckNode(IrExpr *E)
{
    if (!IrCanonicalGateEnabled)
    {
        const char *Gate = getenv("BABYC_CANONICAL_GATE");
        if (Gate == NULL || Gate[0] == '\0' || (Gate[0] == '0' && Gate[1] == '\0'))
            return;
        IrCanonicalGateEnabled = 1;
    }
    if (!IrCanonicalAtexitRegistered)
    {
        atexit(IrCanonicalReportAtExit);
        IrCanonicalAtexitRegistered = 1;
        IrCanonicalReportAtExit();
    }
    if (E != NULL && !(E->CanonicalFlags & IR_CF_VALID))
    {
        ++IrCanonicalInvalidNodeCount;
        fprintf(stderr, "[babyc-canonical-invalid] kind=%d op=%d line=%d flags=%u\n",
                (int)E->Kind, (int)E->Op, E->SourceLine, (unsigned)E->CanonicalFlags);
        IrCanonicalReportAtExit();
    }
}

int IrTypeIsUnsignedBase(enum BaseType B)
{
    return (B == TypeUnsignedLongLong || B == TypeUnsignedLong
            || B == TypeUnsignedInt || B == TypeUnsignedShort
            || B == TypeUnsignedChar);
}

/* ISO C integer promotions / usual arithmetic conversions for the Linear and
 * shared IR executors. Values travel through a 64-bit payload, but that
 * transport type must not decide the source-language signedness. In
 * particular, LP64 signed long can represent every unsigned int value, so
 * (long)-1 < 1u is a signed comparison; conversely signed long and unsigned
 * long of equal width convert to unsigned long. */
static int IrBaseIsInteger(enum BaseType B)
{
    switch (B)
    {
        case TypeInt: case TypeShort: case TypeChar: case TypeLong:
        case TypeLongLong: case TypeUnsignedInt: case TypeUnsignedShort:
        case TypeUnsignedChar: case TypeUnsignedLong: case TypeUnsignedLongLong:
        case TypeEnum:
            return 1;
        default:
            return 0;
    }
}

static int IrBaseRank(enum BaseType B)
{
    switch (B)
    {
        case TypeChar: case TypeUnsignedChar: return 1;
        case TypeShort: case TypeUnsignedShort: return 2;
        case TypeInt: case TypeUnsignedInt: case TypeEnum: return 3;
        case TypeLong: case TypeUnsignedLong: return 4;
        case TypeLongLong: case TypeUnsignedLongLong: return 5;
        default: return 0;
    }
}

static size_t IrBaseStorageSize(enum BaseType B)
{
    /* Keep all integer-width decisions tied to the central target model. */
    switch (B)
    {
        case TypeChar: case TypeUnsignedChar: return (size_t)BABYC_VALUE_BITS_CHAR / CHAR_BIT;
        case TypeShort: case TypeUnsignedShort: return (size_t)BABYC_VALUE_BITS_SHORT / CHAR_BIT;
        case TypeInt: case TypeUnsignedInt: case TypeEnum: return (size_t)BABYC_VALUE_BITS_INT / CHAR_BIT;
        case TypeLong: case TypeUnsignedLong: return (size_t)BABYC_VALUE_BITS_LONG / CHAR_BIT;
        case TypeLongLong: case TypeUnsignedLongLong: return (size_t)BABYC_VALUE_BITS_LLONG / CHAR_BIT;
        default: return 0;
    }
}

static int IrBaseIsUnsigned(enum BaseType B)
{
    /* TypeChar is plain char, whose signedness is a target property. */
    if (B == TypeChar) return !BABYC_PLAIN_CHAR_IS_SIGNED;
    return B == TypeUnsignedChar || B == TypeUnsignedShort
        || B == TypeUnsignedInt || B == TypeUnsignedLong
        || B == TypeUnsignedLongLong;
}

static enum BaseType IrBaseUnsignedCounterpart(enum BaseType B)
{
    switch (B)
    {
        case TypeChar: case TypeInt: case TypeEnum: return TypeUnsignedInt;
        case TypeShort: return TypeUnsignedInt;
        case TypeLong: return TypeUnsignedLong;
        case TypeLongLong: return TypeUnsignedLongLong;
        default: return B;
    }
}

enum BaseType IrIntegerPromotionBase(enum BaseType B)
{
    switch (B)
    {
        case TypeChar: case TypeShort: case TypeInt: case TypeEnum:
            return TypeInt;
        case TypeUnsignedChar:
            return sizeof(unsigned char) < sizeof(int) ? TypeInt : TypeUnsignedInt;
        case TypeUnsignedShort:
            return sizeof(unsigned short) < sizeof(int) ? TypeInt : TypeUnsignedInt;
        case TypeUnsignedInt: case TypeLong: case TypeUnsignedLong:
        case TypeLongLong: case TypeUnsignedLongLong:
            return B;
        default:
            return TypeVoid;
    }
}


enum BaseType IrUsualArithmeticCommonIntegerBase(enum BaseType L, enum BaseType R)
{
    int lu, ru, lr, rr;
    enum BaseType S, U;
    if (!IrBaseIsInteger(L) || !IrBaseIsInteger(R)) return TypeVoid;
    L = IrIntegerPromotionBase(L);
    R = IrIntegerPromotionBase(R);
    if (!IrBaseIsInteger(L) || !IrBaseIsInteger(R)) return TypeVoid;
    lu = IrBaseIsUnsigned(L); ru = IrBaseIsUnsigned(R);
    lr = IrBaseRank(L); rr = IrBaseRank(R);
    if (lu == ru) return lr >= rr ? L : R;
    S = lu ? R : L;
    U = lu ? L : R;
    if (IrBaseRank(U) >= IrBaseRank(S)) return U;
    /* A strictly wider signed type represents all values of the narrower
     * unsigned type. Equal-width signed/unsigned types do not. */
    if (IrBaseStorageSize(S) > IrBaseStorageSize(U)) return S;
    return IrBaseUnsignedCounterpart(S);
}


long long IrNarrowToBaseType(enum BaseType Base, long long V)
{
    switch (Base)
    {
        case TypeChar: return BABYC_PLAIN_CHAR_IS_SIGNED ? (long long)(signed char)V : (long long)(unsigned char)V;
        case TypeUnsignedChar: return (long long)(unsigned char)V;
        case TypeShort: return (long long)(short)V;
        case TypeUnsignedShort: return (long long)(unsigned short)V;
        case TypeInt: return (long long)(int)V;
        case TypeUnsignedInt: return (long long)(unsigned int)V;
        case TypeLong: return (long long)(long)V;
        case TypeUnsignedLong: return (long long)(unsigned long)V;
        case TypeLongLong: return V;
        case TypeUnsignedLongLong: return (long long)(unsigned long long)V;
        default: return V;
    }
}

long long IrNarrowToLocalType(IrCallFrame *Fr, int Slot, long long V)
{
    struct ValueType *T = NULL;
    int es;
    if (Fr == NULL || Slot < 0 || Slot >= IR_CALL_MAX_LOCALS)
        return V;
    /* Pointer / fptr / array-decay slots: LocalElemSize is pointee scale, not
     * the width of the stored address. Never truncate pointer bits. */
    if (Fr->LocalIsPtr[Slot] || Fr->LocalIsFptr[Slot]
        || Fr->LocalArraySize[Slot] > 0)
        return V;
    T = Fr->LocalTyp[Slot];
    if (T == NULL)
        T = Fr->LocalFromType[Slot];
    if (T != NULL && (T->Base == TypePointer || T->Base == TypeArray
                      || T->Base == TypeFunction || T->Base == TypeFP))
        return V;
    es = Fr->LocalElemSize[Slot];
    if (T != NULL)
    {
        switch (T->Base)
        {
            case TypeChar:           return (long long)(signed char)V;
            case TypeUnsignedChar:   return (long long)(unsigned char)V;
            case TypeShort:          return (long long)(short)V;
            case TypeUnsignedShort:  return (long long)(unsigned short)V;
            case TypeInt:            return (long long)(int)V;
            case TypeUnsignedInt:    return (long long)(unsigned int)V;
            case TypeLong:           return (long long)(long)V;
            case TypeUnsignedLong:   return (long long)(unsigned long)V;
            default: break;
        }
    }
    /* es fallthrough is for plain integer locals with missing Typ.
     * Never use es==4 to truncate — that destroyed pointer returns
     * (LocalElemSize is pointee scale for T* slots). */
    if (es == 1) return (long long)(unsigned char)V;
    if (es == 2) return (long long)(unsigned short)V;
    return V;
}

long long IrApplyBinOpFlags(struct ParseState *Parser, enum LexToken Op,
                            long long L, long long R,
                            int Unsigned, int Wide, int SourceLine)
{
    /* FIX: signed non-wide (plain int) = 32-bit wrap semantics.
     * long/long long stay 64-bit. Token path already wraps; IR/VM must match. */
    if (!Unsigned && !Wide)
    {
        /* 32-bit wrap without signed-overflow UB: do +-* in unsigned int,
         * then reinterpret as int (same bit pattern as -fwrapv). */
        int sl = (int)L, sr = (int)R;
        unsigned int ul = (unsigned int)sl, ur = (unsigned int)sr;
        switch (Op)
        {
            case TokenPlus: return (long long)(int)(ul + ur);
            case TokenMinus: return (long long)(int)(ul - ur);
            case TokenAsterisk: return (long long)(int)(ul * ur);
            case TokenSlash:
                if (sr == 0) {
                    if (Parser != NULL && SourceLine > 0) Parser->Line = SourceLine;
                    if (Parser != NULL) ParserSetCaretOnLine(Parser, '/');
                    ProgramFail(Parser, DiagnosticMsgDivisionByZero);
                }
                if (sl == INT_MIN && sr == -1) return (long long)INT_MIN;
                return (long long)(sl / sr);
            case TokenModulus:
                if (sr == 0) {
                    if (Parser != NULL && SourceLine > 0) Parser->Line = SourceLine;
                    if (Parser != NULL) ParserSetCaretOnLine(Parser, '%');
                    ProgramFail(Parser, DiagnosticMsgModuloByZero);
                }
                if (sl == INT_MIN && sr == -1) return 0;
                return (long long)(sl % sr);
            case TokenShiftLeft: return (long long)(int)(ul << (unsigned)(R & 31));
            case TokenShiftRight: return (long long)(sl >> (unsigned)(R & 31));
            /* comparisons and bitwise already well-defined in 64; leave to general path */
            default: break;
        }
    }
    /* FIX: unsigned non-wide = 32-bit semantics (operands and result truncated). */
    if (Unsigned && !Wide)
    {
        unsigned int ul = (unsigned int)L, ur = (unsigned int)R;
        switch (Op)
        {
            case TokenPlus: return (long long)(unsigned int)(ul + ur);
            case TokenMinus: return (long long)(unsigned int)(ul - ur);
            case TokenAsterisk: return (long long)(unsigned int)(ul * ur);
            case TokenSlash:
                if (ur == 0) {
                    if (Parser != NULL && SourceLine > 0) Parser->Line = SourceLine;
                    if (Parser != NULL) ParserSetCaretOnLine(Parser, '/');
                    ProgramFail(Parser, DiagnosticMsgDivisionByZero);
                }
                return (long long)(ul / ur);
            case TokenModulus:
                if (ur == 0) {
                    if (Parser != NULL && SourceLine > 0) Parser->Line = SourceLine;
                    if (Parser != NULL) ParserSetCaretOnLine(Parser, '%');
                    ProgramFail(Parser, DiagnosticMsgModuloByZero);
                }
                return (long long)(ul % ur);
            case TokenEqual: return ul == ur;
            case TokenNotEqual: return ul != ur;
            case TokenLessThan: return ul < ur;
            case TokenGreaterThan: return ul > ur;
            case TokenLessEqual: return ul <= ur;
            case TokenGreaterEqual: return ul >= ur;
            case TokenShiftLeft: return (long long)(unsigned int)(ul << (unsigned)(R & 31));
            case TokenShiftRight: return (long long)(ul >> (unsigned)(R & 31));
            case TokenAmpersand: return (long long)(ul & ur);
            case TokenArithmeticOr: return (long long)(ul | ur);
            case TokenArithmeticExor: return (long long)(ul ^ ur);
            default: return 0;
        }
    }
    switch (Op)
    {
        case TokenPlus:
            /* signed and unsigned both wrap in 64-bit two's complement */
            return (long long)((unsigned long long)L + (unsigned long long)R);
        case TokenMinus:
            return (long long)((unsigned long long)L - (unsigned long long)R);
        case TokenAsterisk:
            return (long long)((unsigned long long)L * (unsigned long long)R);
        case TokenSlash:
            if (R == 0) {
                if (Parser != NULL && SourceLine > 0) Parser->Line = SourceLine;
                if (Parser != NULL) ParserSetCaretOnLine(Parser, '/');
                ProgramFail(Parser, DiagnosticMsgDivisionByZero);
            }
            if (Unsigned) return (long long)((unsigned long long)L / (unsigned long long)R);
            if (L == LLONG_MIN && R == -1) return LLONG_MIN;
            return L / R;
        case TokenModulus:
            if (R == 0) {
                if (Parser != NULL && SourceLine > 0) Parser->Line = SourceLine;
                if (Parser != NULL) ParserSetCaretOnLine(Parser, '%');
                ProgramFail(Parser, DiagnosticMsgModuloByZero);
            }
            if (Unsigned) return (long long)((unsigned long long)L % (unsigned long long)R);
            if (L == LLONG_MIN && R == -1) return 0;
            return L % R;
        case TokenEqual: return L == R;
        case TokenNotEqual: return L != R;
        case TokenLessThan:
            return Unsigned ? ((unsigned long long)L < (unsigned long long)R) : (L < R);
        case TokenGreaterThan:
            return Unsigned ? ((unsigned long long)L > (unsigned long long)R) : (L > R);
        case TokenLessEqual:
            return Unsigned ? ((unsigned long long)L <= (unsigned long long)R) : (L <= R);
        case TokenGreaterEqual:
            return Unsigned ? ((unsigned long long)L >= (unsigned long long)R) : (L >= R);
        case TokenShiftLeft:
            if (Unsigned) {
                if (Wide) return (long long)((unsigned long long)L << (unsigned)(R & 63));
                return (long long)((unsigned int)L << (unsigned)(R & 31));
            }
            /* Use unsigned arithmetic internally: signed left shift of a
             * negative/overflowing value is UB in the host interpreter. */
            return (long long)((unsigned long long)L << (unsigned)(R & 63));
        case TokenShiftRight:
            if (Unsigned) {
                if (Wide) return (long long)((unsigned long long)L >> (unsigned)(R & 63));
                return (long long)((unsigned int)L >> (unsigned)(R & 31));
            }
            return L >> R;
        case TokenAmpersand: return L & R;
        case TokenArithmeticOr: return L | R;
        case TokenArithmeticExor: return L ^ R;
        default: return 0;
    }
}

long long IrApplyBinOp(struct ParseState *Parser, enum LexToken Op,
                       long long L, long long R, IrExpr *E)
{
    int U = 0, W = 0, line = 0;
    /* Per-node cache: avoid 6 recursive tree walks per BinOp. SignedFast=1 when node and
     * both children are resolved signed int/ptr (flags are static per node). */
    if (E != NULL && E->SignedFast != 1
        && (E->TypeFlags & (IR_TF_RESOLVED | IR_TF_INT | IR_TF_PTR)) && !(E->TypeFlags & IR_TF_UNSIGNED)
        && (E->Left == NULL || ((E->Left->TypeFlags & (IR_TF_RESOLVED | IR_TF_INT | IR_TF_PTR))
                                && !(E->Left->TypeFlags & IR_TF_UNSIGNED)))
        && (E->Right == NULL || ((E->Right->TypeFlags & (IR_TF_RESOLVED | IR_TF_INT | IR_TF_PTR))
                                 && !(E->Right->TypeFlags & IR_TF_UNSIGNED)))
        && !(E->CanonicalOpFlags & IR_CF_UNSIGNED))
        E->SignedFast = 1;
    if (E != NULL) {
        /* Width is independent of signed/unsigned. Compute even for SignedFast
         * so long long does not fall into the 32-bit wrap path. */
        if (E->WideCache == 0)
            E->WideCache = (unsigned char)((E->CanonicalOpFlags & IR_CF_WIDE) ? 2 : 1);
        W = (E->WideCache == 2);
        if (E->SignedFast != 1) {
        U = (E->CanonicalOpFlags & IR_CF_UNSIGNED) != 0;
        if (U) {
            /* Pointer arithmetic (unsigned char *p; p+3, array+i) must stay 64-bit:
             * unsigned element type does not make the address a 32-bit value. */
            if ((E->TypeFlags & IR_TF_PTR)
                || (E->Left && ((E->Left->TypeFlags & IR_TF_PTR)
                    || (E->Left->Kind == IrExIdent && E->Left->ElemSize > 0)))
                || (E->Right && ((E->Right->TypeFlags & IR_TF_PTR)
                    || (E->Right->Kind == IrExIdent && E->Right->ElemSize > 0))))
                return IrApplyBinOpFlags(Parser, Op, L, R, 0, 1, E->SourceLine > 0 ? E->SourceLine : 0);
        }
        }
        if (E->SourceLine > 0) line = E->SourceLine;
        else if (E->Right != NULL && E->Right->SourceLine > 0) line = E->Right->SourceLine;
    }
    return IrApplyBinOpFlags(Parser, Op, L, R, U, W, line);
}

long long IrApplyUnary(enum LexToken Op, long long L)
{
    switch (Op)
    {
        case TokenUnaryNot: return L ? 0 : 1;
        case TokenMinus: return -L;
        case TokenPlus: return L;
        case TokenUnaryExor: return (long long)(~(int)L);
        default: return L;
    }
}

/* F3: read integer payload from a Value* without ExpressionCoerceInteger. */
long long IrValueAsInt64(struct Value *Val)
{
    if (Val == NULL || Val->Typ == NULL || Val->Val == NULL)
        return 0;
    /* Bitfields: rare on IR hot path; fall through to integer union read. */
    switch (Val->Typ->Base)
    {
        case TypeInt:              return (long long)Val->Val->Integer;
        case TypeChar:             return (long long)Val->Val->Character;
        case TypeShort:            return (long long)Val->Val->ShortInteger;
        case TypeLong:             return (long long)Val->Val->LongInteger;
        case TypeLongLong:         return Val->Val->LongLongInteger;
        case TypeUnsignedInt:      return (long long)Val->Val->UnsignedInteger;
        case TypeUnsignedShort:    return (long long)Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:     return (long long)Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong: return (long long)Val->Val->UnsignedLongLongInteger;
        case TypeUnsignedChar:     return (long long)Val->Val->UnsignedCharacter;
        case TypePointer:          return (long long)(intptr_t)Val->Val->Pointer;
#ifndef NO_FP
        case TypeFP:               return (long long)Val->Val->FP;
#endif
        default:                   return 0;
    }
}

long long IrApplyBinOpRaw(struct ParseState *Parser, enum LexToken Op,
                          long long L, long long R, int Unsigned, int Wide)
{
    return IrApplyBinOpFlags(Parser, Op, L, R, Unsigned, Wide,
                             Parser != NULL ? Parser->Line : 0);
}
