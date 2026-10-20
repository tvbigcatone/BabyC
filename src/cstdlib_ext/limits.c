/* limits.h — integer type limits for BabyC / PicoC */
#include <limits.h>
#include "../interpreter.h"

/* Host values; PicoC float==double but integer sizes match the host ABI. */
static int CharMinValue = CHAR_MIN;
static int CharMaxValue = CHAR_MAX;
static int SCharMinValue = SCHAR_MIN;
static int SCharMaxValue = SCHAR_MAX;
static int UCharMaxValue = UCHAR_MAX;
static int ShrtMinValue = SHRT_MIN;
static int ShrtMaxValue = SHRT_MAX;
static int UShrtMaxValue = USHRT_MAX;
static int IntMinValue = INT_MIN;
static int IntMaxValue = INT_MAX;
static unsigned int UIntMaxValue = UINT_MAX;
static long LongMinValue = LONG_MIN;
static long LongMaxValue = LONG_MAX;
static unsigned long ULongMaxValue = ULONG_MAX;
static long long LLongMinValue = LLONG_MIN;
static long long LLongMaxValue = LLONG_MAX;
static unsigned long long ULLongMaxValue = ULLONG_MAX;
static int CharBitValue = CHAR_BIT;
static int MbLenMaxValue = MB_LEN_MAX;

void LimitsSetupFunc(Picoc *pc)
{
    VariableDefinePlatformVar(pc, NULL, "CHAR_BIT", &pc->IntType,
        (union AnyValue *)&CharBitValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "MB_LEN_MAX", &pc->IntType,
        (union AnyValue *)&MbLenMaxValue, FALSE);

    VariableDefinePlatformVar(pc, NULL, "CHAR_MIN", &pc->IntType,
        (union AnyValue *)&CharMinValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "CHAR_MAX", &pc->IntType,
        (union AnyValue *)&CharMaxValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "SCHAR_MIN", &pc->IntType,
        (union AnyValue *)&SCharMinValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "SCHAR_MAX", &pc->IntType,
        (union AnyValue *)&SCharMaxValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "UCHAR_MAX", &pc->IntType,
        (union AnyValue *)&UCharMaxValue, FALSE);

    VariableDefinePlatformVar(pc, NULL, "SHRT_MIN", &pc->IntType,
        (union AnyValue *)&ShrtMinValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "SHRT_MAX", &pc->IntType,
        (union AnyValue *)&ShrtMaxValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "USHRT_MAX", &pc->IntType,
        (union AnyValue *)&UShrtMaxValue, FALSE);

    VariableDefinePlatformVar(pc, NULL, "INT_MIN", &pc->IntType,
        (union AnyValue *)&IntMinValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "INT_MAX", &pc->IntType,
        (union AnyValue *)&IntMaxValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "UINT_MAX", &pc->UnsignedIntType,
        (union AnyValue *)&UIntMaxValue, FALSE);

    VariableDefinePlatformVar(pc, NULL, "LONG_MIN", &pc->LongType,
        (union AnyValue *)&LongMinValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "LONG_MAX", &pc->LongType,
        (union AnyValue *)&LongMaxValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "ULONG_MAX", &pc->UnsignedLongType,
        (union AnyValue *)&ULongMaxValue, FALSE);

    VariableDefinePlatformVar(pc, NULL, "LLONG_MIN", &pc->LongLongType,
        (union AnyValue *)&LLongMinValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "LLONG_MAX", &pc->LongLongType,
        (union AnyValue *)&LLongMaxValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "ULLONG_MAX", &pc->UnsignedLongLongType,
        (union AnyValue *)&ULLongMaxValue, FALSE);
}
