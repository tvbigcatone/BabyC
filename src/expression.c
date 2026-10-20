/* picoc expression evaluator - a stack-based expression evaluation system
 * which handles operator precedence */

#include "interpreter.h"
#include "ir_runtime.h"
int IrSafeChecksOn(void);
extern int BabycGotoMatchCount;
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/* Extra slots for varargs (...). Was 8 → crash on printf with ≥9 args
 * (format is Param[0]; ArgCount 10 overruns NumParams+8). */
#ifndef PICOC_MAX_VARARGS
# define PICOC_MAX_VARARGS 32
#endif

/* whether evaluation is left to right for a given precedence level */
#define IS_LEFT_TO_RIGHT(p) ((p) != 2 && (p) != 14)
#define BRACKET_PRECEDENCE 20

/* If the destination is not float, we can't assign a floating value to it, we need to convert it to integer instead */
#define ASSIGN_FP_OR_INT(value) \
        if (IS_FP(BottomValue)) { ResultFP = ExpressionAssignFP(Parser, BottomValue, value); } \
        else { ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, (PTR_TYPE_INT_SIGNED)(value), FALSE); ResultIsInt = TRUE; } \

#define DEEP_PRECEDENCE (BRACKET_PRECEDENCE*1000)

/* A host long is a 64-bit arithmetic operand on LP64, but remains 32-bit
 * on LLP64/ILP32. Use the host ABI's actual width, not the enum name alone. */
#define IS_LONGLONG_COERCIBLE(v) ((v)->Typ->Base == TypeLongLong || (v)->Typ->Base == TypeUnsignedLongLong || (((v)->Typ->Base == TypeLong || (v)->Typ->Base == TypeUnsignedLong) && sizeof(long) > sizeof(int)))

static int TokenIsAssignmentOp(enum LexToken T)
{
    return T == TokenAssign || T == TokenAddAssign || T == TokenSubtractAssign
        || T == TokenMultiplyAssign || T == TokenDivideAssign || T == TokenModulusAssign
        || T == TokenShiftLeftAssign || T == TokenShiftRightAssign
        || T == TokenArithmeticAndAssign || T == TokenArithmeticOrAssign
        || T == TokenArithmeticExorAssign;
}

/* Keep the common integer case cheap: hot expression evaluation typically has
 * equal signedness, for which only integer promotions are needed. Compute the
 * full rank/representability rule only for a mixed signed/unsigned pair. */
static int ExpressionOpUsesUnsigned(struct Value *BottomValue,
                                    struct Value *TopValue, enum LexToken Op)
{
    enum BaseType LB, RB, LP, RP, Common;
    int LU, RU;
    if (BottomValue == NULL || TopValue == NULL
        || BottomValue->Typ == NULL || TopValue->Typ == NULL)
        return 0;
    LB = BottomValue->Typ->Base;
    RB = TopValue->Typ->Base;
    if (Op == TokenShiftLeft || Op == TokenShiftRight
        || Op == TokenShiftLeftAssign || Op == TokenShiftRightAssign)
        return IrTypeIsUnsignedBase(IrIntegerPromotionBase(LB));
    LP = IrIntegerPromotionBase(LB);
    RP = IrIntegerPromotionBase(RB);
    LU = IrTypeIsUnsignedBase(LP);
    RU = IrTypeIsUnsignedBase(RP);
    if (LU == RU) return LU;
    Common = IrUsualArithmeticCommonIntegerBase(LB, RB);
    return IrTypeIsUnsignedBase(Common);
}

static int TypeIsFunctionLike(struct Value *V)
{
    return V != NULL && V->Typ != NULL
        && (V->Typ->Base == TypeFunction || V->Typ->Base == TypeMacro);
}

/* Pointer-alias provenance belongs to the token/tree evaluator. Body-IR
 * maintains a separate per-frame map; applying token Value maps while IR runs
 * would misclassify valid IR writes as uninitialized. */
static int ExpressionAliasInitTrackingEnabled(void)
{
#ifdef BABYC_MICRO
    return 1;
#else
    static int cached = -1;
    if (cached < 0)
    {
        /* PATH=token (or legacy BABYC_NO_IR) */
        cached = (BabycPathGet() == BABYC_PATH_TOKEN) ? 1 : 0;
    }
    return cached;
#endif
}

/* Per-byte init tracking.
 * An automatic array or struct declared without initializer carries a bitmap
 * (Value.ElemInit, one bit per byte).  Every lvalue formed from it (a[i],
 * m[i][j], a[i].m, s.m, s.arr[i], ...) keeps LValueFrom pointing at that root,
 * so a scalar leaf is initialised iff all of its bytes have been written.
 * Aggregates are never checked as a whole (copying a partly written struct is
 * accepted).  Anything that lets an address escape (&x, bare array name,
 * row/array-member decay) marks the affected bytes, or the whole root, as
 * initialised: aliases stay permissive. */
static struct Value *ExpressionInitRoot(struct Value *V)
{
    int Guard = 0;
    while (V != NULL && Guard++ < 8)
    {
        if (V->ElemInit != NULL)
            return V;
        if (V->LValueFrom == NULL || V->LValueFrom == V)
            break;
        V = V->LValueFrom;
    }
    return NULL;
}

/* byte range of V inside Root; 0 when V does not lie inside it */
static int ExpressionByteRange(struct Value *Root, struct Value *V, int *Off, int *Len)
{
    ptrdiff_t o;
    int n;
    if (Root == NULL || V == NULL || Root->ElemInit == NULL || V->Typ == NULL || V->Val == NULL)
        return 0;
    n = TypeSizeValue(V, TRUE);
    if (n < 1)
        return 0;
    /* Aligned scalar proxies keep Val in private aligned storage, while
     * ValRaw identifies the real byte range inside the tracked aggregate.
     * Init tracking must use the backing address or a member write is
     * misclassified as an escaped alias and marks the whole object initialised. */
    o = (char *)(V->ValRaw != NULL ? V->ValRaw : (void *)V->Val)
        - (char *)Root->Val;
    if (o < 0 || o + n > Root->ElemInitCount)
        return 0;
    *Off = (int)o;
    *Len = n;
    return 1;
}

static void ExpressionInitMarkRange(struct Value *Root, int Off, int Len)
{
    while (Len > 0)
    {
        int Bit = Off & 7;
        int N = 8 - Bit;
        if (N > Len)
            N = Len;
        Root->ElemInit[Off >> 3] |= (unsigned char)(((1u << N) - 1u) << Bit);
        Off += N;
        Len -= N;
    }
}

/* are all (mode 0) / any (mode 1) of the bytes Off..Off+Len-1 marked? */
static int ExpressionInitTest(struct Value *Root, int Off, int Len, int Any)
{
    while (Len > 0)
    {
        int Bit = Off & 7;
        int N = 8 - Bit;
        unsigned Mask, V;
        if (N > Len)
            N = Len;
        Mask = ((1u << N) - 1u) << Bit;
        V = Root->ElemInit[Off >> 3] & Mask;
        if (Any ? V != 0 : V != Mask)
            return Any ? 1 : 0;
        Off += N;
        Len -= N;
    }
    return Any ? 0 : 1;
}

/* is the value V, an lvalue inside tracked Root, initialised? */
static int ExpressionLeafInit(struct Value *Root, struct Value *V)
{
    int Off, Len;
    if (Root == NULL || Root->IsInitialized)
        return 1;
    if (V->Typ == NULL || V->Typ->Base == TypeArray
        || V->Typ->Base == TypeStruct || V->Typ->Base == TypeUnion)
        return 1; /* aggregates: permissive */
    if (!ExpressionByteRange(Root, V, &Off, &Len))
        return 1;
    return ExpressionInitTest(Root, Off, Len, 0);
}

/* Pointer-alias reads are stricter than the legacy escape policy: when the
 * target still lies in a tracked automatic object, inspect its byte map even
 * if IsInitialized was set conservatively when the address escaped. */
static int ExpressionLeafInitAlias(struct Value *Root, struct Value *V)
{
    int Off, Len;
    if (Root == NULL || Root->ElemInit == NULL || V == NULL || V->Typ == NULL)
        return 1;
    if (V->Typ->Base == TypeArray || V->Typ->Base == TypeStruct
        || V->Typ->Base == TypeUnion)
        return 1;
    if (!ExpressionByteRange(Root, V, &Off, &Len))
        return 1;
    return ExpressionInitTest(Root, Off, Len, 0);
}

/* the bytes of V (a row / array member) may be written through a decayed pointer */
static void ExpressionInitEscape(struct Value *Root, struct Value *V)
{
    int Off, Len;
    if (Root != NULL && ExpressionByteRange(Root, V, &Off, &Len))
        ExpressionInitMarkRange(Root, Off, Len);
}

/* Address-based variants used by the bound loop IR (parse.c): Root is an
 * automatic array/struct carrying the per-byte bitmap, Addr/Size a scalar leaf. */
int ExpressionInitLeafAddr(struct Value *Root, void *Addr, int Size)
{
    ptrdiff_t o;
    if (Root == NULL || Root->ElemInit == NULL || Root->IsInitialized)
        return 1;
    o = (char *)Addr - (char *)Root->Val;
    if (o < 0 || Size < 1 || o + Size > Root->ElemInitCount)
        return 1; /* outside the tracked object: permissive */
    return ExpressionInitTest(Root, (int)o, Size, 0);
}

void ExpressionInitMarkAddr(struct Value *Root, void *Addr, int Size)
{
    ptrdiff_t o;
    if (Root == NULL || Root->ElemInit == NULL || Root->IsInitialized)
        return;
    o = (char *)Addr - (char *)Root->Val;
    if (o < 0 || Size < 1 || o + Size > Root->ElemInitCount)
    {
        Root->IsInitialized = TRUE; /* not inside the object: alias, permissive */
        return;
    }
    ExpressionInitMarkRange(Root, (int)o, Size);
}

/* compound assignment (+= -= ...) reads its target: an uninitialised target is a use */
static void ExpressionCheckCompoundUse(struct ParseState *Parser, struct Value *V)
{
    struct Value *Root;
    if (V == NULL || !V->IsLValue || V->Typ == NULL || TypeIsFunctionLike(V)
        || V->Typ->Base == TypeArray || V->Typ->Base == TypeStruct
        || V->Typ->Base == TypeUnion)
        return;
    Root = ExpressionInitRoot(V);
    if (Root != NULL && Root != V)
    {
        if (!ExpressionLeafInit(Root, V))
            ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
        return;
    }
    if (!V->IsInitialized)
        ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
}

void ExpressionMarkInitialized(struct Value *Dest)
{
    struct Value *Root;
    int Off, Len;
    if (Dest == NULL)
        return;
    Dest->IsInitialized = TRUE;
    if (Dest->InitAliasRoot != NULL && Dest->Val != NULL)
    {
        void *Addr = Dest->ValRaw != NULL ? Dest->ValRaw : (void *)Dest->Val;
        int Size = TypeSizeValue(Dest, TRUE);
        if (Size > 0)
            ExpressionInitMarkAddr(Dest->InitAliasRoot, Addr, Size);
    }
    Root = ExpressionInitRoot(Dest);
    if (Root != NULL && Root != Dest)
    {
        if (ExpressionByteRange(Root, Dest, &Off, &Len))
            ExpressionInitMarkRange(Root, Off, Len);
        else
            Root->IsInitialized = TRUE; /* not inside the object: alias, permissive */
        return; /* do not mark the whole object as initialized */
    }
    if (Dest->LValueFrom != NULL && Dest->LValueFrom->Typ != NULL
        && Dest->LValueFrom->Typ->Base != TypeArray)
        Dest->LValueFrom->IsInitialized = TRUE;
}

/* Mark a char buffer written by sprintf/snprintf/memcpy-like intrinsics.
 * Walks LValueFrom to the tracked array/struct root and marks bytes. */
/* Pointer passed to opaque library: whole object may be written. */
void ExpressionMarkPtrArgEscaped(struct Value *Arg)
{
    struct Value *V;
    int Guard = 0;
    if (Arg == NULL)
        return;
    ExpressionMarkInitialized(Arg);
    V = Arg;
    while (V != NULL && Guard++ < 8)
    {
        V->IsInitialized = TRUE;
        if (V->ElemInit != NULL)
        {
            /* mark entire tracked object */
            int n = V->ElemInitCount;
            if (n > 0)
                ExpressionInitMarkRange(V, 0, n);
            V->IsInitialized = TRUE;
            return;
        }
        if (V->LValueFrom == NULL || V->LValueFrom == V)
            return;
        V = V->LValueFrom;
    }
}

void ExpressionMarkBufferWritten(struct Value *DestArg, void *Buf, int Len)
{
    struct Value *V = DestArg;
    int Guard = 0;
    if (V == NULL)
        return;
    ExpressionMarkInitialized(V);
    while (V != NULL && Guard++ < 8)
    {
        if (V->ElemInit != NULL && Buf != NULL && Len > 0)
        {
            ExpressionInitMarkAddr(V, Buf, Len);
            return;
        }
        if (V->LValueFrom == NULL || V->LValueFrom == V)
        {
            V->IsInitialized = TRUE;
            return;
        }
        V = V->LValueFrom;
        ExpressionMarkInitialized(V);
    }
}


#ifdef DEBUG_EXPRESSIONS
#define debugf printf
#else
void debugf(char *Format, ...)
{
    (void)Format;
}
#endif

/* local prototypes */
enum OperatorOrder
{
    OrderNone,
    OrderPrefix,
    OrderInfix,
    OrderPostfix
};

/* a stack of expressions we use in evaluation */
struct ExpressionStack
{
    struct ExpressionStack *Next;       /* the next lower item on the stack */
    struct Value *Val;                  /* the value for this stack node */
    enum LexToken Op;                   /* the operator */
    short unsigned int Precedence;      /* the operator precedence of this node */
    unsigned char Order;                /* the evaluation order of this operator */
    short int Line;                     /* source line when this node was pushed */
    short int CharacterPos;             /* column for diagnostics (operand caret) */
};

/* operator precedence definitions */
struct OpPrecedence
{
    unsigned int PrefixPrecedence:4;
    unsigned int PostfixPrecedence:4;
    unsigned int InfixPrecedence:4;
    char *Name;
};

/* NOTE: the order of this array must correspond exactly to the order of these tokens in enum LexToken */
static struct OpPrecedence OperatorPrecedence[] =
{
    /* TokenNone, */ { 0, 0, 0, "none" },
    /* TokenComma, */ { 0, 0, 1, "," },
    /* TokenAssign, */ { 0, 0, 2, "=" }, /* TokenAddAssign, */ { 0, 0, 2, "+=" }, /* TokenSubtractAssign, */ { 0, 0, 2, "-=" },
    /* TokenMultiplyAssign, */ { 0, 0, 2, "*=" }, /* TokenDivideAssign, */ { 0, 0, 2, "/=" }, /* TokenModulusAssign, */ { 0, 0, 2, "%=" },
    /* TokenShiftLeftAssign, */ { 0, 0, 2, "<<=" }, /* TokenShiftRightAssign, */ { 0, 0, 2, ">>=" }, /* TokenArithmeticAndAssign, */ { 0, 0, 2, "&=" },
    /* TokenArithmeticOrAssign, */ { 0, 0, 2, "|=" }, /* TokenArithmeticExorAssign, */ { 0, 0, 2, "^=" },
    /* TokenQuestionMark, */ { 0, 0, 3, "?" }, /* TokenColon, */ { 0, 0, 3, ":" },
    /* TokenLogicalOr, */ { 0, 0, 4, "||" },
    /* TokenLogicalAnd, */ { 0, 0, 5, "&&" },
    /* TokenArithmeticOr, */ { 0, 0, 6, "|" },
    /* TokenArithmeticExor, */ { 0, 0, 7, "^" },
    /* TokenAmpersand, */ { 14, 0, 8, "&" },
    /* TokenEqual, */  { 0, 0, 9, "==" }, /* TokenNotEqual, */ { 0, 0, 9, "!=" },
    /* TokenLessThan, */ { 0, 0, 10, "<" }, /* TokenGreaterThan, */ { 0, 0, 10, ">" }, /* TokenLessEqual, */ { 0, 0, 10, "<=" }, /* TokenGreaterEqual, */ { 0, 0, 10, ">=" },
    /* TokenShiftLeft, */ { 0, 0, 11, "<<" }, /* TokenShiftRight, */ { 0, 0, 11, ">>" },
    /* TokenPlus, */ { 14, 0, 12, "+" }, /* TokenMinus, */ { 14, 0, 12, "-" },
    /* TokenAsterisk, */ { 14, 0, 13, "*" }, /* TokenSlash, */ { 0, 0, 13, "/" }, /* TokenModulus, */ { 0, 0, 13, "%" },
    /* TokenIncrement, */ { 14, 15, 0, "++" }, /* TokenDecrement, */ { 14, 15, 0, "--" }, /* TokenUnaryNot, */ { 14, 0, 0, "!" }, /* TokenUnaryExor, */ { 14, 0, 0, "~" }, /* TokenSizeof, */ { 14, 0, 0, "sizeof" }, /* TokenCast, */ { 14, 0, 0, "cast" },
    /* TokenLeftSquareBracket, */ { 0, 0, 15, "[" }, /* TokenRightSquareBracket, */ { 0, 15, 0, "]" }, /* TokenDot, */ { 0, 0, 15, "." }, /* TokenArrow, */ { 0, 0, 15, "->" },
    /* TokenOpenBracket, */ { 15, 0, 0, "(" }, /* TokenCloseBracket, */ { 0, 15, 0, ")" }
};

void ExpressionParseFunctionCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *FuncName, int RunIt);
void ExpressionParseFunctionCallValue(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *FuncValue, int RunIt);

#ifdef DEBUG_EXPRESSIONS
/* show the contents of the expression stack */
void ExpressionStackShow(Picoc *pc, struct ExpressionStack *StackTop)
{
    printf("Expression stack [0x%lx,0x%lx]: ", (long)pc->HeapStackTop, (long)StackTop);

    while (StackTop != NULL)
    {
        if (StackTop->Order == OrderNone)
        {
            /* it's a value */
            if (StackTop->Val->IsLValue)
                printf("lvalue=");
            else
                printf("value=");

            switch (StackTop->Val->Typ->Base)
            {
                case TypeVoid:      printf("void"); break;
                case TypeInt:       printf("%d:int", StackTop->Val->Val->Integer); break;
                case TypeShort:     printf("%d:short", StackTop->Val->Val->ShortInteger); break;
                case TypeChar:      printf("%d:char", StackTop->Val->Val->Character); break;
                case TypeLong:      printf("%ld:long", StackTop->Val->Val->LongInteger); break;
                case TypeUnsignedShort: printf("%d:unsigned short", StackTop->Val->Val->UnsignedShortInteger); break;
                case TypeUnsignedInt: printf("%d:unsigned int", StackTop->Val->Val->UnsignedInteger); break;
                case TypeUnsignedLong: printf("%ld:unsigned long", StackTop->Val->Val->UnsignedLongInteger); break;
                case TypeFP:        printf("%f:fp", StackTop->Val->Val->FP); break;
                case TypeFunction:  printf("%s:function", StackTop->Val->Val->Identifier); break;
                case TypeMacro:     printf("%s:macro", StackTop->Val->Val->Identifier); break;
                case TypePointer:
                    if (StackTop->Val->Val->Pointer == NULL)
                        printf("ptr(NULL)");
                    else if (StackTop->Val->Typ->FromType->Base == TypeChar)
                        printf("\"%s\":string", (char *)StackTop->Val->Val->Pointer);
                    else
                        printf("ptr(0x%lx)", (long)StackTop->Val->Val->Pointer);
                    break;
                case TypeArray:     printf("array"); break;
                case TypeStruct:    printf("%s:struct", StackTop->Val->Val->Identifier); break;
                case TypeUnion:     printf("%s:union", StackTop->Val->Val->Identifier); break;
                case TypeEnum:      printf("%s:enum", StackTop->Val->Val->Identifier); break;
                case Type_Type:     PrintType(StackTop->Val->Val->Typ, pc->CStdOut); printf(":type"); break;
                default:            printf("unknown"); break;
            }
            printf("[0x%lx,0x%lx]", (long)StackTop, (long)StackTop->Val);
        }
        else
        {
            /* it's an operator */
            printf("op='%s' %s %d", OperatorPrecedence[(int)StackTop->Op].Name,
                (StackTop->Order == OrderPrefix) ? "prefix" : ((StackTop->Order == OrderPostfix) ? "postfix" : "infix"),
                StackTop->Precedence);
            printf("[0x%lx]", (long)StackTop);
        }

        StackTop = StackTop->Next;
        if (StackTop != NULL)
            printf(", ");
    }

    printf("\n");
}
#endif

int IsTypeToken(struct ParseState * Parser, enum LexToken t, struct Value * LexValue)
{
    if (t >= TokenIntType && t <= TokenUnsignedType)
        return 1; /* base type */

    /* typedef'ed type? */
    if (t == TokenIdentifier) /* see TypeParseFront, case TokenIdentifier and ParseTypedef */
    {
        struct Value * VarValue;
        if (VariableDefined(Parser->pc, LexValue->Val->Pointer))
        {
            VariableGet(Parser->pc, Parser, LexValue->Val->Pointer, &VarValue);
            if (VarValue->Typ == &Parser->pc->TypeType)
                return 1;
        }
    }

    return 0;
}

/* ---- bitfield extract / insert (token path) ----
 * Container is the storage unit; bits are LSB-first within the unit.
 * Signed bitfields are sign-extended on extract. */
static unsigned long long BitFieldMask(int Width)
{
    if (Width <= 0)
        return 0;
    if (Width >= 64)
        return ~0ULL;
    return (1ULL << Width) - 1ULL;
}

static long long BitFieldExtract(const void *Container, int BitOffset, int BitWidth, int IsSigned)
{
    /* storage units are int-sized (4 bytes) in our layout model */
    unsigned int Word = 0;
    memcpy(&Word, Container, sizeof(Word));
    {
        unsigned long long Raw = ((unsigned long long)Word >> BitOffset) & BitFieldMask(BitWidth);
        if (IsSigned && BitWidth > 0 && (Raw & (1ULL << (BitWidth - 1))))
            Raw |= ~BitFieldMask(BitWidth);
        return (long long)Raw;
    }
}

static void BitFieldInsert(void *Container, int BitOffset, int BitWidth, long long Value)
{
    unsigned int Word = 0;
    unsigned int Mask = (unsigned int)(BitFieldMask(BitWidth) << BitOffset);
    unsigned int Field = (unsigned int)(((unsigned long long)Value << BitOffset) & Mask);
    memcpy(&Word, Container, sizeof(Word));
    Word = (Word & ~Mask) | Field;
    memcpy(Container, &Word, sizeof(Word));
}

static int TypeIsUnsignedBitFieldBase(struct ValueType *Typ)
{
    if (Typ == NULL)
        return 0;
    switch (Typ->Base)
    {
        case TypeUnsignedInt:
        case TypeUnsignedShort:
        case TypeUnsignedLong:
        case TypeUnsignedLongLong:
        case TypeUnsignedChar:
            return 1;
        default:
            return 0;
    }
}

PTR_TYPE_INT_SIGNED ExpressionCoerceInteger(struct Value *Val)
{
    if (Val->BitWidth > 0)
        return (PTR_TYPE_INT_SIGNED)BitFieldExtract(Val->Val, Val->BitOffset, Val->BitWidth,
            !TypeIsUnsignedBitFieldBase(Val->Typ));

    switch (Val->Typ->Base)
    {
        case TypeInt:               return (PTR_TYPE_INT_SIGNED)Val->Val->Integer;
        case TypeChar:              return (PTR_TYPE_INT_SIGNED)Val->Val->Character;
        case TypeShort:             return (PTR_TYPE_INT_SIGNED)Val->Val->ShortInteger;
        case TypeLong:              return (PTR_TYPE_INT_SIGNED)Val->Val->LongInteger;
        case TypeUnsignedInt:       return (PTR_TYPE_INT_SIGNED)Val->Val->UnsignedInteger;
        case TypeUnsignedShort:     return (PTR_TYPE_INT_SIGNED)Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:      return (PTR_TYPE_INT_SIGNED)Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong:  return (PTR_TYPE_INT_SIGNED)Val->Val->UnsignedLongLongInteger;
        case TypeLongLong:          return (PTR_TYPE_INT_SIGNED)Val->Val->LongLongInteger;
        case TypeUnsignedChar:      return (PTR_TYPE_INT_SIGNED)Val->Val->UnsignedCharacter;
        case TypePointer:           return (PTR_TYPE_INT_SIGNED)Val->Val->Pointer;
#ifndef NO_FP
        case TypeFP:                return (PTR_TYPE_INT_SIGNED)Val->Val->FP;
#endif
        default:                    return 0;
    }
}

PTR_TYPE_INT ExpressionCoerceUnsignedInteger(struct Value *Val)
{
    switch (Val->Typ->Base)
    {
        case TypeInt:               return (PTR_TYPE_INT)Val->Val->Integer;
        case TypeChar:              return (PTR_TYPE_INT)Val->Val->Character;
        case TypeShort:             return (PTR_TYPE_INT)Val->Val->ShortInteger;
        case TypeLong:              return (PTR_TYPE_INT)Val->Val->LongInteger;
        case TypeUnsignedInt:       return (PTR_TYPE_INT)Val->Val->UnsignedInteger;
        case TypeUnsignedShort:     return (PTR_TYPE_INT)Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:      return (PTR_TYPE_INT)Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong:  return (PTR_TYPE_INT)Val->Val->UnsignedLongLongInteger;
        case TypeLongLong:          return (PTR_TYPE_INT)Val->Val->LongLongInteger;
        case TypeUnsignedChar:      return (PTR_TYPE_INT)Val->Val->UnsignedCharacter;
        case TypePointer:           return (PTR_TYPE_INT)Val->Val->Pointer;
#ifndef NO_FP
        case TypeFP:                return (PTR_TYPE_INT)Val->Val->FP;
#endif
        default:                    return 0;
    }
}

#ifndef NO_FP
double ExpressionCoerceFP(struct Value *Val)
{
#ifndef BROKEN_FLOAT_CASTS
    int IntVal;
    unsigned long long UnsignedVal;

    switch (Val->Typ->Base)
    {
        case TypeInt:                 IntVal = Val->Val->Integer; return (double)IntVal;
        case TypeChar:                IntVal = Val->Val->Character; return (double)IntVal;
        case TypeShort:               IntVal = Val->Val->ShortInteger; return (double)IntVal;
        case TypeLong:                IntVal = Val->Val->LongInteger; return (double)IntVal;
        case TypeUnsignedInt:         UnsignedVal = Val->Val->UnsignedInteger; return (double)UnsignedVal;
        case TypeUnsignedShort:       UnsignedVal = Val->Val->UnsignedShortInteger; return (double)UnsignedVal;
        case TypeUnsignedLong:        UnsignedVal = Val->Val->UnsignedLongInteger; return (double)UnsignedVal;
        case TypeUnsignedLongLong:    UnsignedVal = Val->Val->UnsignedLongLongInteger; return (double)UnsignedVal;
        case TypeLongLong:            return (double)Val->Val->LongLongInteger;
        case TypeUnsignedChar:        UnsignedVal = Val->Val->UnsignedCharacter; return (double)UnsignedVal;
        case TypeFP:                  return Val->Val->FP;
        default:                      return 0.0;
    }
#else
    switch (Val->Typ->Base)
    {
        case TypeInt:                 return (double)Val->Val->Integer;
        case TypeChar:                return (double)Val->Val->Character;
        case TypeShort:               return (double)Val->Val->ShortInteger;
        case TypeLong:                return (double)Val->Val->LongInteger;
        case TypeUnsignedInt:         return (double)Val->Val->UnsignedInteger;
        case TypeUnsignedShort:       return (double)Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:        return (double)Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong:    return (double)Val->Val->UnsignedLongLongInteger;
        case TypeLongLong:            return (double)Val->Val->LongLongInteger;
        case TypeUnsignedChar:        return (double)Val->Val->UnsignedCharacter;
        case TypeFP:                  return (double)Val->Val->FP;
        default:                      return 0.0;
    }
#endif
}
#endif

/* assign an integer value */
PTR_TYPE_INT ExpressionAssignInt(struct ParseState *Parser, struct Value *DestValue, PTR_TYPE_INT_SIGNED FromInt, int After)
{
    PTR_TYPE_INT_SIGNED Result;

    if (!DestValue->IsLValue)
        ProgramFail(Parser, "can't assign to this");
    if (DestValue->IsConst || (DestValue->LValueFrom != NULL && DestValue->LValueFrom->IsConst))
        ProgramFail(Parser, "assignment of read-only variable");
    /* char *p = "lit"; p[0] = x; — string literals are read-only */
    if (DestValue->Val != NULL
        && VariableAddressIsStringLiteral(Parser->pc, (const void *)(DestValue->ValRaw != NULL ? DestValue->ValRaw : (void *)DestValue->Val)))
    {
        ParserSetCaretOnAssignTarget(Parser);
        ProgramFail(Parser, "assignment of read-only location (string literal)");
    }
    if (DestValue->Val != NULL)
        {
            int hs = 0;
            if (DestValue->Typ != NULL && DestValue->Typ->Sizeof > 0)
                hs = DestValue->Typ->Sizeof;
            else
                hs = TypeSizeValue(DestValue, TRUE);
            if (hs < 1) hs = (int)sizeof(int);
            BabycHeapCheckWrite(Parser, (void *)(DestValue->ValRaw != NULL ? DestValue->ValRaw : (void *)DestValue->Val), hs);
        }

    /* bitfield: read/write via extract/insert */
    if (DestValue->BitWidth > 0)
    {
        int IsSigned = !TypeIsUnsignedBitFieldBase(DestValue->Typ);
        if (After)
        {
            struct Value *Root = DestValue;
            int Guard = 0;
            while (Root->LValueFrom != NULL && Root->LValueFrom != Root && Guard++ < 16)
                Root = Root->LValueFrom;
            /* bitmap leaf or whole-object flag (same policy as normal path) */
            if (Root->IsLValue && !Root->IsInitialized && !TypeIsFunctionLike(Root)
                && (Root->ElemInit == NULL || !ExpressionLeafInit(Root, DestValue)))
                ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
            Result = (PTR_TYPE_INT_SIGNED)BitFieldExtract(DestValue->Val,
                DestValue->BitOffset, DestValue->BitWidth, IsSigned);
        }
        else
            Result = FromInt;
        BitFieldInsert(DestValue->Val, DestValue->BitOffset, DestValue->BitWidth, FromInt);
        VariableValueCommit(DestValue);
        ExpressionMarkInitialized(DestValue);
        return Result;
    }

    if (After)
    {
        struct Value *Root = DestValue;
        int RootUninit;
        int Guard = 0;
        /* Guard against LValueFrom cycles (array element postfix ++/--). */
        while (Root->LValueFrom != NULL && Root->LValueFrom != Root && Guard++ < 16)
            Root = Root->LValueFrom;
        RootUninit = (Root->IsLValue && !Root->IsInitialized && !TypeIsFunctionLike(Root));
        if (RootUninit && Root != DestValue && Root->ElemInit != NULL
            && ExpressionLeafInit(Root, DestValue))
            RootUninit = 0; /* the element itself was written: a[i]++ is fine */
        if (RootUninit)
            ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
        Result = ExpressionCoerceInteger(DestValue);
    }
    else
        Result = FromInt;

    switch (DestValue->Typ->Base)
    {
        case TypeInt:               DestValue->Val->Integer = (int)FromInt; break;
        case TypeShort:             DestValue->Val->ShortInteger = (short)FromInt; break;
        case TypeChar:              DestValue->Val->Character = (char)FromInt; break;
        case TypeLong:              DestValue->Val->LongInteger = BABYC_NARROW_LONG(FromInt); break;
        case TypeLongLong:          DestValue->Val->LongLongInteger = (long long)FromInt; break;
        case TypeUnsignedInt:       DestValue->Val->UnsignedInteger = (unsigned int)FromInt; break;
        case TypeUnsignedShort:     DestValue->Val->UnsignedShortInteger = (unsigned short)FromInt; break;
        case TypeUnsignedLong:      DestValue->Val->UnsignedLongInteger = BABYC_NARROW_ULONG(FromInt); break;
        case TypeUnsignedLongLong:  DestValue->Val->UnsignedLongLongInteger = (unsigned long long)FromInt; break;
        case TypeUnsignedChar:      DestValue->Val->UnsignedCharacter = (unsigned char)FromInt; break;
        default: break;
    }
    VariableValueCommit(DestValue);
    ExpressionMarkInitialized(DestValue);
    return Result;
}

#ifndef NO_FP
/* assign a floating point value */
double ExpressionAssignFP(struct ParseState *Parser, struct Value *DestValue, double FromFP)
{
    if (!DestValue->IsLValue)
        ProgramFail(Parser, "can't assign to this");
    if (DestValue->IsConst || (DestValue->LValueFrom != NULL && DestValue->LValueFrom->IsConst))
        ProgramFail(Parser, "assignment of read-only variable");

    /* C float assignments round at the semantic conversion boundary even
     * when the destination is an execution temporary (e.g. a call result).
     * A raw-memory commit alone is insufficient for return/argument slots. */
    if (DestValue->Typ != NULL && DestValue->Typ->Base == TypeFP
        && DestValue->Typ->FPFormat == FPStorageBinary32)
        FromFP = (double)(float)FromFP;
    DestValue->Val->FP = FromFP;
    VariableValueCommit(DestValue);
    ExpressionMarkInitialized(DestValue);
    return FromFP;
}
#endif

/* push a node on to the expression stack */
void ExpressionStackPushValueNode(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *ValueLoc)
{
    VariableValueRefresh(ValueLoc);
    struct ExpressionStack *StackNode = VariableAlloc(Parser->pc, Parser, sizeof(struct ExpressionStack), FALSE);
    StackNode->Next = *StackTop;
    StackNode->Val = ValueLoc;
    *StackTop = StackNode;
    StackNode->Line = Parser->Line;
    StackNode->CharacterPos = Parser->CharacterPos;
#ifdef FANCY_ERROR_MESSAGES
    /* kept for any other FANCY-only fields */
#endif
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* push a blank value on to the expression stack by type */
struct Value *ExpressionStackPushValueByType(struct ParseState *Parser, struct ExpressionStack **StackTop, struct ValueType *PushType)
{
    struct Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, PushType, FALSE, NULL, FALSE);
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);

    return ValueLoc;
}

/* push a value on to the expression stack */
void ExpressionStackPushValue(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *PushValue)
{
    struct Value *ValueLoc = VariableAllocValueAndCopy(Parser->pc, Parser, PushValue, FALSE);
    /* Keep a link to the original LValue so array→pointer decay does not
     * point into this temporary (which is freed when the expression ends). */
    if (PushValue != NULL && PushValue->IsLValue && ValueLoc->LValueFrom == NULL)
        ValueLoc->LValueFrom = (PushValue->LValueFrom != NULL) ? PushValue->LValueFrom : PushValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionStackPushLValue(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *PushValue, int Offset)
{
    struct Value *ValueLoc = VariableAllocValueShared(Parser, PushValue);
    ValueLoc->Val = (void *)((char *)ValueLoc->Val + Offset);
    ValueLoc->LValueFrom = PushValue;
    ValueLoc->IsInitialized = PushValue->IsInitialized;
    ValueLoc->IsConst = PushValue->IsConst;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionStackPushDereference(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *DereferenceValue)
{
    struct Value *DerefVal;
    struct Value *ValueLoc;
    int Offset;
    struct ValueType *DerefType;
    int DerefIsLValue;
    void *DerefDataLoc;

    if (DereferenceValue->Typ->Base == TypePointer &&
        DereferenceValue->Typ->FromType != NULL &&
        DereferenceValue->Typ->FromType->Base == TypeFunction)
    {
        struct Value *Fn = (struct Value *)DereferenceValue->Val->Pointer;
        if (Fn == NULL)
            ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
        ExpressionStackPushValueNode(Parser, StackTop, Fn);
        return;
    }

    /* A dereferenced multidimensional array yields an array lvalue. If that
     * row is itself the operand of another unary *, C array decay means its
     * storage address is the pointer value; the first scalar is not a pointer
     * word. Keep this rule local to expression dereference (not all memory
     * helpers), whose broader array payload conventions differ. */
    if (DereferenceValue->Typ != NULL && DereferenceValue->Typ->Base == TypeArray
        && DereferenceValue->Typ->FromType != NULL && DereferenceValue->Val != NULL)
    {
        DerefVal = NULL;
        Offset = 0;
        DerefType = DereferenceValue->Typ->FromType;
        DerefIsLValue = TRUE;
        DerefDataLoc = (void *)DereferenceValue->Val;
    }
    else
        DerefDataLoc = VariableDereferencePointer(Parser, DereferenceValue, &DerefVal, &Offset, &DerefType, &DerefIsLValue);
    if (DerefDataLoc == NULL)
        ProgramFail(Parser, DiagnosticMsgNullPointerDereference);

    ValueLoc = VariableAllocValueFromExistingData(Parser, DerefType, (union AnyValue *)DerefDataLoc, DerefIsLValue, DerefVal);
    /* Pointer targets are not tracked per-object yet — be permissive. */
    ValueLoc->IsInitialized = TRUE;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionPushInt(struct ParseState *Parser, struct ExpressionStack **StackTop, PTR_TYPE_INT_SIGNED IntValue)
{
    struct Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->IntType, FALSE, NULL, FALSE);
    ValueLoc->Val->Integer = (int)IntValue;
    ValueLoc->Val->LongInteger = BABYC_NARROW_LONG(IntValue);
    ValueLoc->Val->UnsignedLongInteger = BABYC_NARROW_ULONG(IntValue);

    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

/* push a long value — used when usual arithmetic conversions yield long */
void ExpressionPushLong(struct ParseState *Parser, struct ExpressionStack **StackTop, long LongValue)
{
    struct Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->LongType, FALSE, NULL, FALSE);
    ValueLoc->Val->LongInteger = LongValue;
    ValueLoc->Val->LongLongInteger = (long long)LongValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

/* push a long long value on to the expression stack */
void ExpressionPushLongLong(struct ParseState* Parser, struct ExpressionStack** StackTop, long long LLValue)
{
    struct Value* ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->LongLongType, FALSE, NULL, FALSE);
    ValueLoc->Val->LongLongInteger = LLValue;  // scrive solo il campo corretto
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

// AGGIUNTA - variante unsigned mancante
void ExpressionPushUnsignedLongLong(struct ParseState* Parser, struct ExpressionStack** StackTop, unsigned long long ULLValue)
{
    struct Value* ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->UnsignedLongLongType, FALSE, NULL, FALSE);
    ValueLoc->Val->UnsignedLongLongInteger = ULLValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

/* assign a long long value and return the result */
long long ExpressionAssignLongLong(struct ParseState *Parser, struct Value *DestValue, long long FromLL, int After)
{
    long long Result;

    if (!DestValue->IsLValue)
        ProgramFail(Parser, "can't assign to this");
    if (DestValue->IsConst || (DestValue->LValueFrom != NULL && DestValue->LValueFrom->IsConst))
        ProgramFail(Parser, "assignment of read-only variable");
    if (DestValue->Val != NULL
        && VariableAddressIsStringLiteral(Parser->pc, (const void *)(DestValue->ValRaw != NULL ? DestValue->ValRaw : (void *)DestValue->Val)))
    {
        ParserSetCaretOnAssignTarget(Parser);
        ProgramFail(Parser, "assignment of read-only location (string literal)");
    }
    if (DestValue->Val != NULL)
        {
            int hs = 0;
            if (DestValue->Typ != NULL && DestValue->Typ->Sizeof > 0)
                hs = DestValue->Typ->Sizeof;
            else
                hs = TypeSizeValue(DestValue, TRUE);
            if (hs < 1) hs = (int)sizeof(int);
            BabycHeapCheckWrite(Parser, (void *)(DestValue->ValRaw != NULL ? DestValue->ValRaw : (void *)DestValue->Val), hs);
        }

    if (After)
        Result = (long long)ExpressionCoerceInteger(DestValue);
    else
        Result = FromLL;

    switch (DestValue->Typ->Base)
    {
        case TypeLongLong:          DestValue->Val->LongLongInteger = FromLL; break;
        case TypeUnsignedLongLong:  DestValue->Val->UnsignedLongLongInteger = (unsigned long long)FromLL; break;
        case TypeInt:               DestValue->Val->Integer = (int)FromLL; break;
        case TypeLong:              DestValue->Val->LongInteger = BABYC_NARROW_LONG(FromLL); break;
        case TypeUnsignedLong:      DestValue->Val->UnsignedLongInteger = (unsigned long)FromLL; break;
        case TypeUnsignedInt:       DestValue->Val->UnsignedInteger = (unsigned int)FromLL; break;
        default: break;
    }
    VariableValueCommit(DestValue);
    ExpressionMarkInitialized(DestValue);
    return Result;
}

#ifndef NO_FP
void ExpressionPushFP(struct ParseState *Parser, struct ExpressionStack **StackTop, double FPValue)
{
    struct Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->FPType, FALSE, NULL, FALSE);
    ValueLoc->Val->FP = FPValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}
#endif

/* char and unsigned char are interchangeable as pointed-to types (C practice) */
static int CharPtrTargetsCompatible(struct ValueType *A, struct ValueType *B)
{
    if (A == NULL || B == NULL)
        return 0;
    if (A == B)
        return 1;
    if ((A->Base == TypeChar || A->Base == TypeUnsignedChar) &&
        (B->Base == TypeChar || B->Base == TypeUnsignedChar))
        return 1;
    return 0;
}

/* True if this pointer type is (or points to) void, or is any struct/union pointer.
 * Used to allow automatic void* <-> struct* (and any pointer) casts in the usual
 * C-ish places: assignment, comparison with NULL, library returns, etc. */
static int IsVoidOrStructPointer(struct ValueType *Typ)
{
    struct ValueType *From;
    if (Typ == NULL || Typ->Base != TypePointer)
        return 0;
    From = Typ->FromType;
    if (From == NULL)
        return 1;
    if (From->Base == TypeVoid)
        return 1;
    if (From->Base == TypeStruct || From->Base == TypeUnion)
        return 1;
    return 0;
}

/* assign to a pointer */
void ExpressionAssignToPointer(struct ParseState *Parser, struct Value *ToValue, struct Value *FromValue, const char *FuncName, int ParamNo, int AllowPointerCoercion)
{
    struct ValueType *PointedToType = ToValue->Typ->FromType;

    /* Pointer storage has separate state from the object it aliases. Carry
     * the tracked root across parameter/local pointer copies when known. */
    ToValue->InitAliasRoot = NULL;
    if (ExpressionAliasInitTrackingEnabled()
        && FromValue != NULL && FromValue->Typ != NULL)
    {
        if (FromValue->Typ->Base == TypeArray)
        {
            struct Value *Root = ExpressionInitRoot(FromValue);
            /* Only retain stable tracked roots; an untracked array expression
             * may be a temporary whose lifetime ends at this assignment. */
            ToValue->InitAliasRoot = Root;
        }
        else if (FromValue->Typ->Base == TypePointer)
            ToValue->InitAliasRoot = FromValue->InitAliasRoot;
    }

    /* Function designator → void* / function pointer: pass the persistent
     * Func Value* so library trampolines (pthread_create, CreateThread, …)
     * can invoke interpreted start routines. Must run before the void*
     * free-assign branch, which would copy an unset Pointer field. */
    if (FromValue->Typ != NULL && FromValue->Typ->Base == TypeFunction)
    {
        ToValue->Val->Pointer = (void *)(FromValue->LValueFrom != NULL
            ? FromValue->LValueFrom : FromValue);
        return;
    }

    /* Same type, or either side is void*, or either side is a struct/union pointer:
     * treat as freely assignable (automatic cast, as in C).
     * Special case: array → void* must take the address of the array data,
     * not the (usually unset) Pointer field of the array Value. */
    if (FromValue->Typ == ToValue->Typ
        || FromValue->Typ == Parser->pc->VoidPtrType
        || ToValue->Typ == Parser->pc->VoidPtrType
        || (FromValue->Typ->Base == TypePointer && ToValue->Typ->Base == TypePointer
            && (IsVoidOrStructPointer(FromValue->Typ) || IsVoidOrStructPointer(ToValue->Typ))))
    {
        if (FromValue->Typ->Base == TypeArray && FromValue->Val != NULL)
        {
            /* LValueFrom only stands in for the same array (not for a row or
             * member of a bigger object: m[1] is not &m[0]). */
            struct Value *Src = FromValue;
            if (Src->LValueFrom != NULL && Src->LValueFrom->Typ == Src->Typ)
                Src = Src->LValueFrom;
            ToValue->Val->Pointer = (void *)(char *)Src->Val;
        }
        else
            ToValue->Val->Pointer = FromValue->Val->Pointer;
    }

    else if (FromValue->Typ->Base == TypePointer && ToValue->Typ->Base == TypePointer
             && FromValue->Typ->FromType != NULL && ToValue->Typ->FromType != NULL
             && FromValue->Typ->FromType->Base == TypePointer
             && ToValue->Typ->FromType->Base == TypePointer)
    {
        /* pointer-to-pointer of any kind (e.g. char** <-> void**) */
        ToValue->Val->Pointer = FromValue->Val->Pointer;
    }

    else if (FromValue->Typ->Base == TypePointer && CharPtrTargetsCompatible(PointedToType, FromValue->Typ->FromType))
    {
        /* char* <-> unsigned char* (and const variants treated the same) */
        ToValue->Val->Pointer = FromValue->Val->Pointer;
    }

    else if (FromValue->Typ->Base == TypeArray &&
             (PointedToType == FromValue->Typ->FromType ||
              CharPtrTargetsCompatible(PointedToType, FromValue->Typ->FromType) ||
              ToValue->Typ == Parser->pc->VoidPtrType))
    {
        /* the form is: blah *x = array of blah  (incl. char[] -> unsigned char*)
         * Prefer the original LValue storage so we do not point into a temporary. */
        struct Value *Src = FromValue;
        if (Src->LValueFrom != NULL && Src->LValueFrom->Typ == Src->Typ)
            Src = Src->LValueFrom; /* same array only: a row / member must keep its own address */
        ToValue->Val->Pointer = (void *)(char *)Src->Val;
    }
    else if (FromValue->Typ->Base == TypePointer && FromValue->Typ->FromType->Base == TypeArray &&
               (PointedToType == FromValue->Typ->FromType->FromType ||
                CharPtrTargetsCompatible(PointedToType, FromValue->Typ->FromType->FromType) ||
                ToValue->Typ == Parser->pc->VoidPtrType) )
    {
        /* the form is: blah *x = pointer to array of blah */
        ToValue->Val->Pointer = VariableDereferencePointer(Parser, FromValue, NULL, NULL, NULL, NULL);
    }
    else if (IS_NUMERIC_COERCIBLE(FromValue) && ExpressionCoerceInteger(FromValue) == 0)
    {
        /* null pointer assignment */
        ToValue->Val->Pointer = NULL;
    }
    else if (AllowPointerCoercion && IS_NUMERIC_COERCIBLE(FromValue))
    {
        /* assign integer to native pointer */
        ToValue->Val->Pointer = (void *)(unsigned long)ExpressionCoerceUnsignedInteger(FromValue);
    }
    else if (AllowPointerCoercion && FromValue->Typ->Base == TypePointer)
    {
        /* assign a pointer to a pointer to a different type */
        ToValue->Val->Pointer = FromValue->Val->Pointer;
    }
    else if (FromValue->Typ->Base == TypeFunction)
    {
        /* Allow a script function where a void* / function-pointer is
         * expected (e.g. sqlite3_exec callback argument).
         * We pass the persistent Value* of the function (via LValueFrom
         * when available) so that native wrappers can invoke the script
         * function through a trampoline. */
        ToValue->Val->Pointer = (void *)(FromValue->LValueFrom != NULL ? FromValue->LValueFrom : FromValue);
    }
    else if (FromValue->Typ->Base == TypePointer &&
             FromValue->Typ->FromType != NULL &&
             FromValue->Typ->FromType->Base == TypeFunction &&
             PointedToType != NULL && PointedToType->Base == TypeFunction)
    {
        /* function* = function*  (e.g. int (*f)(int) = &fred) */
        ToValue->Val->Pointer = FromValue->Val->Pointer;
    }
    else
        AssignFail(Parser, "%t from %t", ToValue->Typ, FromValue->Typ, 0, 0, FuncName, ParamNo);
}

/* assign any kind of value */
void ExpressionAssign(struct ParseState *Parser, struct Value *DestValue, struct Value *SourceValue, int Force, const char *FuncName, int ParamNo, int AllowPointerCoercion)
{
    if (!DestValue->IsLValue && !Force)
        AssignFail(Parser, "not an lvalue", NULL, NULL, 0, 0, FuncName, ParamNo);

    /* const: forbid modification after initialisation (Force is used by casts, not by '=' ) */
    if (DestValue->IsConst && !Force)
        AssignFail(Parser, "assignment of read-only variable", NULL, NULL, 0, 0, FuncName, ParamNo);
    if (DestValue->LValueFrom != NULL && DestValue->LValueFrom->IsConst && !Force)
        AssignFail(Parser, "assignment of read-only variable", NULL, NULL, 0, 0, FuncName, ParamNo);
    if (!Force && DestValue->Val != NULL
        && VariableAddressIsStringLiteral(Parser->pc, (const void *)(DestValue->ValRaw != NULL ? DestValue->ValRaw : (void *)DestValue->Val)))
    {
        ParserSetCaretOnAssignTarget(Parser);
        AssignFail(Parser, "assignment of read-only location (string literal)", NULL, NULL, 0, 0, FuncName, ParamNo);
    }
    if (!Force && DestValue->Val != NULL)
        {
            int hs = 0;
            if (DestValue->Typ != NULL && DestValue->Typ->Sizeof > 0)
                hs = DestValue->Typ->Sizeof;
            else
                hs = TypeSizeValue(DestValue, TRUE);
            if (hs < 1) hs = (int)sizeof(int);
            BabycHeapCheckWrite(Parser, (void *)(DestValue->ValRaw != NULL ? DestValue->ValRaw : (void *)DestValue->Val), hs);
        }

    /* bitfield destination: insert into storage unit */
    if (DestValue->BitWidth > 0)
    {
        long long Src = ExpressionCoerceInteger(SourceValue);
        BitFieldInsert(DestValue->Val, DestValue->BitOffset, DestValue->BitWidth, Src);
        VariableValueCommit(DestValue);
        ExpressionMarkInitialized(DestValue);
        return;
    }

    if (IS_NUMERIC_COERCIBLE(DestValue) && !IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion))
        AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

    switch (DestValue->Typ->Base)
    {
        case TypeInt:               DestValue->Val->Integer = (int)ExpressionCoerceInteger(SourceValue); break;
        case TypeShort:             DestValue->Val->ShortInteger = (short)ExpressionCoerceInteger(SourceValue); break;
        case TypeChar:              DestValue->Val->Character = (char)ExpressionCoerceInteger(SourceValue); break;
        case TypeLong:              DestValue->Val->LongInteger = BABYC_NARROW_LONG(ExpressionCoerceInteger(SourceValue)); break;
        case TypeUnsignedInt:       DestValue->Val->UnsignedInteger = (unsigned int)ExpressionCoerceUnsignedInteger(SourceValue); break;
        case TypeUnsignedShort:     DestValue->Val->UnsignedShortInteger = (unsigned short)ExpressionCoerceUnsignedInteger(SourceValue); break;
        case TypeUnsignedLong:      DestValue->Val->UnsignedLongInteger = BABYC_NARROW_ULONG(ExpressionCoerceUnsignedInteger(SourceValue)); break;
        case TypeUnsignedChar:      DestValue->Val->UnsignedCharacter = (unsigned char)ExpressionCoerceUnsignedInteger(SourceValue); break;
        case TypeUnsignedLongLong:  DestValue->Val->UnsignedLongLongInteger = ExpressionCoerceUnsignedInteger(SourceValue); break;
        case TypeLongLong:          DestValue->Val->LongLongInteger = (long long)ExpressionCoerceInteger(SourceValue); break;

#ifndef NO_FP
        case TypeFP:
            if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion))
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            DestValue->Val->FP = ExpressionCoerceFP(SourceValue);
            /* Function return slots and some forced assignments use the
             * generic assignment path rather than ExpressionAssignFP().
             * Preserve C's float conversion at this boundary as well. */
            if (DestValue->Typ->FPFormat == FPStorageBinary32)
                DestValue->Val->FP = (double)(float)DestValue->Val->FP;
            break;
#endif
        case TypePointer:
            ExpressionAssignToPointer(Parser, DestValue, SourceValue, FuncName, ParamNo, AllowPointerCoercion);
            break;

        case TypeArray:
            if (SourceValue->Typ->Base == TypeArray && SourceValue->Typ->FromType == DestValue->Typ->FromType && DestValue->Typ->ArraySize == 0)
            {
                /* destination array is unsized - need to resize the destination array to the same size as the source array */
                DestValue->Typ = SourceValue->Typ;
                VariableRealloc(Parser, DestValue, TypeSizeValue(DestValue, FALSE));

                if (DestValue->LValueFrom != NULL)
                {
                    /* copy the resized value back to the LValue */
                    DestValue->LValueFrom->Val = DestValue->Val;
                    DestValue->LValueFrom->AnyValOnHeap = DestValue->AnyValOnHeap;
                }
            }

            /* char array = "abcd" */
            if (DestValue->Typ->FromType->Base == TypeChar && SourceValue->Typ->Base == TypePointer && SourceValue->Typ->FromType->Base == TypeChar)
            {
                if (DestValue->Typ->ArraySize == 0) /* char x[] = "abcd", x is unsized */
                {
                    int Size = strlen(SourceValue->Val->Pointer) + 1;
                    #ifdef DEBUG_ARRAY_INITIALIZER
                    PRINT_SOURCE_POS;
                    fprintf(stderr, "str size: %d\n", Size);
                    #endif
                    DestValue->Typ = TypeGetMatching(Parser->pc, Parser, DestValue->Typ->FromType, DestValue->Typ->Base, Size, DestValue->Typ->Identifier, TRUE);
                    VariableRealloc(Parser, DestValue, TypeSizeValue(DestValue, FALSE));
                }
                /* else, it's char x[10] = "abcd" */

                #ifdef DEBUG_ARRAY_INITIALIZER
                PRINT_SOURCE_POS;
                fprintf(stderr, "char[%d] from char* (len=%d)\n", DestValue->Typ->ArraySize, strlen(SourceValue->Val->Pointer));
                #endif
                /* A fixed-size char array may be larger than the source
                 * string literal. Copy only the available literal bytes and
                 * zero-fill the rest; copying the destination size reads past
                 * the literal (and is undefined when the array is larger). */
                {
                    size_t DestSize = (size_t)TypeSizeValue(DestValue, FALSE);
                    size_t SrcSize = strlen(SourceValue->Val->Pointer) + 1;
                    size_t CopySize = DestSize < SrcSize ? DestSize : SrcSize;
                    memcpy((void *)DestValue->Val, SourceValue->Val->Pointer, CopySize);
                    if (DestSize > CopySize)
                        memset((char *)DestValue->Val + CopySize, 0, DestSize - CopySize);
                }
                break;
            }

            if (DestValue->Typ != SourceValue->Typ)
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            if (DestValue->Typ->ArraySize != SourceValue->Typ->ArraySize)
                AssignFail(Parser, "from an array of size %d to one of size %d", NULL, NULL, DestValue->Typ->ArraySize, SourceValue->Typ->ArraySize, FuncName, ParamNo);

            memcpy((void *)DestValue->Val, (void *)SourceValue->Val, TypeSizeValue(DestValue, FALSE));
            break;

        case TypeStruct:
        case TypeUnion:
            if (DestValue->Typ != SourceValue->Typ)
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            memcpy((void *)DestValue->Val, (void *)SourceValue->Val, TypeSizeValue(SourceValue, FALSE));
            break;

        case TypeVoid:
            /* (void)expr — discard value, legal in C */
            break;

        default:
            AssignFail(Parser, "%t", DestValue->Typ, NULL, 0, 0, FuncName, ParamNo);
            break;
    }
    VariableValueCommit(DestValue);
    ExpressionMarkInitialized(DestValue);
}

/* evaluate the first half of a ternary operator x ? y : z */
/* evaluate the first half of a ternary operator x ? y : z */
void ExpressionQuestionMarkOperator(struct ParseState* Parser, struct ExpressionStack** StackTop, struct Value* BottomValue, struct Value* TopValue)
{
    long long Cond = 0;

    /* Accetta numerici e anche puntatori/array (come in ExpressionParseInt).
     * In C an array expression is never null (decays to pointer to first element). */
    if (IS_NUMERIC_COERCIBLE(TopValue))
    {
        Cond = ExpressionCoerceInteger(TopValue);
    }
    else if (TopValue->Typ->Base == TypeArray)
    {
        Cond = 1;
    }
    else if (TopValue->Typ->Base == TypePointer)
    {
        Cond = (TopValue->Val->Pointer != NULL) ? 1 : 0;
    }
    else
    {
        ProgramFail(Parser, "first argument to '?' should be a number or pointer");
    }

    if (Cond)
    {
        /* the condition's true, return the BottomValue */
        ExpressionStackPushValue(Parser, StackTop, BottomValue);
    }
    else
    {
        /* the condition's false, return void */
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->VoidType);
    }
}

/* evaluate the second half of a ternary operator x ? y : z */
void ExpressionColonOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *BottomValue, struct Value *TopValue)
{
    if (TopValue->Typ->Base == TypeVoid)
    {
        /* invoke the "else" part - return the BottomValue */
        ExpressionStackPushValue(Parser, StackTop, BottomValue);
    }
    else
    {
        /* it was a "then" - return the TopValue */
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    }
}

/* evaluate a prefix operator */
void ExpressionPrefixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, struct Value *TopValue)
{
    struct Value *Result;
    union AnyValue *ValPtr;

    debugf("ExpressionPrefixOperator()\n");
    switch (Op)
    {
        case TokenAmpersand:
            /* Functions are not classic LValues but C allows &func. */
            if (!TopValue->IsLValue && TopValue->Typ->Base != TypeFunction)
                ProgramFail(Parser, "can't get the address of this");

            if (TopValue->Typ->Base == TypeFunction)
            {
                /* Pointer to the function Value itself (persistent). */
                struct Value *Fn = (TopValue->LValueFrom != NULL) ? TopValue->LValueFrom : TopValue;
                Result = VariableAllocValueFromType(Parser->pc, Parser,
                    TypeGetMatching(Parser->pc, Parser, TopValue->Typ, TypePointer, 0, Parser->pc->StrEmpty, TRUE),
                    FALSE, NULL, FALSE);
                Result->Val->Pointer = (void *)Fn;
            }
            else
            {
                ValPtr = (union AnyValue *)(TopValue->ValRaw != NULL ? TopValue->ValRaw : (void *)TopValue->Val);
                Result = VariableAllocValueFromType(Parser->pc, Parser, TypeGetMatching(Parser->pc, Parser, TopValue->Typ, TypePointer, 0, Parser->pc->StrEmpty, TRUE), FALSE, NULL, FALSE);
                Result->Val->Pointer = (void *)ValPtr;
            }
            ExpressionStackPushValueNode(Parser, StackTop, Result);
            break;

        case TokenAsterisk:
            ExpressionStackPushDereference(Parser, StackTop, TopValue);
            break;

        case TokenSizeof:
            /* return the size of the argument */
            if (TopValue->Typ == &Parser->pc->TypeType)
                ExpressionPushInt(Parser, StackTop, TypeSize(TopValue->Val->Typ, TopValue->Val->Typ->ArraySize, TRUE));
            else if (!TopValue->IsLValue
                     && TopValue->LValueFrom == NULL
                     && TopValue->Typ == Parser->pc->CharPtrType
                     && TopValue->Val->Pointer != NULL
                     && VariableStringLiteralGet(Parser->pc, (char *)TopValue->Val->Pointer) != NULL)
            {
                /* String literals are stored as char* (decay) for most expressions, but
                 * in C sizeof("x") applies to the array type char[N], including NUL.
                 * Only treat as literal when the operand is NOT an lvalue (and has no
                 * LValueFrom root): otherwise
                 *   char *p = "x"; sizeof(p);
                 * would wrongly return strlen+1 instead of sizeof(char*). */
                ExpressionPushInt(Parser, StackTop,
                    (int)strlen((const char *)TopValue->Val->Pointer) + 1);
            }
            else
                ExpressionPushInt(Parser, StackTop, TypeSize(TopValue->Typ, TopValue->Typ->ArraySize, TRUE));
            break;

        default:
            /* an arithmetic operator */
#ifndef NO_FP
            if ((TopValue->Typ != NULL && TopValue->Typ->Base == TypeFP))
            {
                /* floating point prefix arithmetic */
                double ResultFP = 0.0;

                switch (Op)
                {
                    case TokenPlus:         ResultFP = TopValue->Val->FP; break;
                    case TokenMinus:        ResultFP = -TopValue->Val->FP; break;
                    case TokenIncrement:    ResultFP = ExpressionAssignFP(Parser, TopValue, TopValue->Val->FP+1); break;
                    case TokenDecrement:    ResultFP = ExpressionAssignFP(Parser, TopValue, TopValue->Val->FP-1); break;
                    case TokenUnaryNot:     ResultFP = !TopValue->Val->FP; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

                ExpressionPushFP(Parser, StackTop, ResultFP);
            }
            else
#endif
            if (IS_LONGLONG_COERCIBLE(TopValue))
            {
                /* 64-bit prefix arithmetic */
                long long ResultLL = 0;
                long long TopLL = (long long)ExpressionCoerceInteger(TopValue);
                int IsUnsigned = (TopValue->Typ->Base == TypeUnsignedLongLong);
                switch (Op)
                {
                    case TokenPlus:         ResultLL = TopLL; break;
                    case TokenMinus:        ResultLL = -TopLL; break;
                    case TokenIncrement:    ResultLL = ExpressionAssignLongLong(Parser, TopValue, TopLL+1, FALSE); break;
                    case TokenDecrement:    ResultLL = ExpressionAssignLongLong(Parser, TopValue, TopLL-1, FALSE); break;
                    case TokenUnaryNot:     ResultLL = !TopLL; break;
                    case TokenUnaryExor:    ResultLL = ~TopLL; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }
                if (IsUnsigned)
                    ExpressionPushUnsignedLongLong(Parser, StackTop, (unsigned long long)ResultLL);
                else
                    ExpressionPushLongLong(Parser, StackTop, ResultLL);
            }
            else
            if (IS_NUMERIC_COERCIBLE(TopValue))
            {
                /* integer prefix arithmetic */
                PTR_TYPE_INT_SIGNED ResultInt = 0;
                PTR_TYPE_INT_SIGNED TopInt = ExpressionCoerceInteger(TopValue);
                switch (Op)
                {
                    case TokenPlus:         ResultInt = TopInt; break;
                    case TokenMinus:        ResultInt = -TopInt; break;
                    case TokenIncrement:
                        ResultInt = (int)ExpressionAssignInt(Parser, TopValue, TopInt+1, FALSE);
                        /* Prefix ++ returns the value after conversion back to
                         * the lvalue type, not the unconverted promoted sum. */
                        if (TopValue->Typ != NULL
                            && (TopValue->Typ->Base == TypeChar
                                || TopValue->Typ->Base == TypeUnsignedChar
                                || TopValue->Typ->Base == TypeShort
                                || TopValue->Typ->Base == TypeUnsignedShort))
                            ResultInt = (int)ExpressionCoerceInteger(TopValue);
                        break;
                    case TokenDecrement:
                        ResultInt = (int)ExpressionAssignInt(Parser, TopValue, TopInt-1, FALSE);
                        if (TopValue->Typ != NULL
                            && (TopValue->Typ->Base == TypeChar
                                || TopValue->Typ->Base == TypeUnsignedChar
                                || TopValue->Typ->Base == TypeShort
                                || TopValue->Typ->Base == TypeUnsignedShort))
                            ResultInt = (int)ExpressionCoerceInteger(TopValue);
                        break;
                    case TokenUnaryNot:     ResultInt = !TopInt; break;
                    case TokenUnaryExor:    ResultInt = ~TopInt; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

                ExpressionPushInt(Parser, StackTop, ResultInt);
            }
            else if (TopValue->Typ->Base == TypePointer)
            {
                /* pointer prefix operators */
                if (Op == TokenUnaryNot)
                {
                    /* !ptr  →  true if pointer is NULL, false otherwise */
                    ExpressionPushInt(Parser, StackTop, TopValue->Val->Pointer == NULL);
                }
                else
                {
                    int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
                    struct Value *StackValue;
                    void *ResultPtr;

                    if (TopValue->Val->Pointer == NULL)
                        ProgramFail(Parser, "invalid use of a NULL pointer");

                    if (!TopValue->IsLValue)
                        ProgramFail(Parser, "can't assign to this");

                    switch (Op)
                    {
                        case TokenIncrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer + Size); break;
                        case TokenDecrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer - Size); break;
                        default:                ProgramFail(Parser, "invalid operation"); break;
                    }

                    ResultPtr = TopValue->Val->Pointer;
                    StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
                    StackValue->Val->Pointer = ResultPtr;
                }
            }
            else
                ProgramFail(Parser, "invalid operation");
            break;
    }
}

/* evaluate a postfix operator */
void ExpressionPostfixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, struct Value *TopValue)
{
    debugf("ExpressionPostfixOperator()\n");
#ifndef NO_FP
    if ((TopValue->Typ != NULL && TopValue->Typ->Base == TypeFP))
    {
        /* floating point prefix arithmetic */
        double ResultFP = 0.0;

        switch (Op)
        {
            case TokenIncrement:    ResultFP = ExpressionAssignFP(Parser, TopValue, TopValue->Val->FP+1); break;
            case TokenDecrement:    ResultFP = ExpressionAssignFP(Parser, TopValue, TopValue->Val->FP-1); break;
            default:                ProgramFail(Parser, "invalid operation"); break;
        }

        ExpressionPushFP(Parser, StackTop, ResultFP);
    }
    else
#endif
    if (IS_LONGLONG_COERCIBLE(TopValue))
    {
        long long ResultLL = 0;
        long long TopLL = (long long)ExpressionCoerceInteger(TopValue);
        int IsUnsigned = (TopValue->Typ->Base == TypeUnsignedLongLong);
        switch (Op)
        {
            case TokenIncrement:            ResultLL = ExpressionAssignLongLong(Parser, TopValue, TopLL+1, TRUE); break;
            case TokenDecrement:            ResultLL = ExpressionAssignLongLong(Parser, TopValue, TopLL-1, TRUE); break;
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }
        if (IsUnsigned)
            ExpressionPushUnsignedLongLong(Parser, StackTop, (unsigned long long)ResultLL);
        else
            ExpressionPushLongLong(Parser, StackTop, ResultLL);
    }
    else
    if (IS_NUMERIC_COERCIBLE(TopValue))
    {
        PTR_TYPE_INT_SIGNED ResultInt = 0;
        PTR_TYPE_INT_SIGNED TopInt = ExpressionCoerceInteger(TopValue);
        switch (Op)
        {
            case TokenIncrement:            ResultInt = (int)ExpressionAssignInt(Parser, TopValue, TopInt+1, TRUE); break;
            case TokenDecrement:            ResultInt = (int)ExpressionAssignInt(Parser, TopValue, TopInt-1, TRUE); break;
            case TokenRightSquareBracket:   ProgramFail(Parser, "not supported"); break;  /* XXX */
            case TokenCloseBracket:         ProgramFail(Parser, "not supported"); break;  /* XXX */
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        ExpressionPushInt(Parser, StackTop, ResultInt);
    }
    else if (TopValue->Typ->Base == TypePointer)
    {
        /* pointer postfix arithmetic */
        int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
        struct Value *StackValue;
        void *OrigPointer = TopValue->Val->Pointer;

        if (TopValue->Val->Pointer == NULL)
            ProgramFail(Parser, "invalid use of a NULL pointer");

        if (!TopValue->IsLValue)
            ProgramFail(Parser, "can't assign to this");

        switch (Op)
        {
            case TokenIncrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer + Size); break;
            case TokenDecrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer - Size); break;
            default:                ProgramFail(Parser, "invalid operation"); break;
        }

        StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
        StackValue->Val->Pointer = OrigPointer;
    }
    else
        ProgramFail(Parser, "invalid operation");
}

/* evaluate an infix operator */
void ExpressionInfixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, struct Value *BottomValue, struct Value *TopValue)
{
    PTR_TYPE_INT_SIGNED ResultInt = 0;
    struct Value *StackValue;
    void *Pointer;

    debugf("ExpressionInfixOperator()\n");
    if (BottomValue == NULL || TopValue == NULL)
        ProgramFail(Parser, "invalid expression");

    /* comma operator: evaluate left (side effects done), yield right */
    if (Op == TokenComma)
    {
        if (Parser->Mode == RunModeRun)
        {
            ExpressionStackPushValue(Parser, StackTop, TopValue);
        }
        else
            ExpressionPushInt(Parser, StackTop, 0);
        return;
    }

    if (Op != TokenAssign && TokenIsAssignmentOp(Op) && Parser->Mode == RunModeRun)
        ExpressionCheckCompoundUse(Parser, BottomValue);

    if (Op == TokenLeftSquareBracket)
    {
        /* array index — C: a[i] is *(a+i), so i[a] is also valid when one
         * operand is pointer/array and the other is integer. */
        ptrdiff_t ArrayIndex;
        struct Value *Result = NULL;
        /* BottomValue lives on the expression stack: once popped, the allocation
         * of Result below can reuse (and overwrite) its memory, so remember now
         * whether we are indexing a real array. */
        int IndexingArray;
        struct Value *ElemOwner = NULL;
        struct Value *AliasOwner = NULL;
        struct Value *BaseValue;
        struct Value *IndexValue;

        /* 4[a] : Bottom=4 (int), Top=a (array) → swap so Base=array, Index=int */
        if (IS_NUMERIC_COERCIBLE(BottomValue)
            && TopValue->Typ != NULL
            && (TopValue->Typ->Base == TypeArray || TopValue->Typ->Base == TypePointer))
        {
            BaseValue = TopValue;
            IndexValue = BottomValue;
        }
        else
        {
            BaseValue = BottomValue;
            IndexValue = TopValue;
        }
        IndexingArray = (BaseValue->Typ != NULL && BaseValue->Typ->Base == TypeArray);
        if (ExpressionAliasInitTrackingEnabled()
            && BaseValue->Typ != NULL && BaseValue->Typ->Base == TypePointer)
        {
            AliasOwner = BaseValue->InitAliasRoot;
            if (AliasOwner == NULL && BaseValue->LValueFrom != NULL)
                AliasOwner = ExpressionInitRoot(BaseValue->LValueFrom);
        }

        if (!IS_NUMERIC_COERCIBLE(IndexValue))
            ProgramFail(Parser, "array index must be an integer");

        ArrayIndex = (ptrdiff_t)ExpressionCoerceInteger(IndexValue);

        /* Fixed-size array bounds. C allows &a[N] (one-past-end); reading a[N]
         * or &a[N+1] is illegal when ArraySize is known. */
        if (IrSafeChecksOn() && BaseValue->Typ->Base == TypeArray && BaseValue->Typ->ArraySize > 0)
        {
            int AddrOfPeek = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                              && (*StackTop)->Op == TokenAmpersand);
            if (ArrayIndex < 0
                || (AddrOfPeek && ArrayIndex > (ptrdiff_t)BaseValue->Typ->ArraySize)
                || (!AddrOfPeek && ArrayIndex >= (ptrdiff_t)BaseValue->Typ->ArraySize))
                ProgramFail(Parser, DiagnosticMsgArrayIndexOutOfBoundsSized,
                    (int)ArrayIndex, BaseValue->Typ->ArraySize);
        }

        /* make the array element result */
        switch (BaseValue->Typ->Base)
        {
            case TypeArray:
            {
                struct Value *Owner = BaseValue;
                struct Value *Tracked = ExpressionInitRoot(BaseValue);
                if (Tracked != NULL)
                    Owner = Tracked;
                ElemOwner = Tracked;
                Result = VariableAllocValueFromExistingData(Parser, BaseValue->Typ->FromType,
                    (union AnyValue *)((char *)BaseValue->Val + TypeSize(BaseValue->Typ, ArrayIndex, TRUE)),
                    BaseValue->IsLValue, Owner);
                if (Tracked != NULL && ArrayIndex >= 0)
                    Result->IsInitialized = ExpressionLeafInit(Tracked, Result);
                else
                    Result->IsInitialized = 1; /* pointer / unknown / not tracked → permissive */
                break;
            }
            case TypePointer:
                /* f()[i] / p[i]: element is an lvalue even when the pointer is an rvalue */
                /* A pointer to a fixed-size row (e.g. int (*)[4]) advances by
                 * the complete row size. TypeSize(array, 0, ...) is zero because
                 * its second argument is an element count, not the array bound. */
                Result = VariableAllocValueFromExistingData(Parser, BaseValue->Typ->FromType,
                    (union AnyValue *)((char *)BaseValue->Val->Pointer
                        + (BaseValue->Typ->FromType->Base == TypeArray
                            ? TypeSize(BaseValue->Typ->FromType, BaseValue->Typ->FromType->ArraySize, TRUE)
                            : TypeSize(BaseValue->Typ->FromType, 0, TRUE)) * ArrayIndex),
                    TRUE, BaseValue->LValueFrom ? BaseValue->LValueFrom : BaseValue);
                if (AliasOwner != NULL)
                {
                    Result->InitAliasRoot = AliasOwner;
                    Result->IsInitialized = ExpressionLeafInitAlias(AliasOwner, Result);
                }
                break;
            default:          ProgramFail(Parser, "this %t is not an array", BaseValue->Typ);
        }

        if (Result != NULL && (IndexingArray || AliasOwner != NULL))
        {
            /* Forming an lvalue is not a use of the value.  The element is only
             * being named when it is the target of an assignment / ++ / --, the
             * base of a further . -> [ ] chain, or the operand of unary & or
             * sizeof.  The operator that triggered this collapse tells us which
             * case we are in (the lexer has already moved past it, so peeking
             * the next token would be wrong). */
            /* After ']' collapse, ExprCollapseTok is always TokenLeftSquareBracket
             * (see ExpressionParseEx). That cannot distinguish intermediate a[i][j]
             * from a final value use of a[i]. Peek the real next token instead. */
            enum LexToken Next = LexGetToken(Parser, NULL, FALSE);
            int AddrOf = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                          && (*StackTop)->Op == TokenAmpersand);
            int InSizeof = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                            && (*StackTop)->Op == TokenSizeof);

            /* &a[i] lets the address escape: from here on the whole array is
             * treated as initialised (policy: alias -> permissive). */
            if (AddrOf && ElemOwner != NULL)
                ElemOwner->IsInitialized = TRUE;

            /* a row / array member used without a further [ ] decays to a
             * pointer: its bytes may be written behind our back. */
            if (ElemOwner != NULL && !AddrOf && !InSizeof
                && Result->Typ != NULL && Result->Typ->Base == TypeArray
                && Next != TokenLeftSquareBracket)
                ExpressionInitEscape(ElemOwner, Result);

            if (!Result->IsInitialized
                && !AddrOf && !InSizeof
                && !TokenIsAssignmentOp(Next)
                && Next != TokenIncrement && Next != TokenDecrement
                && Next != TokenDot && Next != TokenArrow
                && Next != TokenLeftSquareBracket)
                ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
        }

        ExpressionStackPushValueNode(Parser, StackTop, Result);
    }
    else if (Op == TokenQuestionMark)
        ExpressionQuestionMarkOperator(Parser, StackTop, TopValue, BottomValue);

    else if (Op == TokenColon)
        ExpressionColonOperator(Parser, StackTop, TopValue, BottomValue);

    else if (Op == TokenCast)
    {
        /* cast a value to a different type */
        struct Value* ValueLoc = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Val->Typ);
        ExpressionAssign(Parser, ValueLoc, TopValue, TRUE, NULL, 0, TRUE);
    }
#ifndef NO_FP
    else if ( ((TopValue->Typ != NULL && TopValue->Typ->Base == TypeFP) && (BottomValue->Typ != NULL && BottomValue->Typ->Base == TypeFP)) ||
              ((TopValue->Typ != NULL && TopValue->Typ->Base == TypeFP) && IS_NUMERIC_COERCIBLE(BottomValue)) ||
              (IS_NUMERIC_COERCIBLE(TopValue) && (BottomValue->Typ != NULL && BottomValue->Typ->Base == TypeFP)) )
    {
        /* floating point infix arithmetic */
        int ResultIsInt = FALSE;
        double ResultFP = 0.0;
        double TopFP = ((TopValue->Typ != NULL && TopValue->Typ->Base == TypeFP)) ? TopValue->Val->FP : (double)ExpressionCoerceInteger(TopValue);
        double BottomFP = ((BottomValue->Typ != NULL && BottomValue->Typ->Base == TypeFP)) ? BottomValue->Val->FP : (double)ExpressionCoerceInteger(BottomValue);

        switch (Op)
        {
            case TokenAssign:               ASSIGN_FP_OR_INT(TopFP); break;
            case TokenAddAssign:            ASSIGN_FP_OR_INT(BottomFP + TopFP); break;
            case TokenSubtractAssign:       ASSIGN_FP_OR_INT(BottomFP - TopFP); break;
            case TokenMultiplyAssign:       ASSIGN_FP_OR_INT(BottomFP * TopFP); break;
            case TokenDivideAssign:         ASSIGN_FP_OR_INT(BottomFP / TopFP); break;
            case TokenEqual:                ResultInt = BottomFP == TopFP; ResultIsInt = TRUE; break;
            case TokenNotEqual:             ResultInt = BottomFP != TopFP; ResultIsInt = TRUE; break;
            case TokenLessThan:             ResultInt = BottomFP < TopFP; ResultIsInt = TRUE; break;
            case TokenGreaterThan:          ResultInt = BottomFP > TopFP; ResultIsInt = TRUE; break;
            case TokenLessEqual:            ResultInt = BottomFP <= TopFP; ResultIsInt = TRUE; break;
            case TokenGreaterEqual:         ResultInt = BottomFP >= TopFP; ResultIsInt = TRUE; break;
            case TokenPlus:                 ResultFP = BottomFP + TopFP; break;
            case TokenMinus:                ResultFP = BottomFP - TopFP; break;
            case TokenAsterisk:             ResultFP = BottomFP * TopFP; break;
            case TokenSlash:
                if (TopFP == 0.0)
                {
                    ParserSetCaretOnLine(Parser, '/');
                    ProgramFail(Parser, DiagnosticMsgDivisionByZero);
                }
                ResultFP = BottomFP / TopFP;
                break;
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        if (ResultIsInt)
            ExpressionPushInt(Parser, StackTop, ResultInt);
        else
            ExpressionPushFP(Parser, StackTop, ResultFP);
    }
#endif

    else if (TopValue->Typ->Base == TypeInt && BottomValue->Typ->Base == TypeInt)
    {
        /* Fast path: both operands are int — no coerce switch, no long long.
         * Bitfield LValues store the unit word in Val; must extract before arith. */
        int TopInt = TopValue->Val->Integer;
        int BottomInt;
        int ResultInt = 0;
        if (BottomValue->BitWidth > 0)
            BottomInt = (int)BitFieldExtract(BottomValue->Val,
                BottomValue->BitOffset, BottomValue->BitWidth,
                !TypeIsUnsignedBitFieldBase(BottomValue->Typ));
        else
            BottomInt = BottomValue->Val->Integer;
        switch (Op)
        {
            case TokenAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, TopInt, FALSE);
                break;
            case TokenAddAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt + TopInt, FALSE);
                break;
            case TokenSubtractAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt - TopInt, FALSE);
                break;
            case TokenMultiplyAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt * TopInt, FALSE);
                break;
            case TokenDivideAssign:
                if (TopInt == 0) { ParserSetCaretOnLine(Parser, '/'); ProgramFail(Parser, DiagnosticMsgDivisionByZero); }
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt / TopInt, FALSE);
                break;
#ifndef NO_MODULUS
            case TokenModulusAssign:
                if (TopInt == 0) {
ParserSetCaretOnLine(Parser, 37);
ProgramFail(Parser, DiagnosticMsgModuloByZero); }
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt % TopInt, FALSE);
                break;
#endif
            case TokenShiftLeftAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt << TopInt, FALSE);
                break;
            case TokenShiftRightAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt >> TopInt, FALSE);
                break;
            case TokenArithmeticAndAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt & TopInt, FALSE);
                break;
            case TokenArithmeticOrAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt | TopInt, FALSE);
                break;
            case TokenArithmeticExorAssign:
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt ^ TopInt, FALSE);
                break;
            case TokenLogicalOr:      ResultInt = BottomInt || TopInt; break;
            case TokenLogicalAnd:     ResultInt = BottomInt && TopInt; break;
            case TokenEqual:          ResultInt = BottomInt == TopInt; break;
            case TokenNotEqual:       ResultInt = BottomInt != TopInt; break;
            case TokenLessThan:       ResultInt = BottomInt < TopInt; break;
            case TokenGreaterThan:    ResultInt = BottomInt > TopInt; break;
            case TokenLessEqual:      ResultInt = BottomInt <= TopInt; break;
            case TokenGreaterEqual:   ResultInt = BottomInt >= TopInt; break;
            case TokenShiftLeft:      ResultInt = BottomInt << TopInt; break;
            case TokenShiftRight:     ResultInt = BottomInt >> TopInt; break;
            case TokenPlus:           ResultInt = BottomInt + TopInt; break;
            case TokenMinus:          ResultInt = BottomInt - TopInt; break;
            case TokenAsterisk:       ResultInt = BottomInt * TopInt; break;
            case TokenSlash:
                if (TopInt == 0) { ParserSetCaretOnLine(Parser, '/'); ProgramFail(Parser, DiagnosticMsgDivisionByZero); }
                ResultInt = BottomInt / TopInt;
                break;
#ifndef NO_MODULUS
            case TokenModulus:
                if (TopInt == 0) {
ParserSetCaretOnLine(Parser, 37);
ProgramFail(Parser, DiagnosticMsgModuloByZero); }
                ResultInt = BottomInt % TopInt;
                break;
#endif
            case TokenArithmeticOr:   ResultInt = BottomInt | TopInt; break;
            case TokenArithmeticExor: ResultInt = BottomInt ^ TopInt; break;
            case TokenAmpersand:      ResultInt = BottomInt & TopInt; break;
            default:
                ProgramFail(Parser, "invalid operation");
                break;
        }
        ExpressionPushInt(Parser, StackTop, ResultInt);
    }

    else if (IS_LONGLONG_COERCIBLE(TopValue) || IS_LONGLONG_COERCIBLE(BottomValue))
    {
        /* 64-bit integer operation - at least one operand is long long */
        /* IsUnsigned reflects the type of BottomValue (the left-hand operand),
         * which determines whether the operation is signed or unsigned.
         * TopValue may be a plain int shift count and must not override this. */
        int IsUnsigned = ExpressionOpUsesUnsigned(BottomValue, TopValue, Op);
        long long TopLL = IrTypeIsUnsignedBase(TopValue->Typ->Base)
            ? (long long)(unsigned long long)ExpressionCoerceUnsignedInteger(TopValue)
            : (long long)ExpressionCoerceInteger(TopValue);
        long long BottomLL = IrTypeIsUnsignedBase(BottomValue->Typ->Base)
            ? (long long)(unsigned long long)ExpressionCoerceUnsignedInteger(BottomValue)
            : (long long)ExpressionCoerceInteger(BottomValue);
        long long ResultLL = 0;
        int ResultIsInt = FALSE;
        switch (Op)
        {
            case TokenAssign:               ResultLL = ExpressionAssignLongLong(Parser, BottomValue, TopLL, FALSE); break;
            case TokenAddAssign:            ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL + TopLL, FALSE); break;
            case TokenSubtractAssign:       ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL - TopLL, FALSE); break;
            case TokenMultiplyAssign:       ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL * TopLL, FALSE); break;
            case TokenDivideAssign:         ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL / TopLL, FALSE); break;
#ifndef NO_MODULUS
            case TokenModulusAssign:        ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL % TopLL, FALSE); break;
#endif
            case TokenShiftLeftAssign:      ResultLL = ExpressionAssignLongLong(Parser, BottomValue, IsUnsigned ? (long long)((unsigned long long)BottomLL << TopLL) : (BottomLL << TopLL), FALSE); break;
            case TokenShiftRightAssign:     ResultLL = ExpressionAssignLongLong(Parser, BottomValue, IsUnsigned ? (long long)((unsigned long long)BottomLL >> TopLL) : (BottomLL >> TopLL), FALSE); break;
            case TokenArithmeticAndAssign:  ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL & TopLL, FALSE); break;
            case TokenArithmeticOrAssign:   ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL | TopLL, FALSE); break;
            case TokenArithmeticExorAssign: ResultLL = ExpressionAssignLongLong(Parser, BottomValue, BottomLL ^ TopLL, FALSE); break;
            case TokenLogicalOr:            ResultLL = BottomLL || TopLL; ResultIsInt = TRUE; break;
            case TokenLogicalAnd:           ResultLL = BottomLL && TopLL; ResultIsInt = TRUE; break;
            case TokenEqual:                ResultLL = BottomLL == TopLL; ResultIsInt = TRUE; break;
            case TokenNotEqual:             ResultLL = BottomLL != TopLL; ResultIsInt = TRUE; break;
            case TokenLessThan:             ResultLL = IsUnsigned ? ((unsigned long long)BottomLL < (unsigned long long)TopLL) : (BottomLL < TopLL);  ResultIsInt = TRUE; break;
            case TokenGreaterThan:          ResultLL = IsUnsigned ? ((unsigned long long)BottomLL > (unsigned long long)TopLL) : (BottomLL > TopLL);  ResultIsInt = TRUE; break;
            case TokenLessEqual:            ResultLL = IsUnsigned ? ((unsigned long long)BottomLL <= (unsigned long long)TopLL) : (BottomLL <= TopLL); ResultIsInt = TRUE; break;
            case TokenGreaterEqual:         ResultLL = IsUnsigned ? ((unsigned long long)BottomLL >= (unsigned long long)TopLL) : (BottomLL >= TopLL); ResultIsInt = TRUE; break;
            case TokenArithmeticOr:         ResultLL = BottomLL | TopLL; break;
            case TokenArithmeticExor:       ResultLL = BottomLL ^ TopLL; break;
            case TokenAmpersand:            ResultLL = BottomLL & TopLL; break;
            case TokenShiftLeft:            ResultLL = (long long)((unsigned long long)BottomLL << (unsigned)(TopLL & 63)); break;
            case TokenShiftRight:           ResultLL = IsUnsigned ? (long long)((unsigned long long)BottomLL >> TopLL) : (BottomLL >> TopLL); break;
            case TokenPlus:                 ResultLL = (long long)((unsigned long long)BottomLL + (unsigned long long)TopLL); break;
            case TokenMinus:                ResultLL = (long long)((unsigned long long)BottomLL - (unsigned long long)TopLL); break;
            case TokenAsterisk:             ResultLL = (long long)((unsigned long long)BottomLL * (unsigned long long)TopLL); break;
            case TokenSlash:                if (TopLL == 0) { ParserSetCaretOnLine(Parser, '/'); ProgramFail(Parser, DiagnosticMsgDivisionByZero); }
                                            ResultLL = (BottomLL == LLONG_MIN && TopLL == -1)
                                                ? LLONG_MIN : BottomLL / TopLL; break;
#ifndef NO_MODULUS
            case TokenModulus:              if (TopLL == 0) { ParserSetCaretOnLine(Parser, '%'); ProgramFail(Parser, DiagnosticMsgModuloByZero); }
                                            ResultLL = (BottomLL == LLONG_MIN && TopLL == -1)
                                                ? 0 : BottomLL % TopLL; break;
#endif
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }
        if (ResultIsInt)
            ExpressionPushInt(Parser, StackTop, (long)ResultLL);
        else
        {
            if (IsUnsigned)
                ExpressionPushUnsignedLongLong(Parser, StackTop, (unsigned long long)ResultLL);
            else
                ExpressionPushLongLong(Parser, StackTop, ResultLL);
        }
    }
    else if ((Op == TokenLogicalOr || Op == TokenLogicalAnd) &&
             (IS_NUMERIC_COERCIBLE(TopValue) || TopValue->Typ->Base == TypePointer || TopValue->Typ->Base == TypeArray) &&
             (IS_NUMERIC_COERCIBLE(BottomValue) || BottomValue->Typ->Base == TypePointer || BottomValue->Typ->Base == TypeArray))
    {
        /* pointer/int logical ops: treat non-NULL pointer as true */
        PTR_TYPE_INT_SIGNED TopInt = IS_NUMERIC_COERCIBLE(TopValue)
            ? ExpressionCoerceInteger(TopValue)
            : (TopValue->Val->Pointer != NULL);
        PTR_TYPE_INT_SIGNED BottomInt = IS_NUMERIC_COERCIBLE(BottomValue)
            ? ExpressionCoerceInteger(BottomValue)
            : (BottomValue->Val->Pointer != NULL);
        ResultInt = (Op == TokenLogicalOr) ? (BottomInt || TopInt) : (BottomInt && TopInt);
        ExpressionPushInt(Parser, StackTop, ResultInt);
    }
    else if (IS_NUMERIC_COERCIBLE(TopValue) && IS_NUMERIC_COERCIBLE(BottomValue))
    {
        /* integer operation */
        PTR_TYPE_INT_SIGNED TopInt = ExpressionCoerceInteger(TopValue);
        PTR_TYPE_INT_SIGNED BottomInt = ExpressionCoerceInteger(BottomValue);
        int Unsign = ExpressionOpUsesUnsigned(BottomValue, TopValue, Op);
        unsigned int Tu = (unsigned int)(IrTypeIsUnsignedBase(TopValue->Typ->Base)
            ? ExpressionCoerceUnsignedInteger(TopValue) : TopInt);
        unsigned int Bu = (unsigned int)(IrTypeIsUnsignedBase(BottomValue->Typ->Base)
            ? ExpressionCoerceUnsignedInteger(BottomValue) : BottomInt);
        switch (Op)
        {
            case TokenAssign:               ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, TopInt, FALSE); break;
            case TokenAddAssign:            ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt + TopInt, FALSE); break;
            case TokenSubtractAssign:       ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt - TopInt, FALSE); break;
            case TokenMultiplyAssign:       ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt * TopInt, FALSE); break;
            case TokenDivideAssign:         if (TopInt == 0) { ParserSetCaretOnLine(Parser, '/'); ProgramFail(Parser, DiagnosticMsgDivisionByZero); }
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt / TopInt, FALSE); break;
#ifndef NO_MODULUS
            case TokenModulusAssign:        if (TopInt == 0) { ParserSetCaretOnLine(Parser, '%'); ProgramFail(Parser, DiagnosticMsgModuloByZero); }
                ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt % TopInt, FALSE); break;
#endif
            case TokenShiftLeftAssign:      ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt << TopInt, FALSE); break;
            case TokenShiftRightAssign:     ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt >> TopInt, FALSE); break;
            case TokenArithmeticAndAssign:  ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt & TopInt, FALSE); break;
            case TokenArithmeticOrAssign:   ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt | TopInt, FALSE); break;
            case TokenArithmeticExorAssign: ResultInt = (int)ExpressionAssignInt(Parser, BottomValue, BottomInt ^ TopInt, FALSE); break;
            case TokenQuestionMark:
                                            {
                                                struct Value *thirdVal;
                                                struct Value *LexValue;
                                                enum LexToken Token = LexGetToken(Parser, &LexValue, TRUE);
                                                if (Token != TokenColon)
                                                    ProgramFail(Parser, ": expected");
                                                ExpressionParse(Parser, &thirdVal);
                                                if (Parser->Mode == RunModeRun)
                                                    VariableStackPop(Parser, thirdVal);
                                                if (BottomInt)
                                                    ResultInt = TopInt;
                                                else
                                                {
                                                    if (thirdVal->Typ->Base == TypeChar)
                                                        ResultInt = (long)thirdVal->Val->Character;
                                                    else
                                                        ResultInt = thirdVal->Val->LongInteger;
                                                }
                                            }
                                            break;
            //TokenColon actually never seen here - we handle it in ExpressionParse()
            case TokenColon:                ProgramFail(Parser, "not supported"); break; /* XXX */
            case TokenLogicalOr:            ResultInt = BottomInt || TopInt; break;
            case TokenLogicalAnd:           ResultInt = BottomInt && TopInt; break;
            case TokenArithmeticOr:         ResultInt = BottomInt | TopInt; break;
            case TokenArithmeticExor:       ResultInt = BottomInt ^ TopInt; break;
            case TokenAmpersand:            ResultInt = BottomInt & TopInt; break;
            case TokenEqual:                ResultInt = BottomInt == TopInt; break;
            case TokenNotEqual:             ResultInt = BottomInt != TopInt; break;
            case TokenLessThan:             ResultInt = Unsign ? (Bu < Tu) : (BottomInt < TopInt); break;
            case TokenGreaterThan:          ResultInt = Unsign ? (Bu > Tu) : (BottomInt > TopInt); break;
            case TokenLessEqual:            ResultInt = Unsign ? (Bu <= Tu) : (BottomInt <= TopInt); break;
            case TokenGreaterEqual:         ResultInt = Unsign ? (Bu >= Tu) : (BottomInt >= TopInt); break;
            case TokenShiftLeft:            ResultInt = BottomInt << TopInt; break;
            case TokenShiftRight:           ResultInt = Unsign ? (PTR_TYPE_INT_SIGNED)(Bu >> (Tu & 31)) : (BottomInt >> TopInt); break;
            case TokenPlus:                 ResultInt = Unsign ? (PTR_TYPE_INT_SIGNED)(Bu + Tu) : (BottomInt + TopInt); break;
            case TokenMinus:                ResultInt = Unsign ? (PTR_TYPE_INT_SIGNED)(Bu - Tu) : (BottomInt - TopInt); break;
            case TokenAsterisk:             ResultInt = Unsign ? (PTR_TYPE_INT_SIGNED)(Bu * Tu) : (BottomInt * TopInt); break;
            case TokenSlash:
                if (TopInt == 0)
                {
                    ParserSetCaretOnLine(Parser, '/');
                    ProgramFail(Parser, DiagnosticMsgDivisionByZero);
                }
                ResultInt = Unsign ? (PTR_TYPE_INT_SIGNED)(Bu / Tu) : (BottomInt / TopInt);
                break;
#ifndef NO_MODULUS
            case TokenModulus:
                if (TopInt == 0)
                {
                    ParserSetCaretOnLine(Parser, '%');
                    ProgramFail(Parser, DiagnosticMsgModuloByZero);
                }
                ResultInt = Unsign ? (PTR_TYPE_INT_SIGNED)(Bu % Tu) : (BottomInt % TopInt);
                break;
#endif
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        /* C usual arithmetic conversions: if either operand is long (or the
         * assignment target is long), the expression value must not be pushed
         * as int — ExpressionPushInt truncates to 32-bit and types as IntType,
         * which broke e.g. long x; int y; r = x + y;  (HANDOFF_DELTA §7.2).
         * Comparisons / logical ops still yield int. */
        {
            int IsCmp = (Op == TokenEqual || Op == TokenNotEqual
                      || Op == TokenLessThan || Op == TokenGreaterThan
                      || Op == TokenLessEqual || Op == TokenGreaterEqual
                      || Op == TokenLogicalOr || Op == TokenLogicalAnd);
            enum BaseType Rb = BottomValue->Typ->Base;
            enum BaseType Rt = TopValue->Typ->Base;
            int NeedLong = 0;
            if (!IsCmp)
            {
                if (TokenIsAssignmentOp(Op))
                    NeedLong = (Rb == TypeLong || Rb == TypeUnsignedLong);
                else
                    NeedLong = (Rb == TypeLong || Rb == TypeUnsignedLong
                             || Rt == TypeLong || Rt == TypeUnsignedLong);
            }
            if (NeedLong)
                ExpressionPushLong(Parser, StackTop, (long)ResultInt);
            else if (Unsign && !IsCmp && !TokenIsAssignmentOp(Op))
            {
                struct Value *UV = VariableAllocValueFromType(Parser->pc, Parser,
                    &Parser->pc->UnsignedIntType, FALSE, NULL, FALSE);
                UV->Val->UnsignedInteger = (unsigned int)ResultInt;
                ExpressionStackPushValueNode(Parser, StackTop, UV);
            }
            else
                ExpressionPushInt(Parser, StackTop, (long)ResultInt);
        }
    }
    else if (IS_NUMERIC_COERCIBLE(BottomValue) && TopValue->Typ->Base == TypePointer
             && (Op == TokenEqual || Op == TokenNotEqual))
    {
        /* integer/pointer comparison (e.g. NULL != ptr) — automatic cast */
        PTR_TYPE_INT_SIGNED BottomInt = ExpressionCoerceInteger(BottomValue);
        if (BottomInt != 0)
            ProgramFail(Parser, "invalid operation");
        if (Op == TokenEqual)
            ExpressionPushInt(Parser, StackTop, TopValue->Val->Pointer == NULL);
        else
            ExpressionPushInt(Parser, StackTop, TopValue->Val->Pointer != NULL);
    }
    else if (BottomValue->Typ->Base == TypeArray && IS_NUMERIC_COERCIBLE(TopValue)
             && (Op == TokenPlus || Op == TokenMinus))
    {
        /* array decays to pointer to first element, then pointer arithmetic */
        int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);
        PTR_TYPE_INT_SIGNED TopInt = ExpressionCoerceInteger(TopValue);
        char *Base = (char *)BottomValue->Val;
        void *Pointer;
        struct ValueType *PtrTyp;
        struct Value *StackValue;

        if (Size < 1) Size = 1;
        if (Op == TokenPlus)
            Pointer = (void *)(Base + TopInt * Size);
        else
            Pointer = (void *)(Base - TopInt * Size);

        PtrTyp = TypeGetMatching(Parser->pc, Parser, BottomValue->Typ->FromType,
                                 TypePointer, 0, Parser->pc->StrEmpty, TRUE);
        StackValue = ExpressionStackPushValueByType(Parser, StackTop, PtrTyp);
        StackValue->Val->Pointer = Pointer;
        StackValue->IsInitialized = TRUE;
    }
    else if (IS_NUMERIC_COERCIBLE(BottomValue) && TopValue->Typ->Base == TypeArray
             && Op == TokenPlus)
    {
        /* int + array → pointer arithmetic (commutative +) */
        int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
        PTR_TYPE_INT_SIGNED BottomInt = ExpressionCoerceInteger(BottomValue);
        char *Base = (char *)TopValue->Val;
        void *Pointer;
        struct ValueType *PtrTyp;
        struct Value *StackValue;

        if (Size < 1) Size = 1;
        Pointer = (void *)(Base + BottomInt * Size);
        PtrTyp = TypeGetMatching(Parser->pc, Parser, TopValue->Typ->FromType,
                                 TypePointer, 0, Parser->pc->StrEmpty, TRUE);
        StackValue = ExpressionStackPushValueByType(Parser, StackTop, PtrTyp);
        StackValue->Val->Pointer = Pointer;
        StackValue->IsInitialized = TRUE;
    }
    else if (IS_NUMERIC_COERCIBLE(BottomValue) && TopValue->Typ->Base == TypePointer
             && Op == TokenPlus)
    {
        /* integer + pointer is the commutative form of pointer arithmetic.
         * Keep the pointer's declared pointee type so subsequent dereference
         * uses the correct object width (including FP and aggregate types). */
        int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
        PTR_TYPE_INT_SIGNED BottomInt = ExpressionCoerceInteger(BottomValue);
        void *Pointer = TopValue->Val->Pointer;
        struct Value *StackValue;
        if (Size < 1) Size = 1;
        if (Pointer == NULL)
            ProgramFail(Parser, "invalid use of a NULL pointer");
        Pointer = (void *)((char *)Pointer + BottomInt * Size);
        StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
        StackValue->Val->Pointer = Pointer;
        StackValue->IsInitialized = TRUE;
    }
    else if (BottomValue->Typ->Base == TypePointer && IS_NUMERIC_COERCIBLE(TopValue))
    {
        /* pointer/integer infix arithmetic */
        PTR_TYPE_INT_SIGNED TopInt = ExpressionCoerceInteger(TopValue);

        if (Op == TokenEqual || Op == TokenNotEqual)
        {
            /* comparison to a NULL pointer */
            if (TopInt != 0)
                ProgramFail(Parser, "invalid operation");

            if (Op == TokenEqual)
                ExpressionPushInt(Parser, StackTop, BottomValue->Val->Pointer == NULL);
            else
                ExpressionPushInt(Parser, StackTop, BottomValue->Val->Pointer != NULL);
        }
        else if (Op == TokenPlus || Op == TokenMinus)
        {
            /* pointer arithmetic */
            int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);

            Pointer = BottomValue->Val->Pointer;
            if (Pointer == NULL)
                ProgramFail(Parser, "invalid use of a NULL pointer");

            if (Op == TokenPlus)
                Pointer = (void *)((char *)Pointer + TopInt * Size);
            else
                Pointer = (void *)((char *)Pointer - TopInt * Size);

            StackValue = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Typ);
            StackValue->Val->Pointer = Pointer;
            StackValue->IsInitialized = TRUE;
        }
        else if (Op == TokenAssign && TopInt == 0)
        {
            /* assign a NULL pointer */
            HeapUnpopStack(Parser->pc, sizeof(struct Value));
            ExpressionAssign(Parser, BottomValue, TopValue, FALSE, NULL, 0, TRUE);
            ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
        }
        else if (Op == TokenAddAssign || Op == TokenSubtractAssign)
        {
            /* pointer arithmetic */
            int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);

            Pointer = BottomValue->Val->Pointer;
            if (Pointer == NULL)
                ProgramFail(Parser, "invalid use of a NULL pointer");

            if (Op == TokenAddAssign)
                Pointer = (void *)((char *)Pointer + TopInt * Size);
            else
                Pointer = (void *)((char *)Pointer - TopInt * Size);

            HeapUnpopStack(Parser->pc, sizeof(struct Value));
            BottomValue->Val->Pointer = Pointer;
            ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
        }
        else
            ProgramFail(Parser, "invalid operation");
    }
    else if (BottomValue->Typ->Base == TypePointer && TopValue->Typ->Base == TypeArray && Op != TokenAssign)
    {
        /* pointer vs array: decay array to &a[0] */
        char *TopLoc = (char *)TopValue->Val;
        char *BottomLoc = (char *)BottomValue->Val->Pointer;
        switch (Op)
        {
            case TokenEqual:     ExpressionPushInt(Parser, StackTop, BottomLoc == TopLoc); break;
            case TokenNotEqual:  ExpressionPushInt(Parser, StackTop, BottomLoc != TopLoc); break;
            case TokenLessThan:  ExpressionPushInt(Parser, StackTop, BottomLoc < TopLoc); break;
            case TokenGreaterThan: ExpressionPushInt(Parser, StackTop, BottomLoc > TopLoc); break;
            case TokenLessEqual: ExpressionPushInt(Parser, StackTop, BottomLoc <= TopLoc); break;
            case TokenGreaterEqual: ExpressionPushInt(Parser, StackTop, BottomLoc >= TopLoc); break;
            case TokenMinus:
            {
                int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);
                if (Size < 1) Size = 1;
                ExpressionPushInt(Parser, StackTop, (BottomLoc - TopLoc) / Size);
                break;
            }
            default: ProgramFail(Parser, "invalid operation"); break;
        }
    }
    else if (BottomValue->Typ->Base == TypeArray && TopValue->Typ->Base == TypePointer && Op != TokenAssign)
    {
        char *BottomLoc = (char *)BottomValue->Val;
        char *TopLoc = (char *)TopValue->Val->Pointer;
        switch (Op)
        {
            case TokenEqual:     ExpressionPushInt(Parser, StackTop, BottomLoc == TopLoc); break;
            case TokenNotEqual:  ExpressionPushInt(Parser, StackTop, BottomLoc != TopLoc); break;
            case TokenLessThan:  ExpressionPushInt(Parser, StackTop, BottomLoc < TopLoc); break;
            case TokenGreaterThan: ExpressionPushInt(Parser, StackTop, BottomLoc > TopLoc); break;
            case TokenLessEqual: ExpressionPushInt(Parser, StackTop, BottomLoc <= TopLoc); break;
            case TokenGreaterEqual: ExpressionPushInt(Parser, StackTop, BottomLoc >= TopLoc); break;
            case TokenMinus:
            {
                int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);
                if (Size < 1) Size = 1;
                ExpressionPushInt(Parser, StackTop, (BottomLoc - TopLoc) / Size);
                break;
            }
            default: ProgramFail(Parser, "invalid operation"); break;
        }
    }
    else if (BottomValue->Typ->Base == TypePointer && TopValue->Typ->Base == TypePointer && Op != TokenAssign)
    {
        /* pointer/pointer operations */
        char *TopLoc = (char *)TopValue->Val->Pointer;
        char *BottomLoc = (char *)BottomValue->Val->Pointer;

        switch (Op)
        {
            case TokenEqual:                ExpressionPushInt(Parser, StackTop, BottomLoc == TopLoc); break;
            case TokenNotEqual:             ExpressionPushInt(Parser, StackTop, BottomLoc != TopLoc); break;
            case TokenLessThan:             ExpressionPushInt(Parser, StackTop, BottomLoc < TopLoc); break;
            case TokenGreaterThan:          ExpressionPushInt(Parser, StackTop, BottomLoc > TopLoc); break;
            case TokenLessEqual:            ExpressionPushInt(Parser, StackTop, BottomLoc <= TopLoc); break;
            case TokenGreaterEqual:         ExpressionPushInt(Parser, StackTop, BottomLoc >= TopLoc); break;
            case TokenMinus:
            {
                /* C: pointer difference is in elements of the pointed-to type */
                int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);
                if (Size < 1) Size = 1;
                ExpressionPushInt(Parser, StackTop, (BottomLoc - TopLoc) / Size);
                break;
            }
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }
    }
    else if (Op == TokenAssign)
    {
        /* assign a non-numeric type.
         * AllowPointerCoercion=TRUE permits int↔void* (needed for flexible
         * library functions that sometimes return int, sometimes a pointer). */
        HeapUnpopStack(Parser->pc, sizeof(struct Value));   /* XXX - possible bug if lvalue is a temp value and takes more than sizeof(struct Value) */
        ExpressionAssign(Parser, BottomValue, TopValue, FALSE, NULL, 0, TRUE);
        ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
    }
    else if (Op == TokenCast)
    {
        /* cast a value to a different type */   /* XXX - possible bug if the destination type takes more than sizeof(struct Value) + sizeof(struct ValueType *) */
        struct Value *ValueLoc = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Val->Typ);
        ExpressionAssign(Parser, ValueLoc, TopValue, TRUE, NULL, 0, TRUE);
    }
    else
        ProgramFail(Parser, "invalid operation");
}

/* take the contents of the expression stack and compute the top until there's nothing greater than the given precedence */
void ExpressionStackCollapse(struct ParseState *Parser, struct ExpressionStack **StackTop, int Precedence, int *IgnorePrecedence)
{
    int FoundPrecedence = Precedence;
    struct Value *TopValue;
    struct Value *BottomValue;
    struct ExpressionStack *TopStackNode = *StackTop;
    struct ExpressionStack *TopOperatorNode;

    debugf("ExpressionStackCollapse(%d):\n", Precedence);
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
    while (TopStackNode != NULL && TopStackNode->Next != NULL && FoundPrecedence >= Precedence)
    {
        /* find the top operator on the stack */
        if (TopStackNode->Order == OrderNone)
            TopOperatorNode = TopStackNode->Next;
        else
            TopOperatorNode = TopStackNode;

        FoundPrecedence = TopOperatorNode->Precedence;

        /* does it have a high enough precedence? */
        if (FoundPrecedence >= Precedence && TopOperatorNode != NULL)
        {
            /* execute this operator */
            switch (TopOperatorNode->Order)
            {
                case OrderPrefix:
                    /* prefix evaluation */
                    debugf("prefix evaluation\n");
                    TopValue = TopStackNode->Val;

                    /* pop the value and then the prefix operator - assume they'll still be there until we're done */
                    HeapPopStack(Parser->pc, NULL, sizeof(struct ExpressionStack) + sizeof(struct Value) + TypeStackSizeValue(TopValue));
                    HeapPopStack(Parser->pc, TopOperatorNode, sizeof(struct ExpressionStack));
                    *StackTop = TopOperatorNode->Next;

                    /* do the prefix operation.
                     * When short-circuiting (IgnorePrecedence set), skip operators
                     * that bind tighter than the short-circuit level (Found < Ignore). */
                    {
                        int DoRun = (Parser->Mode == RunModeRun);
                        if (DoRun && *IgnorePrecedence < DEEP_PRECEDENCE && FoundPrecedence > *IgnorePrecedence)
                            DoRun = FALSE;
                        if (DoRun)
                            ExpressionPrefixOperator(Parser, StackTop, TopOperatorNode->Op, TopValue);
                        else
                            ExpressionPushInt(Parser, StackTop, 0);
                    }
                    break;

                case OrderPostfix:
                    /* postfix evaluation */
                    debugf("postfix evaluation\n");
                    TopValue = TopStackNode->Next->Val;

                    /* pop the postfix operator and then the value - assume they'll still be there until we're done */
                    HeapPopStack(Parser->pc, NULL, sizeof(struct ExpressionStack));
                    HeapPopStack(Parser->pc, TopValue, sizeof(struct ExpressionStack) + sizeof(struct Value) + TypeStackSizeValue(TopValue));
                    *StackTop = TopStackNode->Next->Next;

                    /* do the postfix operation (same short-circuit rule as prefix) */
                    {
                        int DoRun = (Parser->Mode == RunModeRun);
                        if (DoRun && *IgnorePrecedence < DEEP_PRECEDENCE && FoundPrecedence > *IgnorePrecedence)
                            DoRun = FALSE;
                        if (DoRun)
                            ExpressionPostfixOperator(Parser, StackTop, TopOperatorNode->Op, TopValue);
                        else
                            ExpressionPushInt(Parser, StackTop, 0);
                    }
                    break;

                case OrderInfix:
                    /* infix evaluation */
                    debugf("infix evaluation\n");
                    TopValue = TopStackNode->Val;
                    if (TopValue != NULL)
                    {
                        BottomValue = TopOperatorNode->Next->Val;
                        /* Caret for div/mod/zero and similar: point at the right operand */
                        {
                            short int TopPos = TopStackNode->CharacterPos;
                            short int TopLine = TopStackNode->Line;
if (TopLine > 0)
                                Parser->Line = TopLine;
                            if (TopPos > 0)
                                Parser->CharacterPos = TopPos;
                        }

                        /* pop a value, the operator and another value - assume they'll still be there until we're done */
                        HeapPopStack(Parser->pc, NULL, sizeof(struct ExpressionStack) + sizeof(struct Value) + TypeStackSizeValue(TopValue));
                        HeapPopStack(Parser->pc, NULL, sizeof(struct ExpressionStack));
                        HeapPopStack(Parser->pc, BottomValue, sizeof(struct ExpressionStack) + sizeof(struct Value) + TypeStackSizeValue(BottomValue));
                        *StackTop = TopOperatorNode->Next->Next;

                        /* do the infix operation (same short-circuit rule as prefix) */
                        {
                            int DoRun = (Parser->Mode == RunModeRun);
                            if (DoRun && *IgnorePrecedence < DEEP_PRECEDENCE && FoundPrecedence > *IgnorePrecedence)
                                DoRun = FALSE;
                            if (DoRun)
                                ExpressionInfixOperator(Parser, StackTop, TopOperatorNode->Op, BottomValue, TopValue);
                            else
                                ExpressionPushInt(Parser, StackTop, 0);
                        }
                    }
                    else
                        FoundPrecedence = -1;
                    break;

                case OrderNone:
                    /* this should never happen */
                    assert(TopOperatorNode->Order != OrderNone);
                    break;
            }

            /* if we've returned above the ignored precedence level turn ignoring off */
            if (FoundPrecedence <= *IgnorePrecedence)
                *IgnorePrecedence = DEEP_PRECEDENCE;
        }
#ifdef DEBUG_EXPRESSIONS
        ExpressionStackShow(Parser->pc, *StackTop);
#endif
        TopStackNode = *StackTop;
    }
    debugf("ExpressionStackCollapse() finished\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* push an operator on to the expression stack */
void ExpressionStackPushOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum OperatorOrder Order, enum LexToken Token, int Precedence)
{
    struct ExpressionStack *StackNode = VariableAlloc(Parser->pc, Parser, sizeof(struct ExpressionStack), FALSE);
    StackNode->Next = *StackTop;
    StackNode->Order = Order;
    StackNode->Op = Token;
    StackNode->Precedence = Precedence;
    *StackTop = StackNode;
    StackNode->Line = Parser->Line;
    StackNode->CharacterPos = Parser->CharacterPos;
    debugf("ExpressionStackPushOperator()\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* do the '.' and '->' operators */
void ExpressionGetStructElement(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Token)
{
    struct Value *Ident;
    int SavedInit = 1;
    struct Value *SavedRoot = NULL;
    struct Value *TrackedRoot = NULL;

    /* Snapshot before LexGetToken — the lexer may allocate on the
     * interpreter stack and clobber the struct Value header. */
    if (Parser->Mode == RunModeRun && StackTop != NULL && *StackTop != NULL && (*StackTop)->Val != NULL)
    {
        SavedRoot = (*StackTop)->Val;
        SavedInit = SavedRoot->IsInitialized;
        if (Token == TokenDot)
            TrackedRoot = ExpressionInitRoot(SavedRoot);
        {
            int Guard = 0;
            while (SavedRoot->LValueFrom != NULL && SavedRoot->LValueFrom != SavedRoot && Guard++ < 8)
                SavedRoot = SavedRoot->LValueFrom;
        }
    }

    /* get the identifier following the '.' or '->' */
    if (LexGetToken(Parser, &Ident, TRUE) != TokenIdentifier)
        ProgramFail(Parser, "need an structure or union member after '%s'", (Token == TokenDot) ? "." : "->");

    if (Parser->Mode == RunModeRun)
    {
        /* look up the struct element */
        struct Value *ParamVal = (*StackTop)->Val;
        struct Value *StructVal = ParamVal;
        struct ValueType *StructType = ParamVal->Typ;
        char *DerefDataLoc = (char *)ParamVal->Val;
        struct Value *MemberValue = NULL;
        struct Value *Result;

        /* if we're doing '->' dereference the struct pointer first */
        if (Token == TokenArrow)
        {
            DerefDataLoc = VariableDereferencePointer(Parser, ParamVal, &StructVal, NULL, &StructType, NULL);
            /* Parity with *p / p[i]: NULL pointer must fail, not SEGV */
            if (DerefDataLoc == NULL)
                ProgramFail(Parser, DiagnosticMsgNullPointerDereference);
            /* StructVal is always NULL from VariableDereferencePointer; keep
             * ParamVal for stack-pop sizing (see HeapPopStack below). */
            if (StructVal == NULL)
                StructVal = ParamVal;
        }

        if (StructType == NULL || (StructType->Base != TypeStruct && StructType->Base != TypeUnion))
            ProgramFail(Parser, "can't use '%s' on something that's not a struct or union %s : it's a %t", (Token == TokenDot) ? "." : "->", (Token == TokenArrow) ? "pointer" : "", ParamVal->Typ);

        if (!TableGet(StructType->Members, Ident->Val->Identifier, &MemberValue, NULL, NULL, NULL))
            ProgramFail(Parser, "doesn't have a member called '%s'", Ident->Val->Identifier);

        /* pop the value - assume it'll still be there until we're done */
        HeapPopStack(Parser->pc, ParamVal, sizeof(struct ExpressionStack) + sizeof(struct Value) + TypeStackSizeValue(StructVal));
        *StackTop = (*StackTop)->Next;

        /* make the result value for this member only */
        {
            enum LexToken PeekAfter = LexGetToken(Parser, NULL, FALSE);
            int PrefixIncDec = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                && ((*StackTop)->Op == TokenIncrement || (*StackTop)->Op == TokenDecrement));
            int NeedLValue = TokenIsAssignmentOp(PeekAfter)
                || PeekAfter == TokenIncrement || PeekAfter == TokenDecrement
                || PeekAfter == TokenDot || PeekAfter == TokenArrow
                || PeekAfter == TokenLeftSquareBracket
                || PrefixIncDec;
            int AddrOfPeek = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                              && (*StackTop)->Op == TokenAmpersand);
            int InSizeofPeek = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                                && (*StackTop)->Op == TokenSizeof);

            if (MemberValue->BitWidth > 0)
            {
                /* bitfield member */
                if (AddrOfPeek)
                    ProgramFail(Parser, "cannot take address of bitfield");
                if (InSizeofPeek)
                    ProgramFail(Parser, "sizeof applied to a bitfield");

                if (NeedLValue)
                {
                    /* LValue: point at the storage unit, carry bit metadata */
                    Result = VariableAllocValueFromExistingData(Parser, MemberValue->Typ,
                        (void *)(DerefDataLoc + MemberValue->Val->Integer), TRUE,
                        SavedRoot != NULL ? SavedRoot : StructVal);
                    Result->BitOffset = MemberValue->BitOffset;
                    Result->BitWidth = MemberValue->BitWidth;
                }
                else
                {
                    /* RValue: extract into a temporary int */
                    long long Extracted = BitFieldExtract(
                        (void *)(DerefDataLoc + MemberValue->Val->Integer),
                        MemberValue->BitOffset, MemberValue->BitWidth,
                        !TypeIsUnsignedBitFieldBase(MemberValue->Typ));
                    Result = VariableAllocValueFromType(Parser->pc, Parser,
                        &Parser->pc->IntType, FALSE, NULL, FALSE);
                    Result->Val->Integer = (int)Extracted;
                    Result->IsInitialized = TRUE;
                }
            }
            else if (MemberValue->Typ != NULL && MemberValue->Typ->Base == TypePointer
                && !NeedLValue)
            {
                /* RValue: copy pointer so "p = p->next" is independent of
                 * struct storage / LValue aliasing in loops. */
                void *PtrVal = NULL;
                memcpy(&PtrVal, DerefDataLoc + MemberValue->Val->Integer, sizeof(PtrVal));
                Result = VariableAllocValueFromType(Parser->pc, Parser,
                    MemberValue->Typ, FALSE, NULL, FALSE);
                Result->Val->Pointer = PtrVal;
                Result->IsInitialized = TRUE;
            }
            else
            {
                Result = VariableAllocValueFromExistingData(Parser, MemberValue->Typ,
                    (void *)(DerefDataLoc + MemberValue->Val->Integer), TRUE,
                    SavedRoot != NULL ? SavedRoot : StructVal);
            }
        }
        if (TrackedRoot != NULL)
            Result->IsInitialized = ExpressionLeafInit(TrackedRoot, Result);
        else
            Result->IsInitialized = SavedInit;
        {
            int AddrOf = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                          && (*StackTop)->Op == TokenAmpersand);
            int InSizeof = (*StackTop != NULL && (*StackTop)->Order == OrderPrefix
                            && (*StackTop)->Op == TokenSizeof);
            int IsAggArray = (TrackedRoot != NULL && Result->Typ != NULL
                              && Result->Typ->Base == TypeArray);

            /* &s.m lets the address escape: the whole object turns permissive */
            if (AddrOf)
            {
                if (TrackedRoot != NULL)
                    TrackedRoot->IsInitialized = TRUE;
                else if (SavedRoot != NULL)
                    SavedRoot->IsInitialized = TRUE;
                Result->IsInitialized = TRUE;
            }
            if (!Result->IsInitialized || (IsAggArray && !InSizeof && !AddrOf))
            {
                enum LexToken PeekTok = LexGetToken(Parser, NULL, FALSE);
                if (IsAggArray && !InSizeof && !AddrOf && PeekTok != TokenLeftSquareBracket)
                    ExpressionInitEscape(TrackedRoot, Result); /* array member decays */
                if (!Result->IsInitialized && !InSizeof
                    && !TokenIsAssignmentOp(PeekTok)
                    && PeekTok != TokenIncrement && PeekTok != TokenDecrement
                    && PeekTok != TokenDot && PeekTok != TokenArrow
                    && PeekTok != TokenLeftSquareBracket)
                    ProgramFail(Parser, DiagnosticMsgUninitializedVariable);
            }
        }
        ExpressionStackPushValueNode(Parser, StackTop, Result);
    }
}

/* parse an expression with operator precedence */
int ExpressionParseEx(struct ParseState *Parser, struct Value **Result, int AllowComma)
{
    struct Value *LexValue;
    int PrefixState = TRUE;
    int Done = FALSE;
    int BracketPrecedence = 0;
    int LocalPrecedence;
    int Precedence = 0;
    int IgnorePrecedence = DEEP_PRECEDENCE;
    struct ExpressionStack *StackTop = NULL;
    int TernaryDepth = 0;
    int TernaryCond[32];  /* condition result for each nested ternary (1=true, 0=false) */

    debugf("ExpressionParse():\n");
    do
    {
        struct ParseState PreState;
        enum LexToken Token;
        int IsCommaOk;

        ParserCopyPos(&PreState, Parser);
        Token = LexGetToken(Parser, &LexValue, TRUE);
        /* offsetof(type, member) — compile-time constant */
        if (Token == TokenOffsetof)
        {
            struct ValueType *Typ = NULL;
            char *Ident = NULL;
            int IsStatic = 0;
            struct Value *MemLex = NULL;
            int Off = 0;
            if (LexGetToken(Parser, NULL, TRUE) != TokenOpenBracket)
                ProgramFail(Parser, "expected '(' after offsetof");
            TypeParse(Parser, &Typ, &Ident, &IsStatic);
            if (Typ == NULL)
                ProgramFail(Parser, "offsetof: type expected");
            if (LexGetToken(Parser, NULL, TRUE) != TokenComma)
                ProgramFail(Parser, "expected ',' in offsetof");
            if (LexGetToken(Parser, &MemLex, TRUE) != TokenIdentifier || MemLex == NULL)
                ProgramFail(Parser, "offsetof: member name expected");
            if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                ProgramFail(Parser, "expected ')' after offsetof");
            if (Typ->Base != TypeStruct && Typ->Base != TypeUnion)
                ProgramFail(Parser, "offsetof: not a struct/union type");
            {
                struct Value *MemVal = NULL;
                if (Typ->Members == NULL
                    || !TableGet(Typ->Members, MemLex->Val->Identifier, &MemVal, NULL, NULL, NULL)
                    || MemVal == NULL)
                    ProgramFail(Parser, "offsetof: unknown member '%s'", MemLex->Val->Identifier);
                Off = MemVal->Val->Integer;
            }
            if (PrefixState)
            {
                ExpressionPushInt(Parser, &StackTop, Off);
                PrefixState = FALSE;
            }
            else
                ProgramFail(Parser, "offsetof not expected here");
            continue;
        }
        /* Comma operator: allowed at statement level (AllowComma), and always
         * inside (...) so (a,b,c) works even when the outer parse is an arg. */
        IsCommaOk = (Token == TokenComma && (AllowComma || BracketPrecedence > 0));
        if ( ( ( IsCommaOk || ((int)Token > TokenComma && (int)Token <= (int)TokenOpenBracket) ) ||
               (Token == TokenCloseBracket && BracketPrecedence != 0)) &&
               (Token != TokenColon || TernaryDepth > 0) )
        {
            /* it's an operator with precedence */
            /* Call after a[i]/fp when PrefixState still true */
            if (PrefixState && Token == TokenOpenBracket && StackTop != NULL
                && StackTop->Val != NULL && StackTop->Val->Typ != NULL
                && (StackTop->Val->Typ->Base == TypeFunction
                    || (StackTop->Val->Typ->Base == TypePointer
                        && StackTop->Val->Typ->FromType != NULL
                        && StackTop->Val->Typ->FromType->Base == TypeFunction)))
                PrefixState = FALSE;

            if (PrefixState)
            {
                /* expect a prefix operator */
                if (OperatorPrecedence[(int)Token].PrefixPrecedence == 0)
                    ProgramFail(Parser, "operator not expected here");

                LocalPrecedence = OperatorPrecedence[(int)Token].PrefixPrecedence;
                Precedence = BracketPrecedence + LocalPrecedence;

                if (Token == TokenOpenBracket)
                {
                    /* it's either a new bracket level or a cast */
                    enum LexToken BracketToken = LexGetToken(Parser, &LexValue, FALSE);
                    if (IsTypeToken(Parser, BracketToken, LexValue) && (StackTop == NULL || StackTop->Op != TokenSizeof) )
                    {
                        /* it's a cast - get the new type */
                        struct ValueType *CastType;
                        char *CastIdentifier;
                        struct Value *CastTypeValue;

                        TypeParse(Parser, &CastType, &CastIdentifier, NULL);
                        if (LexGetToken(Parser, &LexValue, TRUE) != TokenCloseBracket)
                            ProgramFail(Parser, "brackets not closed");

                        /* scan and collapse the stack to the precedence of this infix cast operator, then push */
                        Precedence = BracketPrecedence + OperatorPrecedence[(int)TokenCast].PrefixPrecedence;

                        Parser->pc->ExprCollapseTok = TokenCast;
                        ExpressionStackCollapse(Parser, &StackTop, Precedence+1, &IgnorePrecedence);
                        CastTypeValue = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->TypeType, FALSE, NULL, FALSE);
                        CastTypeValue->Val->Typ = CastType;
                        ExpressionStackPushValueNode(Parser, &StackTop, CastTypeValue);
                        ExpressionStackPushOperator(Parser, &StackTop, OrderInfix, TokenCast, Precedence);
                    }
                    else
                    {
                        /* boost the bracket operator precedence */
                        BracketPrecedence += BRACKET_PRECEDENCE;
                    }
                }
                else
                {
                    /* scan and collapse the stack to the precedence of this operator, then push */

                    /* take some extra care for double prefix operators, e.g. x = - -5, or x = **y */
                    int NextToken = LexGetToken(Parser, NULL, FALSE);
                    int TempPrecedenceBoost = 0;
                    if (NextToken > TokenComma && NextToken < TokenOpenBracket)
                    {
                        int NextPrecedence = OperatorPrecedence[(int)NextToken].PrefixPrecedence;

                        /* two prefix operators with equal precedence? make sure the innermost one runs first */
                        /* XXX - probably not correct, but can't find a test that fails at this */
                        if (LocalPrecedence == NextPrecedence)
                            TempPrecedenceBoost = -1;
                    }

                    Parser->pc->ExprCollapseTok = Token;
                    ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                    ExpressionStackPushOperator(Parser, &StackTop, OrderPrefix, Token, Precedence + TempPrecedenceBoost);
                }
            }
            else
            {
                /* expect an infix or postfix operator */
                if (OperatorPrecedence[(int)Token].PostfixPrecedence != 0)
                {
                    switch (Token)
                    {
                        case TokenCloseBracket:
                        case TokenRightSquareBracket:
                            if (BracketPrecedence == 0)
                            {
                                /* assume this bracket is after the end of the expression */
                                ParserCopyPos(Parser, &PreState);
                                Done = TRUE;
                            }
                            else
                            {
                                /* collapse to the bracket precedence */
                                Parser->pc->ExprCollapseTok = Token;
                                ExpressionStackCollapse(Parser, &StackTop, BracketPrecedence, &IgnorePrecedence);
                                BracketPrecedence -= BRACKET_PRECEDENCE;
                                /* Pending '[' was pushed at (outer BracketPrecedence)+15
                                 * while BracketPrecedence was boosted by 20, so the
                                 * collapse above did not evaluate it. Evaluate now so
                                 * tab[i](args) sees the element (fptr), not the index. */
                                if (Token == TokenRightSquareBracket)
                                {
                                    int IdxPrec = OperatorPrecedence[(int)TokenLeftSquareBracket].InfixPrecedence;
                                    Parser->pc->ExprCollapseTok = TokenLeftSquareBracket;
                                    ExpressionStackCollapse(Parser, &StackTop,
                                        BracketPrecedence + IdxPrec, &IgnorePrecedence);
                                    PrefixState = FALSE;
                                }
                            }
                            break;

                        default:
                            /* scan and collapse the stack to the precedence of this operator, then push */
                            Precedence = BracketPrecedence + OperatorPrecedence[(int)Token].PostfixPrecedence;
                            Parser->pc->ExprCollapseTok = Token;
                            ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                            ExpressionStackPushOperator(Parser, &StackTop, OrderPostfix, Token, Precedence);
                            break;
                    }
                }
                else if (OperatorPrecedence[(int)Token].InfixPrecedence != 0)
                {
                    /* scan and collapse the stack, then push */
                    Precedence = BracketPrecedence + OperatorPrecedence[(int)Token].InfixPrecedence;

                    /* for right to left order, only go down to the next higher precedence so we evaluate it in reverse order */
                    /* for left to right order, collapse down to this precedence so we evaluate it in forward order */
                    Parser->pc->ExprCollapseTok = Token;
                    if (IS_LEFT_TO_RIGHT(OperatorPrecedence[(int)Token].InfixPrecedence))
                        ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                    else
                        ExpressionStackCollapse(Parser, &StackTop, Precedence+1, &IgnorePrecedence);

                    if (Token == TokenDot || Token == TokenArrow)
                    {
                        ExpressionGetStructElement(Parser, &StackTop, Token); /* this operator is followed by a struct element so handle it as a special case */
                    }
                    else
                    {
                        /* if it's a && or || operator we may not need to evaluate the right hand side of the expression */
                        if ( (Token == TokenLogicalOr || Token == TokenLogicalAnd) &&
                             (IS_NUMERIC_COERCIBLE(StackTop->Val) ||
                              StackTop->Val->Typ->Base == TypePointer ||
                              StackTop->Val->Typ->Base == TypeArray) )
                        {
                            long long LHSInt;
                            if (IS_NUMERIC_COERCIBLE(StackTop->Val))
                                LHSInt = ExpressionCoerceInteger(StackTop->Val);
                            else if (StackTop->Val->Typ->Base == TypeArray)
                                LHSInt = 1;
                            else
                                LHSInt = (StackTop->Val->Val->Pointer != NULL);
                            if ( ( (Token == TokenLogicalOr && LHSInt) || (Token == TokenLogicalAnd && !LHSInt) ) &&
                                 (IgnorePrecedence > Precedence) )
                                IgnorePrecedence = Precedence;
                        }

                        /* Ternary short-circuit: only evaluate the taken branch.
                         * Same IgnorePrecedence mechanism used by && / ||.
                         * cond ? then : else
                         *   - if cond false: ignore then (until ':'), evaluate else
                         *   - if cond true:  evaluate then, ignore else (after ':')
                         */
                        if (Token == TokenQuestionMark && StackTop != NULL && StackTop->Val != NULL)
                        {
                            long long Cond;
                            struct Value *CondVal = StackTop->Val;
                            if (IS_NUMERIC_COERCIBLE(CondVal))
                                Cond = ExpressionCoerceInteger(CondVal);
                            else if (CondVal->Typ->Base == TypeArray)
                                Cond = 1;
                            else if (CondVal->Typ->Base == TypePointer)
                                Cond = (CondVal->Val->Pointer != NULL) ? 1 : 0;
                            else
                                Cond = 0;
                            if (TernaryDepth >= 0 && TernaryDepth < 32)
                                TernaryCond[TernaryDepth] = Cond ? 1 : 0;
                            if (!Cond && IgnorePrecedence > Precedence)
                                IgnorePrecedence = Precedence;  /* skip then-branch */
                        }
                        else if (Token == TokenColon && TernaryDepth > 0)
                        {
                            int CondTrue = (TernaryDepth > 0 && TernaryDepth <= 32) ? TernaryCond[TernaryDepth - 1] : 1;
                            if (CondTrue)
                            {
                                /* condition was true: skip else-branch */
                                if (IgnorePrecedence > Precedence)
                                    IgnorePrecedence = Precedence;
                            }
                            else
                            {
                                /* condition was false: force-collapse the then-branch
                                 * WHILE Ignore is still active, so postfix/prefix side
                                 * effects (a++, *p, ...) are not executed. Then clear ignore
                                 * so the else-branch is evaluated normally. */
                                Parser->pc->ExprCollapseTok = Token;
                                ExpressionStackCollapse(Parser, &StackTop, Precedence + 1, &IgnorePrecedence);
                                if (IgnorePrecedence <= Precedence)
                                    IgnorePrecedence = DEEP_PRECEDENCE;
                            }
                        }

                        /* push the operator on the stack */
                        ExpressionStackPushOperator(Parser, &StackTop, OrderInfix, Token, Precedence);
                        PrefixState = TRUE;

                        switch (Token)
                        {
                            case TokenQuestionMark: TernaryDepth++; break;
                            case TokenColon: TernaryDepth--; break;
                            default: break;
                        }
                    }

                    /* treat an open square bracket as an infix array index operator followed by an open bracket */
                    if (Token == TokenLeftSquareBracket)
                    {
                        /* boost the bracket operator precedence, then push */
                        BracketPrecedence += BRACKET_PRECEDENCE;
                    }
                }
                else if (Token == TokenOpenBracket && StackTop != NULL && StackTop->Val != NULL)
                {
                    /* Call through a function or function-pointer value, e.g. (*f)(args)
                     * Put a temporary name binding is not available; invoke via the Value*. */
                    struct Value *Callee = StackTop->Val;
                    struct Value *Fn = Callee;
                    int RunIt = (Parser->Mode == RunModeRun && Precedence < IgnorePrecedence);

                    if (Callee->Typ != NULL &&
                        Callee->Typ->Base == TypePointer &&
                        Callee->Typ->FromType != NULL &&
                        Callee->Typ->FromType->Base == TypeFunction)
                    {
                        Fn = (struct Value *)Callee->Val->Pointer;
                        if (Fn == NULL && RunIt)
                            ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
                    }
                    else if (Callee->Typ == NULL || Callee->Typ->Base != TypeFunction)
                    {
                        if (RunIt)
                            ProgramFail(Parser, "operator not expected here");
                        Fn = NULL;
                    }

                    /* Pop callee from expression stack.
                     * - Global function Values live in the table (ValOnHeap): only ES.
                     * - Expression Values (temp rvalue OR shared LValue from a[i]/p->fp)
                     *   have a Value header on the expression stack — pop ES+Value(+data). */
                    {
                        struct ExpressionStack *OldTop = StackTop;
                        StackTop = StackTop->Next;
                        if (Callee->ValOnHeap)
                            HeapPopStack(Parser->pc, OldTop, sizeof(struct ExpressionStack));
                        else
                            HeapPopStack(Parser->pc, Callee,
                                sizeof(struct ExpressionStack) + sizeof(struct Value)
                                + TypeStackSizeValue(Callee));
                    }

                    if (Fn != NULL && Fn->Typ != NULL && Fn->Typ->Base == TypeFunction)
                        ExpressionParseFunctionCallValue(Parser, &StackTop, Fn, RunIt);
                    else if (RunIt)
                        ProgramFail(Parser, "not a function");
                    else
                    {
                        /* skip arg list */
                        int depth = 1;
                        LexGetToken(Parser, NULL, TRUE); /* consume '(' already taken - Token was OpenBracket, already consumed */
                        /* Token was already consumed at loop start. Re-parse args in skip mode: */
                        while (depth > 0)
                        {
                            enum LexToken T = LexGetToken(Parser, NULL, TRUE);
                            if (T == TokenOpenBracket) depth++;
                            else if (T == TokenCloseBracket) depth--;
                            else if (T == TokenEOF) break;
                        }
                        ExpressionPushInt(Parser, &StackTop, 0);
                    }
                    PrefixState = FALSE;
                }
                else
                    ProgramFail(Parser, "operator not expected here");
            }
        }
        else if (Token == TokenIdentifier)
        {
            /* it's a variable, function or a macro */
            if (!PrefixState)
                ProgramFail(Parser, "identifier not expected here");

            if (LexGetToken(Parser, NULL, FALSE) == TokenOpenBracket)
            {
                ExpressionParseFunctionCall(Parser, &StackTop, LexValue->Val->Identifier, Parser->Mode == RunModeRun && Precedence < IgnorePrecedence);
            }
            else
            {
                if (Parser->Mode == RunModeRun /* && Precedence < IgnorePrecedence */)
                {
                    struct Value *VariableValue = NULL;

                    VariableGet(Parser->pc, Parser, LexValue->Val->Identifier, &VariableValue);
                    if (VariableValue->Typ != NULL && VariableValue->Typ->Base == TypeArray
                        && VariableValue->IsLValue && !VariableValue->IsInitialized
                        && VariableValue->ElemInit != NULL)
                    {
                        /* The bare array name (decay to pointer, passed to a function,
                         * p = a, &a, ...) lets the contents be written behind our back:
                         * treat the whole array as initialised from here on.  Only
                         * a[i] and sizeof(a) keep per-element tracking. */
                        int InSizeof = (StackTop != NULL && StackTop->Order == OrderPrefix
                                        && StackTop->Op == TokenSizeof);
                        if (!InSizeof && LexGetToken(Parser, NULL, FALSE) != TokenLeftSquareBracket)
                            VariableValue->IsInitialized = TRUE;
                    }
                    if (!TypeIsFunctionLike(VariableValue)
                        && VariableValue->IsLValue && !VariableValue->IsInitialized
                        && VariableValue->Typ->Base != TypeArray)
                    {
                        int SkipUse = 0;
                        enum LexToken PeekTok = LexGetToken(Parser, NULL, FALSE);
                        if (TokenIsAssignmentOp(PeekTok)
                            || PeekTok == TokenDot
                            || PeekTok == TokenArrow
                            || PeekTok == TokenLeftSquareBracket
                            || PeekTok == TokenIncrement
                            || PeekTok == TokenDecrement)
                            SkipUse = 1;
                        /* prefix &: address-of is not a value use, and the
                         * address may be written through (policy: alias ->
                         * permissive), so the object counts as initialised. */
                        if (StackTop != NULL && StackTop->Order == OrderPrefix
                            && StackTop->Op == TokenAmpersand)
                        {
                            SkipUse = 1;
                            VariableValue->IsInitialized = TRUE;
                        }
                        /* sizeof x: not evaluated */
                        if (StackTop != NULL && StackTop->Order == OrderPrefix
                            && StackTop->Op == TokenSizeof)
                            SkipUse = 1;
                        /* a struct with some members already written may be
                         * copied / passed as a whole */
                        if (VariableValue->ElemInit != NULL
                            && VariableValue->Typ->Base == TypeStruct
                            && ExpressionInitTest(VariableValue, 0, VariableValue->ElemInitCount, 1))
                            SkipUse = 1;
                        /* (void)x — cast to void discards the value */
                        if (StackTop != NULL && StackTop->Op == TokenCast
                            && StackTop->Next != NULL && StackTop->Next->Val != NULL
                            && StackTop->Next->Val->Typ == &Parser->pc->TypeType
                            && StackTop->Next->Val->Val->Typ != NULL
                            && StackTop->Next->Val->Val->Typ->Base == TypeVoid)
                            SkipUse = 1;
                        if (!SkipUse)
                        {
                            ParserSetCaretOnIdent(Parser, LexValue->Val->Identifier);
                            ProgramFail(Parser, DiagnosticMsgUninitializedVariableNamed,
                                        LexValue->Val->Identifier);
                        }
                    }
                    if (VariableValue->Typ->Base == TypeMacro)
                    {
                        /* evaluate a macro as a kind of simple subroutine */
                        struct ParseState MacroParser;
                        struct Value *MacroResult;

                        ParserCopy(&MacroParser, &VariableValue->Val->MacroDef.Body);
                        MacroParser.Mode = Parser->Mode;
                        if (VariableValue->Val->MacroDef.NumParams != 0)
                            ProgramFail(&MacroParser, "macro arguments missing");

                        if (!ExpressionParse(&MacroParser, &MacroResult) || LexGetToken(&MacroParser, NULL, FALSE) != TokenEndOfFunction)
                            ProgramFail(&MacroParser, "expression expected");

                        /* If the macro yields a string and more adjacent strings
                         * (or string-macros) follow, concatenate like phase 6. */
                        if (MacroResult != NULL &&
                            (MacroResult->Typ == Parser->pc->CharPtrType ||
                             (MacroResult->Typ->Base == TypePointer &&
                              MacroResult->Typ->FromType == &Parser->pc->CharType)))
                        {
                            char *parts[64];
                            int nparts = 0;
                            int total = 0;
                            int i;
                            char *buf;
                            char *reg;
                            enum LexToken Look;
                            struct Value *LookVal = NULL;
                            struct Value *NextVal = NULL;

                            parts[nparts++] = (char *)(MacroResult->Val->Pointer ? MacroResult->Val->Pointer : "");
                            total += (int)strlen(parts[0]);

                            while (nparts < 64)
                            {
                                Look = LexGetToken(Parser, &LookVal, FALSE);
                                if (Look == TokenStringConstant)
                                {
                                    LexGetToken(Parser, &NextVal, TRUE);
                                    parts[nparts] = (char *)NextVal->Val->Pointer;
                                    total += (int)strlen(parts[nparts]);
                                    nparts++;
                                    continue;
                                }
                                if (Look == TokenIdentifier && LookVal != NULL)
                                {
                                    struct Value *MV = NULL;
                                    struct ParseState MP;
                                    struct Value *MR = NULL;
                                    const char *Id = LookVal->Val->Identifier;
                                    if (!VariableDefined(Parser->pc, Id))
                                        break;
                                    VariableGet(Parser->pc, Parser, Id, &MV);
                                    if (MV->Typ->Base != TypeMacro || MV->Val->MacroDef.NumParams != 0)
                                        break;
                                    LexGetToken(Parser, NULL, TRUE);
                                    ParserCopy(&MP, &MV->Val->MacroDef.Body);
                                    MP.Mode = Parser->Mode;
                                    if (!ExpressionParse(&MP, &MR) ||
                                        LexGetToken(&MP, NULL, FALSE) != TokenEndOfFunction)
                                        ProgramFail(&MP, "expression expected");
                                    if (Parser->Mode == RunModeRun)
                                    {
                                        if (MR == NULL || MR->Typ == NULL ||
                                            (MR->Typ != Parser->pc->CharPtrType &&
                                             MR->Typ->Base != TypePointer &&
                                             MR->Typ->Base != TypeArray))
                                            ProgramFail(Parser, "string macro expected in adjacent string concatenation");
                                        parts[nparts] = (char *)(MR->Val->Pointer ? MR->Val->Pointer : "");
                                        /* ExpressionParse left MR on the value stack — free it */
                                        VariableStackPop(Parser, MR);
                                    }
                                    else
                                    {
                                        parts[nparts] = "";
                                        if (MR != NULL)
                                            VariableStackPop(Parser, MR);
                                    }
                                    total += (int)strlen(parts[nparts]);
                                    nparts++;
continue;
                                }
                                break;
                            }

                            if (nparts > 1)
                            {
                                buf = HeapAllocMem(Parser->pc, total + 1);
                                if (buf == NULL)
                                    ProgramFail(Parser, "stack overflow (function call frame)");
                                buf[0] = '\0';
                                for (i = 0; i < nparts; i++)
                                    strcat(buf, parts[i]);
                                reg = TableStrRegister2(Parser->pc, buf, total);
                                HeapFreeMem(Parser->pc, buf);
                                MacroResult->Val->Pointer = reg;
                            }
                        }

                        ExpressionStackPushValueNode(Parser, &StackTop, MacroResult);
                    }
                    else if (VariableValue->Typ == &Parser->pc->VoidType)
                        ProgramFail(Parser, "a void value isn't much use here");
                    else
                        ExpressionStackPushLValue(Parser, &StackTop, VariableValue, 0); /* it's a value variable */
                }
                else
                {
                    /* Skip / Return / Goto: push a dummy, but if this identifier is a
                     * string-producing object-like macro followed by adjacent string
                     * tokens (COL_RED "x" COL_RESET), consume those tokens too so
                     * parsing does not later fail with "value not expected here". */
                    {
                        struct Value *SkipVar = NULL;
                        if (VariableDefined(Parser->pc, LexValue->Val->Identifier))
                        {
                            VariableGet(Parser->pc, Parser, LexValue->Val->Identifier, &SkipVar);
                            if (SkipVar != NULL && SkipVar->Typ->Base == TypeMacro
                                && SkipVar->Val->MacroDef.NumParams == 0)
                            {
                                enum LexToken SkLook;
                                struct Value *SkVal = NULL;
                                for (;;)
                                {
                                    SkLook = LexGetToken(Parser, &SkVal, FALSE);
                                    if (SkLook == TokenStringConstant)
                                    {
                                        LexGetToken(Parser, NULL, TRUE);
                                        continue;
                                    }
                                    if (SkLook == TokenIdentifier && SkVal != NULL
                                        && VariableDefined(Parser->pc, SkVal->Val->Identifier))
                                    {
                                        struct Value *M2 = NULL;
                                        VariableGet(Parser->pc, Parser, SkVal->Val->Identifier, &M2);
                                        if (M2 != NULL && M2->Typ->Base == TypeMacro
                                            && M2->Val->MacroDef.NumParams == 0)
                                        {
                                            LexGetToken(Parser, NULL, TRUE);
                                            continue;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    ExpressionPushInt(Parser, &StackTop, 0);
                }

            }

             /* if we've successfully ignored the RHS turn ignoring off.
              * Use strict < so that after ternary ':' (Precedence == IgnorePrecedence)
              * we do NOT clear Ignore yet — the else branch still needs skipping.
              * Clear when a lower-precedence operator (e.g. assignment) is seen. */
            if (Precedence < IgnorePrecedence && TernaryDepth == 0)
                IgnorePrecedence = DEEP_PRECEDENCE;

            PrefixState = FALSE;
        }
        else if ((int)Token > TokenCloseBracket && (int)Token <= TokenCharacterConstant)
        {
            /* it's a value of some sort, push it */
            if (!PrefixState)
            {
                /* After a previous value, a string literal is only legal as
                 * adjacent-string continuation. In non-run modes just skip. */
                if (Token == TokenStringConstant)
                {
                    if (Parser->Mode != RunModeRun)
                    {
                        /* consume this and further adjacent strings / string-macros */
                        for (;;)
                        {
                            enum LexToken L2;
                            struct Value *V2 = NULL;
                            L2 = LexGetToken(Parser, &V2, FALSE);
                            if (L2 == TokenStringConstant)
                            {
                                LexGetToken(Parser, NULL, TRUE);
                                continue;
                            }
                            if (L2 == TokenIdentifier && V2 != NULL
                                && VariableDefined(Parser->pc, V2->Val->Identifier))
                            {
                                struct Value *M2 = NULL;
                                VariableGet(Parser->pc, Parser, V2->Val->Identifier, &M2);
                                if (M2 != NULL && M2->Typ->Base == TypeMacro
                                    && M2->Val->MacroDef.NumParams == 0)
                                {
                                    LexGetToken(Parser, NULL, TRUE);
                                    continue;
                                }
                            }
                            break;
                        }
                        PrefixState = FALSE;
                        continue; /* main ExpressionParse loop */
                    }
                }
                ProgramFail(Parser, "value not expected here");
            }

            PrefixState = FALSE;

            /* C adjacent string literal concatenation, including after
             * object-like macros that expand to a string:
             *   "a" "b"                      -> "ab"
             *   "x" COL_GREEN "y" COL_RESET  -> "x" + "\033[32m" + "y" + ...
             * (C translation phase 6 after preprocessing)
             */
            if (Token == TokenStringConstant)
            {
                char *parts[64];
                int nparts = 0;
                int total = 0;
                int i;
                char *buf;
                char *reg;
                struct Value *NextVal = NULL;
                enum LexToken Look;
                struct Value *LookVal = NULL;

                parts[nparts++] = (char *)LexValue->Val->Pointer;
                total += (int)strlen(parts[0]);

                while (nparts < 64)
                {
                    Look = LexGetToken(Parser, &LookVal, FALSE);
                    if (Look == TokenStringConstant)
                    {
                        LexGetToken(Parser, &NextVal, TRUE);
                        parts[nparts] = (char *)NextVal->Val->Pointer;
                        total += (int)strlen(parts[nparts]);
                        nparts++;
                        continue;
                    }
                    if (Look == TokenIdentifier && LookVal != NULL)
                    {
                        struct Value *MacroVar = NULL;
                        struct ParseState MacroParser;
                        struct Value *MacroResult = NULL;
                        const char *Ident = LookVal->Val->Identifier;

                        if (!VariableDefined(Parser->pc, Ident))
                            break;
                        VariableGet(Parser->pc, Parser, Ident, &MacroVar);
                        if (MacroVar->Typ->Base != TypeMacro ||
                            MacroVar->Val->MacroDef.NumParams != 0)
                            break;

                        /* consume identifier, evaluate macro body as expression */
                        LexGetToken(Parser, NULL, TRUE);
                        ParserCopy(&MacroParser, &MacroVar->Val->MacroDef.Body);
                        MacroParser.Mode = Parser->Mode;
                        if (!ExpressionParse(&MacroParser, &MacroResult) ||
                            LexGetToken(&MacroParser, NULL, FALSE) != TokenEndOfFunction)
                            ProgramFail(&MacroParser, "expression expected");

                        /* must yield a char* (string) to participate in concat */
                        if (Parser->Mode == RunModeRun)
                        {
                            if (MacroResult == NULL || MacroResult->Typ == NULL ||
                                (MacroResult->Typ != Parser->pc->CharPtrType &&
                                 MacroResult->Typ->Base != TypePointer &&
                                 MacroResult->Typ->Base != TypeArray))
                                ProgramFail(Parser, "string macro expected in adjacent string concatenation");
                            parts[nparts] = (char *)(MacroResult->Val->Pointer ? MacroResult->Val->Pointer : "");
                            VariableStackPop(Parser, MacroResult);
                        }
                        else
                        {
                            /* skip/goto: just consume the macro, no real value */
                            parts[nparts] = "";
                            if (MacroResult != NULL)
                                VariableStackPop(Parser, MacroResult);
                        }
                        total += (int)strlen(parts[nparts]);
                        nparts++;
continue;
                    }
                    break;
                }

                if (nparts > 1)
                {
                    buf = HeapAllocMem(Parser->pc, total + 1);
                    if (buf == NULL)
                        ProgramFail(Parser, "stack overflow (function call frame)");
                    buf[0] = '\0';
                    for (i = 0; i < nparts; i++)
                        strcat(buf, parts[i]);
                    reg = TableStrRegister2(Parser->pc, buf, total);
                    HeapFreeMem(Parser->pc, buf);
                    LexValue->Val->Pointer = reg;
                }
            }

            ExpressionStackPushValue(Parser, &StackTop, LexValue);
        }
        else if (IsTypeToken(Parser, Token, LexValue))
        {
            /* it's a type. push it on the stack like a value. this is used in sizeof() */
            struct ValueType *Typ;
            char *Identifier;
            struct Value *TypeValue;
#ifndef NO_FP
            enum LexToken TypeTokenForSizeof = Token;
            struct ParseState TypeLook;
            ParserCopy(&TypeLook, Parser);
            while (TypeTokenForSizeof == TokenConstType
                || TypeTokenForSizeof == TokenVolatileType)
            {
                TypeTokenForSizeof = LexGetToken(&TypeLook, NULL, TRUE);
            }
#endif

            if (!PrefixState)
                ProgramFail(Parser, "type not expected here");

            PrefixState = FALSE;
            ParserCopyPos(Parser, &PreState);
            TypeParse(Parser, &Typ, &Identifier, NULL);
#ifndef NO_FP
            /* TypeParse deliberately uses FPType as the interpreter's common
             * floating-point working type. sizeof(float), however, must use
             * the declared binary32 object type, not that double-sized slot. */
            if (StackTop != NULL && StackTop->Order == OrderPrefix
                && StackTop->Op == TokenSizeof && TypeTokenForSizeof == TokenFloatType
                && Typ != NULL && Typ->Base == TypeFP)
                Typ = &Parser->pc->FloatType;
#endif
            TypeValue = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->TypeType, FALSE, NULL, FALSE);
            TypeValue->Val->Typ = Typ;
            ExpressionStackPushValueNode(Parser, &StackTop, TypeValue);
        }
        else
        {
            /* it isn't a token from an expression */
            ParserCopyPos(Parser, &PreState);
            Done = TRUE;
        }

    } while (!Done);

    /* check that brackets have been closed */
    if (BracketPrecedence > 0)
        ProgramFail(Parser, "brackets not closed");

    /* scan and collapse the stack to precedence 0 (end of expression) */
    Parser->pc->ExprCollapseTok = TokenNone;
    ExpressionStackCollapse(Parser, &StackTop, 0, &IgnorePrecedence);

    /* fix up the stack and return the result if we're in run mode */
    if (StackTop != NULL)
    {
        /* all that should be left is a single value on the stack */
        if (Parser->Mode == RunModeRun)
        {
            if (StackTop->Order != OrderNone || StackTop->Next != NULL)
                ProgramFail(Parser, "invalid expression");

            *Result = StackTop->Val;
            HeapPopStack(Parser->pc, StackTop, sizeof(struct ExpressionStack));
        }
        else
            HeapPopStack(Parser->pc, StackTop->Val, sizeof(struct ExpressionStack) + sizeof(struct Value) + TypeStackSizeValue(StackTop->Val));
    }

    debugf("ExpressionParse() done\n\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, StackTop);
#endif
    return StackTop != NULL;
}


/* do a parameterised macro call */

/* Full expression including comma operator (C expression, not assignment-expr). */
int ExpressionParse(struct ParseState *Parser, struct Value **Result)
{
    return ExpressionParseEx(Parser, Result, 1);
}

#ifdef WITH_CSTDLIB_EXT
/* stdarg macros have lvalue/type operands which must not be evaluated as
 * ordinary macro arguments. Parse them directly, keeping the cursor and typed
 * argument semantics shared by Tree and Linear execution. */
extern void StdargVaStart(struct ParseState *, struct Value *, struct Value **, int);
extern void StdargVaCopy(struct ParseState *, struct Value *, struct Value **, int);
extern void StdargVaEnd(struct ParseState *, struct Value *, struct Value **, int);
extern struct Value *StdargVaArgValue(struct ParseState *, struct Value *, struct ValueType *);
extern int StdargMacroIntrinsicKind(Picoc *, struct MacroDef *);
static int ExpressionParseStdargStorageCall(struct ParseState *Parser,
    struct ExpressionStack **StackTop, const char *MacroName, struct MacroDef *MDef)
{
    struct Value *Args[2] = { NULL, NULL };
    struct Value *NameValue = NULL;
    struct Value *Result;
    struct ValueType *RequestedType = NULL;
    char *TypeName = NULL;
    enum LexToken Tok;
    int IntrinsicKind = StdargMacroIntrinsicKind(Parser->pc, MDef);
    int IsStart = IntrinsicKind == 1;
    int IsArg = IntrinsicKind == 2;
    int IsEnd = IntrinsicKind == 3;
    int IsCopy = IntrinsicKind == 4;
    if (IntrinsicKind == 0) return 0;
    Tok = LexGetToken(Parser, &NameValue, TRUE);
    if (Tok != TokenIdentifier || NameValue == NULL)
        ProgramFail(Parser, "va_list variable expected");
    VariableGet(Parser->pc, Parser, NameValue->Val->Identifier, &Args[0]);
    if (IsEnd)
    {
        if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
            ProgramFail(Parser, "close bracket expected in va_end");
        if (Parser->Mode == RunModeRun) StdargVaEnd(Parser, NULL, Args, 1);
        ExpressionPushInt(Parser, StackTop, 0);
        return 1;
    }
    if (LexGetToken(Parser, NULL, TRUE) != TokenComma)
        ProgramFail(Parser, "comma expected in %s", MacroName);
    if (IsArg)
    {
        TypeParse(Parser, &RequestedType, &TypeName, NULL);
        if (RequestedType == NULL)
            ProgramFail(Parser, "type expected in va_arg");
        if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
            ProgramFail(Parser, "close bracket expected in va_arg");
        if (Parser->Mode == RunModeRun)
            Result = StdargVaArgValue(Parser, Args[0], RequestedType);
        else
            Result = VariableAllocValueFromType(Parser->pc, Parser,
                RequestedType, FALSE, NULL, FALSE);
        if (Result == NULL)
            ProgramFail(Parser, "out of memory creating va_arg value");
        ExpressionStackPushValueNode(Parser, StackTop, Result);
        return 1;
    }
    Tok = LexGetToken(Parser, &NameValue, TRUE);
    if (Tok != TokenIdentifier || NameValue == NULL)
        ProgramFail(Parser, "identifier expected in %s", MacroName);
    if (IsCopy)
        VariableGet(Parser->pc, Parser, NameValue->Val->Identifier, &Args[1]);
    /* va_start's last parameter is syntax-only; it must not be read. */
    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
        ProgramFail(Parser, "close bracket expected in %s", MacroName);
    if (Parser->Mode == RunModeRun) {
        if (IsStart) StdargVaStart(Parser, NULL, Args, 2);
        else StdargVaCopy(Parser, NULL, Args, 2);
    }
    ExpressionPushInt(Parser, StackTop, 0);
    return 1;
}
#endif

void ExpressionParseMacroCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *MacroName, struct MacroDef *MDef)
{
#ifdef WITH_CSTDLIB_EXT
    if (ExpressionParseStdargStorageCall(Parser, StackTop, MacroName, MDef))
        return;
#endif
    struct Value *ReturnValue = NULL;
    struct Value *Param;
    struct Value **ParamArray = NULL;
    int ArgCount;
    enum LexToken Token;

    if (Parser->Mode == RunModeRun)
    {
        /* create a stack frame for this macro */
#ifndef NO_FP
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->FPType);  /* largest return type there is */
#else
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->IntType);  /* largest return type there is */
#endif
        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        ParamArray = HeapAllocStack(Parser->pc, sizeof(struct Value *) * MDef->NumParams);
        if (ParamArray == NULL)
            ProgramFail(Parser, "stack overflow (function call frame)");
    }
    else
        ExpressionPushInt(Parser, StackTop, 0);

    /* parse arguments */
    ArgCount = 0;
    do {
        if (ExpressionParseEx(Parser, &Param, 0))
        {
            if (Parser->Mode == RunModeRun)
            {
                if (ArgCount < MDef->NumParams)
                    ParamArray[ArgCount] = Param;
                else
                    ProgramFail(Parser, "too many arguments to %s()", MacroName);
            }

            ArgCount++;
            Token = LexGetToken(Parser, NULL, TRUE);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            /* end of argument list? */
            Token = LexGetToken(Parser, NULL, TRUE);
            if (!TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }

    } while (Token != TokenCloseBracket);

    if (Parser->Mode == RunModeRun)
    {
        /* evaluate the macro */
        struct ParseState MacroParser;
        int Count;
        struct Value *EvalValue;

        if (ArgCount < MDef->NumParams)
            ProgramFail(Parser, "not enough arguments to '%s'", MacroName);

        if (MDef->Body.Pos == NULL)
            ProgramFail(Parser, "'%s' is undefined", MacroName);

        ParserCopy(&MacroParser, &MDef->Body);
        MacroParser.Mode = Parser->Mode;
        VariableStackFrameAdd(Parser, MacroName, 0);
        Parser->pc->TopStackFrame->NumParams = ArgCount;
        Parser->pc->TopStackFrame->ReturnValue = ReturnValue;
        for (Count = 0; Count < MDef->NumParams; Count++)
            VariableDefine(Parser->pc, Parser, MDef->ParamName[Count], ParamArray[Count], NULL, TRUE);

        ExpressionParse(&MacroParser, &EvalValue);
        ExpressionAssign(Parser, ReturnValue, EvalValue, TRUE, MacroName, 0, FALSE);
        VariableStackFramePop(Parser);
        HeapPopStackFrame(Parser->pc);
    }
}

/* Call a function given its Value* (open '(' already consumed). */
void ExpressionParseFunctionCallValue(struct ParseState *Parser, struct ExpressionStack **StackTop, struct Value *FuncValue, int RunIt)
{
    struct Value *ReturnValue = NULL;
    struct Value *Param;
    struct Value **ParamArray = NULL;
    int ArgCount;
    enum LexToken Token;
    enum RunMode OldMode = Parser->Mode;
    const char *FuncName = "funcptr";

    if (RunIt)
    {
        if (FuncValue == NULL)
            ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);

        if (FuncValue->Typ->Base == TypePointer &&
            FuncValue->Typ->FromType != NULL &&
            FuncValue->Typ->FromType->Base == TypeFunction)
        {
            FuncValue = (struct Value *)FuncValue->Val->Pointer;
            if (FuncValue == NULL)
                ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
        }

        if (FuncValue->Typ->Base != TypeFunction)
            ProgramFail(Parser, "%t is not a function - can't call", FuncValue->Typ);

        ExpressionStackPushValueByType(Parser, StackTop, FuncValue->Val->FuncDef.ReturnType);
        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        ParamArray = HeapAllocStack(Parser->pc, sizeof(struct Value *) * (FuncValue->Val->FuncDef.NumParams + PICOC_MAX_VARARGS));
        if (ParamArray == NULL)
            ProgramFail(Parser, "stack overflow (function call frame)");
    }
    else
    {
        ExpressionPushInt(Parser, StackTop, 0);
        Parser->Mode = RunModeSkip;
    }

    ArgCount = 0;
    do {
        if (RunIt && ArgCount < FuncValue->Val->FuncDef.NumParams)
            ParamArray[ArgCount] = VariableAllocValueFromType(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamType[ArgCount], FALSE, NULL, FALSE);

        if (ExpressionParseEx(Parser, &Param, 0))
        {
            if (RunIt)
            {
                if (ArgCount < FuncValue->Val->FuncDef.NumParams)
                {
                    ExpressionAssign(Parser, ParamArray[ArgCount], Param, TRUE, FuncName, ArgCount+1, TRUE);
                    VariableStackPop(Parser, Param);
                }
                else
                {
                    if (!FuncValue->Val->FuncDef.VarArgs)
                        ProgramFail(Parser, "too many arguments to function");
                    if (ArgCount >= FuncValue->Val->FuncDef.NumParams + PICOC_MAX_VARARGS)
                        ProgramFail(Parser, "too many arguments to function (max %d varargs)", PICOC_MAX_VARARGS);
                    ParamArray[ArgCount] = Param;
                }
            }
            ArgCount++;
            Token = LexGetToken(Parser, NULL, TRUE);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            Token = LexGetToken(Parser, NULL, TRUE);
            if (Token != TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }
    } while (Token != TokenCloseBracket);

    if (RunIt)
    {
        if (ArgCount < FuncValue->Val->FuncDef.NumParams)
            ProgramFail(Parser, "not enough arguments to function");

        if (FuncValue->Val->FuncDef.Intrinsic == NULL)
        {
            struct ParseState FuncParser;
            int Count;
            int OldScopeID = Parser->ScopeID;

            if (FuncValue->Val->FuncDef.Body.Pos == NULL)
                ProgramFail(Parser, DiagnosticMsgFunctionUndefined);

            VariableStackFrameAdd(Parser, FuncName, FuncValue->Val->FuncDef.Intrinsic ? FuncValue->Val->FuncDef.NumParams : 0);
            Parser->pc->TopStackFrame->NumParams = ArgCount;
            Parser->pc->TopStackFrame->CallAbi.Arguments = ParamArray;
            Parser->pc->TopStackFrame->CallAbi.ArgumentCount = ArgCount;
            Parser->pc->TopStackFrame->CallAbi.FixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
            Parser->pc->TopStackFrame->CallAbi.IsVariadic = FuncValue->Val->FuncDef.VarArgs;
            Parser->pc->TopStackFrame->ReturnValue = ReturnValue;
            for (Count = 0; Count < FuncValue->Val->FuncDef.NumParams; Count++)
                VariableDefine(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamName[Count], ParamArray[Count], NULL, TRUE);

            Parser->ScopeID = OldScopeID + 1;
            /* ParamArray: run Body IR with real args in the IR frame.
             * VariableDefine above keeps VariableGet fallback for *p etc. */
            if (FuncValue->Val->FuncDef.BodyIr != NULL)
            {
                Parser->Mode = RunModeRun;
                IrExecuteFunctionBody(Parser, FuncValue, ParamArray,
                    FuncValue->Val->FuncDef.NumParams);
            }
            else
            {
                ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
                if (ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                    ProgramFail(&FuncParser, "function body error");
                Parser->Mode = FuncParser.Mode;
            }

            if (Parser->Mode == RunModeRun && FuncValue->Val->FuncDef.ReturnType != &Parser->pc->VoidType)
            {
                /* C89: falling off the end of main is equivalent to return 0. */
                if (FuncName != NULL && strcmp(FuncName, "main") == 0
                    && ReturnValue != NULL
                    && (FuncValue->Val->FuncDef.ReturnType == &Parser->pc->IntType
                        || FuncValue->Val->FuncDef.ReturnType->Base == TypeInt))
                {
                    ReturnValue->Val->Integer = 0;
                    ReturnValue->IsInitialized = TRUE;
                }
                else
                    ProgramFail(Parser, "no value returned from a function returning %t", FuncValue->Val->FuncDef.ReturnType);
            }

            VariableStackFramePop(Parser);
        }
        else
        {
            PicocCallIntrinsic(Parser, FuncValue->Val->FuncDef.Intrinsic,
                ReturnValue, ParamArray, ArgCount, FuncName);
            /* Opaque library: pointer args may be written — mark escaped */
            {
                int __ai;
                for (__ai = 0; __ai < ArgCount; __ai++)
                {
                    struct Value *__P = ParamArray[__ai];
                    if (__P != NULL && __P->Typ != NULL
                        && (__P->Typ->Base == TypePointer || __P->Typ->Base == TypeArray
                            || __P->Typ->Base == TypeStruct || __P->Typ->Base == TypeUnion))
                        ExpressionMarkPtrArgEscaped(__P);
                }
            }
        }

        HeapPopStackFrame(Parser->pc);
    }
    Parser->Mode = OldMode;
}

/* do a function call */
void ExpressionParseFunctionCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *FuncName, int RunIt)
{
    struct Value *ReturnValue = NULL;
    struct Value *FuncValue = NULL;
    struct Value *Param;
    struct Value **ParamArray = NULL;
    int ArgCount;
    enum LexToken Token = LexGetToken(Parser, NULL, TRUE);    /* open bracket */
    enum RunMode OldMode = Parser->Mode;

    if (RunIt)
    {
        /* get the function definition */
        VariableGet(Parser->pc, Parser, FuncName, &FuncValue);

        if (FuncValue->Typ->Base == TypeMacro)
        {
            /* this is actually a macro, not a function */
            ExpressionParseMacroCall(Parser, StackTop, FuncName, &FuncValue->Val->MacroDef);
            return;
        }

        /* Function pointer: int (*f)(int) = &fred; f(24); */
        if (FuncValue->Typ->Base == TypePointer &&
            FuncValue->Typ->FromType != NULL &&
            FuncValue->Typ->FromType->Base == TypeFunction)
        {
            FuncValue = (struct Value *)FuncValue->Val->Pointer;
            if (FuncValue == NULL)
                ProgramFail(Parser, DiagnosticMsgNullFunctionPointer);
        }

        if (FuncValue->Typ->Base != TypeFunction)
            ProgramFail(Parser, "%t is not a function - can't call", FuncValue->Typ);

        ExpressionStackPushValueByType(Parser, StackTop, FuncValue->Val->FuncDef.ReturnType);
        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        /* Allocate room for fixed params + a few varargs */
        ParamArray = HeapAllocStack(Parser->pc, sizeof(struct Value *) * (FuncValue->Val->FuncDef.NumParams + PICOC_MAX_VARARGS));
        if (ParamArray == NULL)
            ProgramFail(Parser, "stack overflow (function call frame)");
    }
    else
    {
        ExpressionPushInt(Parser, StackTop, 0);
        Parser->Mode = RunModeSkip;
    }

    /* parse arguments */
    ArgCount = 0;
    do {
        if (RunIt && ArgCount < FuncValue->Val->FuncDef.NumParams)
            ParamArray[ArgCount] = VariableAllocValueFromType(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamType[ArgCount], FALSE, NULL, FALSE);

        if (ExpressionParseEx(Parser, &Param, 0))
        {
            if (RunIt)
            {
                if (ArgCount < FuncValue->Val->FuncDef.NumParams)
                {
                    ExpressionAssign(Parser, ParamArray[ArgCount], Param, TRUE, FuncName, ArgCount+1, TRUE);
                    VariableStackPop(Parser, Param);
                }
                else
                {
                    if (!FuncValue->Val->FuncDef.VarArgs)
                        ProgramFail(Parser, "too many arguments to function");
                    if (ArgCount >= FuncValue->Val->FuncDef.NumParams + PICOC_MAX_VARARGS)
                        ProgramFail(Parser, "too many arguments to %s() (max %d varargs)", FuncName, PICOC_MAX_VARARGS);
                    /* keep the extra argument so the Intrinsic can see it via Param[ArgCount] */
                    ParamArray[ArgCount] = Param;
                }
            }

            ArgCount++;
            Token = LexGetToken(Parser, NULL, TRUE);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            /* end of argument list? */
            Token = LexGetToken(Parser, NULL, TRUE);
            if (!TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }

    } while (Token != TokenCloseBracket);

    if (RunIt)
    {
        /* run the function */
        if (ArgCount < FuncValue->Val->FuncDef.NumParams)
            ProgramFail(Parser, "not enough arguments to '%s'", FuncName);

        if (FuncValue->Val->FuncDef.Intrinsic == NULL)
        {
            /* run a user-defined function */
            struct ParseState FuncParser;
            int Count;
            int OldScopeID = Parser->ScopeID;

            if (FuncValue->Val->FuncDef.Body.Pos == NULL && FuncValue->Val->FuncDef.BodyIr == NULL)
                ProgramFail(Parser, "'%s' is undefined", FuncName);

            VariableStackFrameAdd(Parser, FuncName, FuncValue->Val->FuncDef.Intrinsic ? FuncValue->Val->FuncDef.NumParams : 0);
            Parser->pc->TopStackFrame->NumParams = ArgCount;
            Parser->pc->TopStackFrame->CallAbi.Arguments = ParamArray;
            Parser->pc->TopStackFrame->CallAbi.ArgumentCount = ArgCount;
            Parser->pc->TopStackFrame->CallAbi.FixedArgumentCount = FuncValue->Val->FuncDef.NumParams;
            Parser->pc->TopStackFrame->CallAbi.IsVariadic = FuncValue->Val->FuncDef.VarArgs;
            Parser->pc->TopStackFrame->ReturnValue = ReturnValue;

            Parser->ScopeID = -1;
            for (Count = 0; Count < FuncValue->Val->FuncDef.NumParams; Count++)
                VariableDefine(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamName[Count], ParamArray[Count], NULL, TRUE);
            Parser->ScopeID = OldScopeID;

            /* ParamArray: run Body IR with real args in the IR frame.
             * VariableDefine above keeps VariableGet fallback for *p etc. */
            if (FuncValue->Val->FuncDef.BodyIr != NULL)
            {
                Parser->Mode = RunModeRun;
                IrExecuteFunctionBody(Parser, FuncValue, ParamArray,
                    FuncValue->Val->FuncDef.NumParams);
            }
            else
            {
                int GotoGuard = 0;
                const char *Pending = NULL;
                ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
                if (ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                    ProgramFail(&FuncParser, "function body expected");
                while (RunIt && FuncParser.Mode == RunModeGoto
                       && FuncParser.SearchGotoLabel != NULL
                       && GotoGuard++ < 256)
                {
                    int MatchesBefore = BabycGotoMatchCount;
                    Pending = FuncParser.SearchGotoLabel;
                    ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
                    FuncParser.Mode = RunModeGoto;
                    FuncParser.SearchGotoLabel = Pending;
                    if (ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                        ProgramFail(&FuncParser, "function body expected");
                    if (FuncParser.Mode == RunModeGoto
                        && BabycGotoMatchCount == MatchesBefore)
                        break;
                }
                Parser->Mode = FuncParser.Mode;
                if (RunIt && FuncParser.Mode == RunModeGoto)
                    ProgramFail(&FuncParser, "couldn't find goto label '%s'",
                        FuncParser.SearchGotoLabel ? FuncParser.SearchGotoLabel : "?");
            }

            if (RunIt)
            {
                if (Parser->Mode == RunModeRun && FuncValue->Val->FuncDef.ReturnType != &Parser->pc->VoidType)
                {
                    if (FuncName != NULL && strcmp(FuncName, "main") == 0
                        && ReturnValue != NULL
                        && (FuncValue->Val->FuncDef.ReturnType == &Parser->pc->IntType
                            || FuncValue->Val->FuncDef.ReturnType->Base == TypeInt))
                    {
                        ReturnValue->Val->Integer = 0;
                        ReturnValue->IsInitialized = TRUE;
                    }
                    else
                        ProgramFail(Parser, "no value returned from a function returning %t", FuncValue->Val->FuncDef.ReturnType);
                }
            }

            VariableStackFramePop(Parser);
        }
        else
        {
            PicocCallIntrinsic(Parser, FuncValue->Val->FuncDef.Intrinsic,
                ReturnValue, ParamArray, ArgCount, FuncName);
            /* Opaque library: pointer args may be written — mark escaped */
            {
                int __ai;
                for (__ai = 0; __ai < ArgCount; __ai++)
                {
                    struct Value *__P = ParamArray[__ai];
                    if (__P != NULL && __P->Typ != NULL
                        && (__P->Typ->Base == TypePointer || __P->Typ->Base == TypeArray
                            || __P->Typ->Base == TypeStruct || __P->Typ->Base == TypeUnion))
                        ExpressionMarkPtrArgEscaped(__P);
                }
            }
        }

        HeapPopStackFrame(Parser->pc);
    }

    Parser->Mode = OldMode;
}

/* parse an expression */
/* Fast path: Ident RelOp Integer  (common in for/while conditions) */
static int ExpressionParseIntSimpleCompare(struct ParseState *Parser, PTR_TYPE_INT_SIGNED *Out)
{
    struct ParseState Saved;
    struct Value *LexVal;
    enum LexToken Tok, Op;
    char *Name;
    PTR_TYPE_INT_SIGNED Right;
    struct Value *Var;

    ParserCopyPos(&Saved, Parser);

    Tok = LexGetToken(Parser, &LexVal, TRUE);
    if (Tok != TokenIdentifier)
    {
        ParserCopyPos(Parser, &Saved);
        return FALSE;
    }
    Name = LexVal->Val->Identifier;

    Op = LexGetToken(Parser, NULL, TRUE);
    if (Op != TokenLessThan && Op != TokenLessEqual && Op != TokenGreaterThan
        && Op != TokenGreaterEqual && Op != TokenEqual && Op != TokenNotEqual)
    {
        ParserCopyPos(Parser, &Saved);
        return FALSE;
    }

    Tok = LexGetToken(Parser, &LexVal, TRUE);
    if (Tok != TokenIntegerConstant)
    {
        ParserCopyPos(Parser, &Saved);
        return FALSE;
    }
    Right = ExpressionCoerceInteger(LexVal);

    /* Only if this is the whole expression — not i < 5 && ..., i < n + 1, etc. */
    {
        enum LexToken Rest = LexGetToken(Parser, NULL, FALSE);
        if (Rest != TokenCloseBracket && Rest != TokenSemicolon
            && Rest != TokenComma && Rest != TokenColon
            && Rest != TokenRightBrace && Rest != TokenEOF
            && Rest != TokenQuestionMark)
        {
            ParserCopyPos(Parser, &Saved);
            return FALSE;
        }
    }

    /* Success — evaluate if running */
    if (Parser->Mode == RunModeRun)
    {
        VariableGet(Parser->pc, Parser, Name, &Var);
        {
            PTR_TYPE_INT_SIGNED Left = ExpressionCoerceInteger(Var);
            switch (Op)
            {
                case TokenLessThan:      *Out = Left < Right; break;
                case TokenLessEqual:     *Out = Left <= Right; break;
                case TokenGreaterThan:   *Out = Left > Right; break;
                case TokenGreaterEqual:  *Out = Left >= Right; break;
                case TokenEqual:         *Out = Left == Right; break;
                case TokenNotEqual:      *Out = Left != Right; break;
                default: *Out = 0; break;
            }
        }
    }
    else
        *Out = TRUE;
    return TRUE;
}

PTR_TYPE_INT_SIGNED ExpressionParseInt(struct ParseState *Parser)
{
    struct Value *Val;
    PTR_TYPE_INT_SIGNED Result = 0;
    /* Hot path for for/while: i < N */
    if (ExpressionParseIntSimpleCompare(Parser, &Result))
        return Result;

    /* assignment-expression: no top-level comma (enum e=1, f=2; case labels; etc.) */
    if (!ExpressionParseEx(Parser, &Val, 0))
        ProgramFail(Parser, "expression expected");

    if (Parser->Mode == RunModeRun)
    {
        if (IS_NUMERIC_COERCIBLE(Val))
            Result = ExpressionCoerceInteger(Val);
        else if (Val->Typ->Base == TypePointer || Val->Typ->Base == TypeArray)
            Result = (Val->Val->Pointer != NULL) ? 1 : 0;
        else
            ProgramFail(Parser, "integer value expected instead of %t", Val->Typ);

        VariableStackPop(Parser, Val);
    }

    return Result;
}

