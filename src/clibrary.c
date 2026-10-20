/* picoc mini standard C library - provides an optional tiny C standard library
 * if BUILTIN_MINI_STDLIB is defined */

#include "picoc.h"
#include "interpreter.h"
#include <string.h>
#include <stdio.h>

#ifndef NO_FP
#include <math.h>
#endif


 /* endian-ness checking */
static const int __ENDIAN_CHECK__ = 1;
static int BigEndian;
static int LittleEndian;


/* global initialisation for libraries */
void LibExit(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs);

void LibExit(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    PlatformExit(Parser->pc, Param[0]->Val->Integer);
}

void LibAtoi(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char *s = Param[0]->Val->Pointer;
    int n = 0;
    int sign = 1;
    if (s == NULL) { ReturnValue->Val->Integer = 0; return; }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    ReturnValue->Val->Integer = sign * n;
}



void LibraryInit(Picoc* pc)
{
    /* define the version number macro */
    pc->VersionString = TableStrRegister(pc, PICOC_VERSION);
    VariableDefinePlatformVar(pc, NULL, "PICOC_VERSION", pc->CharPtrType, (union AnyValue*)&pc->VersionString, FALSE);

    /* define endian-ness macros */
    BigEndian = ((*(char*)&__ENDIAN_CHECK__) == 0);
    LittleEndian = ((*(char*)&__ENDIAN_CHECK__) == 1);

    VariableDefinePlatformVar(pc, NULL, "BIG_ENDIAN", &pc->IntType, (union AnyValue*)&BigEndian, FALSE);
    VariableDefinePlatformVar(pc, NULL, "LITTLE_ENDIAN", &pc->IntType, (union AnyValue*)&LittleEndian, FALSE);

    /* size_t / ssize_t always available (no need to #include <stddef.h>).
     * offsetof is a builtin keyword (TokenOffsetof), not a macro. */
    {
        const char *defs =
            "typedef unsigned int size_t;"
            "typedef int ssize_t;";
        PicocParse(pc, "stddef.h", defs, (int)strlen(defs), TRUE, FALSE, FALSE, FALSE);
    }
    /* Always provide exit() without requiring stdlib.h */
    {
        static struct LibraryFunction ExitOnly[] = {
            { LibExit, "void exit(int);" },
            { LibAtoi, "int atoi(char *);" },
            { NULL, NULL }
        };
        LibraryAdd(pc, &pc->GlobalTable, "exit-only", &ExitOnly[0]);
    }
}

/* add a library */
void LibraryAdd(Picoc* pc, struct Table* GlobalTable, const char* LibraryName, struct LibraryFunction* FuncList)
{
    struct ParseState Parser;
    int Count;
    char* Identifier;
    struct ValueType* ReturnType;
    struct Value* NewValue;
    void* Tokens;
    char* IntrinsicName = TableStrRegister(pc, "c library");

    /* read all the library definitions */
    for (Count = 0; FuncList[Count].Prototype != NULL; Count++)
    {
        Tokens = LexAnalyse(pc, IntrinsicName, FuncList[Count].Prototype, strlen((char*)FuncList[Count].Prototype), NULL);
        LexInitParser(&Parser, pc, FuncList[Count].Prototype, Tokens, IntrinsicName, TRUE, FALSE);
        TypeParse(&Parser, &ReturnType, &Identifier, NULL);
        /* Skip if already registered (e.g. exit from exit-only and again from stdlib.h) */
        if (Identifier != NULL && VariableDefined(pc, Identifier))
        {
            HeapFreeMem(pc, Tokens);
            continue;
        }
        NewValue = ParseFunctionDefinition(&Parser, ReturnType, Identifier);
        NewValue->Val->FuncDef.Intrinsic = FuncList[Count].Func;
        HeapFreeMem(pc, Tokens);
    }
}

/* print a type to a stream without using printf/sprintf */
void PrintType(struct ValueType* Typ, IOFILE* Stream)
{
    switch (Typ->Base)
    {
    case TypeVoid:              PrintStr("void", Stream); break;
    case TypeInt:               PrintStr("int", Stream); break;
    case TypeShort:             PrintStr("short", Stream); break;
    case TypeChar:              PrintStr("char", Stream); break;
    case TypeLong:              PrintStr("long", Stream); break;
    case TypeLongLong:          PrintStr("long long", Stream); break;
    case TypeUnsignedInt:       PrintStr("unsigned int", Stream); break;
    case TypeUnsignedShort:     PrintStr("unsigned short", Stream); break;
    case TypeUnsignedLong:      PrintStr("unsigned long", Stream); break;
    case TypeUnsignedLongLong:  PrintStr("unsigned long long", Stream); break;
    case TypeUnsignedChar:      PrintStr("unsigned char", Stream); break;
#ifndef NO_FP
    case TypeFP:                PrintStr("double", Stream); break;
#endif
    case TypeFunction:          PrintStr("function", Stream); break;
    case TypeMacro:             PrintStr("macro", Stream); break;
    case TypePointer:           if (Typ->FromType) PrintType(Typ->FromType, Stream); PrintCh('*', Stream); break;
    case TypeArray:             PrintType(Typ->FromType, Stream); PrintCh('[', Stream); if (Typ->ArraySize != 0) PrintSimpleInt(Typ->ArraySize, Stream); PrintCh(']', Stream); break;
    case TypeStruct:            PrintStr("struct ", Stream); PrintStr(Typ->Identifier, Stream); break;
    case TypeUnion:             PrintStr("union ", Stream); PrintStr(Typ->Identifier, Stream); break;
    case TypeEnum:              PrintStr("enum ", Stream); PrintStr(Typ->Identifier, Stream); break;
    case TypeGotoLabel:         PrintStr("goto label ", Stream); break;
    case Type_Type:             PrintStr("type ", Stream); break;
    }
}


#ifdef BUILTIN_MINI_STDLIB

/*
 * This is a simplified standard library for small embedded systems. It doesn't require
 * a system stdio library to operate.
 *
 * A more complete standard library for larger computers is in the library_XXX.c files.
 */

static int TRUEValue = 1;
static int ZeroValue = 0;

void BasicIOInit(Picoc* pc)
{
    pc->CStdOutBase.Putch = &PlatformPutc;
    pc->CStdOut = &pc->CStdOutBase;
}

/* initialise the C library */
void CLibraryInit(Picoc* pc)
{
    /* define some constants */
    VariableDefinePlatformVar(pc, NULL, "NULL", &pc->IntType, (union AnyValue*)&ZeroValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "TRUE", &pc->IntType, (union AnyValue*)&TRUEValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "FALSE", &pc->IntType, (union AnyValue*)&ZeroValue, FALSE);
}

/* stream for writing into strings */
void SPutc(unsigned char Ch, union OutputStreamInfo* Stream)
{
    struct StringOutputStream* Out = &Stream->Str;
    *Out->WritePos++ = Ch;
}

/* print a character to a stream without using printf/sprintf */
void PrintCh(char OutCh, struct OutputStream* Stream)
{
    (*Stream->Putch)(OutCh, &Stream->i);
}

/* print a string to a stream without using printf/sprintf */
void PrintStr(const char* Str, struct OutputStream* Stream)
{
    while (*Str != 0)
        PrintCh(*Str++, Stream);
}

/* print a single character a given number of times */
void PrintRepeatedChar(Picoc* pc, char ShowChar, int Length, struct OutputStream* Stream)
{
    while (Length-- > 0)
        PrintCh(ShowChar, Stream);
}

/* print an unsigned integer to a stream without using printf/sprintf */
void PrintUnsigned(unsigned long Num, unsigned int Base, int FieldWidth, int ZeroPad, int LeftJustify, struct OutputStream* Stream)
{
    char Result[33];
    int ResPos = sizeof(Result);

    Result[--ResPos] = '\0';
    if (Num == 0)
        Result[--ResPos] = '0';

    while (Num > 0)
    {
        unsigned long NextNum = Num / Base;
        unsigned long Digit = Num - NextNum * Base;
        if (Digit < 10)
            Result[--ResPos] = '0' + Digit;
        else
            Result[--ResPos] = 'a' + Digit - 10;

        Num = NextNum;
    }

    if (FieldWidth > 0 && !LeftJustify)
        PrintRepeatedChar(ZeroPad ? '0' : ' ', FieldWidth - (sizeof(Result) - 1 - ResPos), Stream);

    PrintStr(&Result[ResPos], Stream);

    if (FieldWidth > 0 && LeftJustify)
        PrintRepeatedChar(' ', FieldWidth - (sizeof(Result) - 1 - ResPos), Stream);
}

/* print an unsigned long long integer to a stream without using printf/sprintf */
void PrintUnsignedLongLong(unsigned long long Num, unsigned int Base, int FieldWidth, int ZeroPad, int LeftJustify, struct OutputStream* Stream)
{
    char Result[65];  /* 64 bits in base 2 + null */
    int ResPos = sizeof(Result);

    Result[--ResPos] = '\0';
    if (Num == 0)
        Result[--ResPos] = '0';

    while (Num > 0)
    {
        unsigned long long NextNum = Num / Base;
        unsigned long long Digit = Num - NextNum * Base;
        if (Digit < 10)
            Result[--ResPos] = '0' + (char)Digit;
        else
            Result[--ResPos] = 'a' + (char)(Digit - 10);

        Num = NextNum;
    }

    if (FieldWidth > 0 && !LeftJustify)
        PrintRepeatedChar(ZeroPad ? '0' : ' ', FieldWidth - (sizeof(Result) - 1 - ResPos), Stream);

    PrintStr(&Result[ResPos], Stream);

    if (FieldWidth > 0 && LeftJustify)
        PrintRepeatedChar(' ', FieldWidth - (sizeof(Result) - 1 - ResPos), Stream);
}

/* print an integer to a stream without using printf/sprintf */
void PrintSimpleInt(long Num, struct OutputStream* Stream)
{
    PrintInt(Num, -1, FALSE, FALSE, Stream);
}

/* print an integer to a stream without using printf/sprintf */
void PrintInt(long Num, int FieldWidth, int ZeroPad, int LeftJustify, struct OutputStream* Stream)
{
    if (Num < 0)
    {
        PrintCh('-', Stream);
        Num = -Num;
        if (FieldWidth != 0)
            FieldWidth--;
    }

    PrintUnsigned((unsigned long)Num, 10, FieldWidth, ZeroPad, LeftJustify, Stream);
}

#ifndef NO_FP
/* print a double to a stream without using printf/sprintf */
void PrintFP(double Num, struct OutputStream* Stream)
{
    int Exponent = 0;
    int MaxDecimal;

    if (Num < 0)
    {
        PrintCh('-', Stream);
        Num = -Num;
    }

    if (Num >= 1e7)
        Exponent = log10(Num);
    else if (Num <= 1e-7 && Num != 0.0)
        Exponent = log10(Num) - 0.999999999;

    Num /= pow(10.0, Exponent);
    PrintInt((long)Num, 0, FALSE, FALSE, Stream);
    PrintCh('.', Stream);
    Num = (Num - (long)Num) * 10;
    if (fabs(Num) >= 1e-7)
    {
        for (MaxDecimal = 6; MaxDecimal > 0 && fabs(Num) >= 1e-7; Num = (Num - (long)(Num + 1e-7)) * 10, MaxDecimal--)
            PrintCh('0' + (long)(Num + 1e-7), Stream);
    }
    else
        PrintCh('0', Stream);

    if (Exponent != 0)
    {
        PrintCh('e', Stream);
        PrintInt(Exponent, 0, FALSE, FALSE, Stream);
    }
}
#endif

/* intrinsic functions made available to the language */
void GenericPrintf(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs, struct OutputStream* Stream)
{
    char* FPos;
    struct Value* NextArg = Param[0];
    struct ValueType* FormatType;
    int ArgCount = 1;
    int LeftJustify = FALSE;
    int ZeroPad = FALSE;
    int FieldWidth = 0;
    char* Format = Param[0]->Val->Pointer;

    for (FPos = Format; *FPos != '\0'; FPos++)
    {
        if (*FPos == '%')
        {
            FPos++;
            FieldWidth = 0;
            if (*FPos == '-')
            {
                /* a leading '-' means left justify */
                LeftJustify = TRUE;
                FPos++;
            }

            if (*FPos == '0')
            {
                /* a leading zero means zero pad a decimal number */
                ZeroPad = TRUE;
                FPos++;
            }

            /* get any field width in the format */
            while (isdigit((int)*FPos))
                FieldWidth = FieldWidth * 10 + (*FPos++ - '0');

            /* now check the format type */
            switch (*FPos)
            {
            case 's': FormatType = CharPtrType; break;
            case 'd': case 'u': case 'x': case 'b': case 'c': FormatType = &Parser->pc->IntType; break;
#ifndef NO_FP
            case 'f': FormatType = &Parser->pc->FPType; break;
#endif
            case '%': PrintCh('%', Stream); FormatType = NULL; break;
            case '\0': FPos--; FormatType = NULL; break;
            default:  PrintCh(*FPos, Stream); FormatType = NULL; break;
            }

            if (FormatType != NULL)
            {
                /* we have to format something */
                if (ArgCount >= NumArgs)
                    PrintStr("XXX", Stream);   /* not enough parameters for format */
                else
                {
                    NextArg = (struct Value*)((char*)NextArg + MEM_ALIGN(sizeof(struct Value) + TypeStackSizeValue(NextArg)));
                    if (NextArg->Typ != FormatType &&
                        !((FormatType == &Parser->pc->IntType || *FPos == 'f') && IS_NUMERIC_COERCIBLE(NextArg)) &&
                        !(FormatType == CharPtrType && (NextArg->Typ->Base == TypePointer ||
                            (NextArg->Typ->Base == TypeArray && NextArg->Typ->FromType->Base == TypeChar))))
                        PrintStr("XXX", Stream);   /* bad type for format */
                    else
                    {
                        switch (*FPos)
                        {
                        case 's':
                        {
                            char* Str;

                            if (NextArg->Typ->Base == TypePointer)
                                Str = NextArg->Val->Pointer;
                            else
                                Str = (char *)NextArg->Val;

                            if (Str == NULL)
                                PrintStr("NULL", Stream);
                            else
                                PrintStr(Str, Stream);
                            break;
                        }
                        case 'd': PrintInt(ExpressionCoerceInteger(NextArg), FieldWidth, ZeroPad, LeftJustify, Stream); break;
                        case 'u':
                            if (NextArg->Typ->Base == TypeUnsignedLongLong)
                                PrintUnsignedLongLong(NextArg->Val->UnsignedLongLongInteger, 10, FieldWidth, ZeroPad, LeftJustify, Stream);
                            else
                                PrintUnsigned(ExpressionCoerceUnsignedInteger(NextArg), 10, FieldWidth, ZeroPad, LeftJustify, Stream);
                            break;
                        case 'x':
                            if (NextArg->Typ->Base == TypeUnsignedLongLong)
                                PrintUnsignedLongLong(NextArg->Val->UnsignedLongLongInteger, 16, FieldWidth, ZeroPad, LeftJustify, Stream);
                            else
                                PrintUnsigned(ExpressionCoerceUnsignedInteger(NextArg), 16, FieldWidth, ZeroPad, LeftJustify, Stream);
                            break;
                        case 'b': PrintUnsigned(ExpressionCoerceUnsignedInteger(NextArg), 2, FieldWidth, ZeroPad, LeftJustify, Stream); break;
                        case 'c': PrintCh(ExpressionCoerceUnsignedInteger(NextArg), Stream); break;
#ifndef NO_FP
                        case 'f': PrintFP(ExpressionCoerceFP(NextArg), Stream); break;
#endif
                        }
                    }
                }

                ArgCount++;
            }
        }
        else
            PrintCh(*FPos, Stream);
    }
}

/* printf(): print to console output */
void LibPrintf(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct OutputStream ConsoleStream;

    ConsoleStream.Putch = &PlatformPutc;
    GenericPrintf(Parser, ReturnValue, Param, NumArgs, &ConsoleStream);
}

/* sprintf(): FormatPure magic-box when fmt+int args cacheable */
void LibSPrintf(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct OutputStream StrStream;
    char *Buf, *Fmt;
    long long IArgs[4];
    int NArgs;
    char Tmp[96];

    Buf = Param[0]->Val->Pointer;
    Fmt = (NumArgs >= 2 && Param[1] != NULL && Param[1]->Val != NULL)
        ? (char *)Param[1]->Val->Pointer : NULL;
    NArgs = -1;
    if (Buf != NULL && Fmt != NULL && NumArgs >= 2) {
        NArgs = FmtCacheCollectIntArgs(Param, NumArgs, IArgs, 4);
        if (NArgs >= 0 && FmtCacheLookup(Fmt, IArgs, NArgs, Tmp, 128)) {
            size_t n = strlen(Tmp);
            memcpy(Buf, Tmp, n + 1);
            ReturnValue->Val->Pointer = Param[0]->Val->Pointer;
            {
                int __ml = (Buf != NULL) ? (int)strlen(Buf) + 1 : 1;
                ExpressionMarkBufferWritten(Param[0], Buf, __ml);
            }
            return;
        }
    }

    /* Fast path: safe int-only format → snprintf (avoids GenericPrintf overhead) */
    if (Buf != NULL && Fmt != NULL && NArgs >= 0 && NArgs <= 4
        && FmtCacheFormatIsSafe(Fmt)) {
        int n;
        switch (NArgs) {
            case 0: n = snprintf(Buf, 256, "%s", Fmt); break;
            case 1: n = snprintf(Buf, 256, Fmt, (int)IArgs[0]); break;
            case 2: n = snprintf(Buf, 256, Fmt, (int)IArgs[0], (int)IArgs[1]); break;
            case 3: n = snprintf(Buf, 256, Fmt, (int)IArgs[0], (int)IArgs[1], (int)IArgs[2]); break;
            default: n = snprintf(Buf, 256, Fmt, (int)IArgs[0], (int)IArgs[1], (int)IArgs[2], (int)IArgs[3]); break;
        }
        if (n >= 0) {
            ReturnValue->Val->Pointer = Param[0]->Val->Pointer;
            ExpressionMarkBufferWritten(Param[0], Buf, n + 1);
            if (NArgs >= 0)
                FmtCacheStore(Fmt, IArgs, NArgs, Buf);
            return;
        }
    }

    StrStream.Putch = &SPutc;
    StrStream.i.Str.Parser = Parser;
    StrStream.i.Str.WritePos = Buf;
    GenericPrintf(Parser, ReturnValue, Param + 1, NumArgs - 1, &StrStream);
    PrintCh(0, &StrStream);
    ReturnValue->Val->Pointer = *Param;
    {
                int __ml = (Buf != NULL) ? (int)strlen(Buf) + 1 : 1;
                ExpressionMarkBufferWritten(Param[0], Buf, __ml);
            }

    if (Buf != NULL && Fmt != NULL) {
        NArgs = FmtCacheCollectIntArgs(Param, NumArgs, IArgs, 4);
        if (NArgs >= 0)
            FmtCacheStore(Fmt, IArgs, NArgs, Buf);
    }
}

/* get a line of input. protected from buffer overrun */
void LibGets(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = PlatformGetLine(Param[0]->Val->Pointer, GETS_BUF_MAX, NULL);
    if (ReturnValue->Val->Pointer != NULL)
    {
        char* EOLPos = strchr(Param[0]->Val->Pointer, '\n');
        if (EOLPos != NULL)
            *EOLPos = '\0';
    }
}

void LibGetc(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = PlatformGetCharacter();
}

#ifdef PICOC_LIBRARY
void LibSin(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = sin(Param[0]->Val->FP);
}

void LibCos(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = cos(Param[0]->Val->FP);
}

void LibTan(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = tan(Param[0]->Val->FP);
}

void LibAsin(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = asin(Param[0]->Val->FP);
}

void LibAcos(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = acos(Param[0]->Val->FP);
}

void LibAtan(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = atan(Param[0]->Val->FP);
}

void LibSinh(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = sinh(Param[0]->Val->FP);
}

void LibCosh(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = cosh(Param[0]->Val->FP);
}

void LibTanh(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = tanh(Param[0]->Val->FP);
}

void LibExp(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = exp(Param[0]->Val->FP);
}

void LibFabs(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = fabs(Param[0]->Val->FP);
}

void LibLog(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = log(Param[0]->Val->FP);
}

void LibLog10(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = log10(Param[0]->Val->FP);
}

void LibPow(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = pow(Param[0]->Val->FP, Param[1]->Val->FP);
}

void LibSqrt(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = sqrt(Param[0]->Val->FP);
}

void LibRound(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = floor(Param[0]->Val->FP + 0.5);   /* XXX - fix for soft float */
}

void LibCeil(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = ceil(Param[0]->Val->FP);
}

void LibFloor(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->FP = floor(Param[0]->Val->FP);
}
#endif

#ifndef NO_STRING_FUNCTIONS
/* Keep legacy CLibrary registration on the same tracked allocator path as cstdlib. */
void StdlibMalloc(struct ParseState *, struct Value *, struct Value **, int);
void StdlibCalloc(struct ParseState *, struct Value *, struct Value **, int);
void StdlibRealloc(struct ParseState *, struct Value *, struct Value **, int);
void StdlibFree(struct ParseState *, struct Value *, struct Value **, int);

void LibMalloc(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    StdlibMalloc(Parser, ReturnValue, Param, NumArgs);
}

#ifndef NO_CALLOC
void LibCalloc(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    StdlibCalloc(Parser, ReturnValue, Param, NumArgs);
}
#endif

#ifndef NO_REALLOC
void LibRealloc(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    StdlibRealloc(Parser, ReturnValue, Param, NumArgs);
}
#endif

void LibFree(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    StdlibFree(Parser, ReturnValue, Param, NumArgs);
}

void LibStrcpy(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char* To = (char*)Param[0]->Val->Pointer;
    char* From = (char*)Param[1]->Val->Pointer;

    if (To == NULL)
        ProgramFail(Parser, "strcpy: destination is NULL");
    if (From == NULL)
        ProgramFail(Parser, "strcpy: source is NULL");
    if (PicocMemoryRangesOverlap(To, From, strlen(From) + 1))
        ProgramFail(Parser, "strcpy source and destination overlap (undefined behavior in C)");

    while (*From != '\0')
        *To++ = *From++;

    *To = '\0';
}

void LibStrncpy(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char* To = (char*)Param[0]->Val->Pointer;
    char* From = (char*)Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;

    if (To == NULL)
        ProgramFail(Parser, "strncpy: destination is NULL");
    if (From == NULL)
        ProgramFail(Parser, "strncpy: source is NULL");
    if (Len < 0)
        ProgramFail(Parser, "strncpy: negative size is invalid");
    if (Len == 0)
        return;
    if (PicocMemoryRangesOverlap(To, From, (size_t)Len))
        ProgramFail(Parser, "strncpy source and destination overlap (undefined behavior in C)");

    {
        int Remaining = Len;
        while (Remaining > 0 && *From != '\0')
        {
            *To++ = *From++;
            Remaining--;
        }
        while (Remaining > 0)
        {
            *To++ = '\0';
            Remaining--;
        }
    }
}

void LibStrcmp(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    /* Return unsigned-char delta (not just -1/0/1) for portable teaching output. */
    const unsigned char* Str1 = (const unsigned char*)Param[0]->Val->Pointer;
    const unsigned char* Str2 = (const unsigned char*)Param[1]->Val->Pointer;

    if (Str1 == NULL)
        ProgramFail(Parser, "strcmp: first argument is NULL");
    if (Str2 == NULL)
        ProgramFail(Parser, "strcmp: second argument is NULL");

    while (*Str1 != '\0' && *Str1 == *Str2)
    {
        Str1++;
        Str2++;
    }
    ReturnValue->Val->Integer = (int)*Str1 - (int)*Str2;
}

void LibStrncmp(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const unsigned char* Str1 = (const unsigned char*)Param[0]->Val->Pointer;
    const unsigned char* Str2 = (const unsigned char*)Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;

    if (Str1 == NULL)
        ProgramFail(Parser, "strncmp: first argument is NULL");
    if (Str2 == NULL)
        ProgramFail(Parser, "strncmp: second argument is NULL");

    if (Len < 0)
        ProgramFail(Parser, "strncmp: negative size is invalid");
    if (Len == 0)
    {
        ReturnValue->Val->Integer = 0;
        return;
    }
    while (Len > 0 && *Str1 != '\0' && *Str1 == *Str2)
    {
        Str1++;
        Str2++;
        Len--;
    }
    if (Len == 0)
        ReturnValue->Val->Integer = 0;
    else
        ReturnValue->Val->Integer = (int)*Str1 - (int)*Str2;
}

void LibStrcat(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char* To = (char*)Param[0]->Val->Pointer;
    char* From = (char*)Param[1]->Val->Pointer;

    if (To == NULL)
        ProgramFail(Parser, "strcat: destination is NULL");
    if (From == NULL)
        ProgramFail(Parser, "strcat: source is NULL");
    {
        size_t DstLen = strlen(To), SrcLen = strlen(From);
        if (DstLen > (size_t)-1 - SrcLen - 1)
            ProgramFail(Parser, "strcat: resulting string size overflows size_t");
        if (PicocMemoryRangesOverlap(To, From, DstLen + SrcLen + 1) ||
            PicocMemoryRangesOverlap(From, To, SrcLen + 1))
            ProgramFail(Parser, "strcat source and destination overlap (undefined behavior in C)");
    }

    while (*To != '\0')
        To++;

    while (*From != '\0')
        *To++ = *From++;

    *To = '\0';
}

void LibIndex(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char* Pos = (char*)Param[0]->Val->Pointer;
    if (Pos == NULL) ProgramFail(Parser, "index: string is NULL");
    int SearchChar = Param[1]->Val->Integer;

    while (*Pos != '\0' && *Pos != SearchChar)
        Pos++;

    if (*Pos != SearchChar)
        ReturnValue->Val->Pointer = NULL;
    else
        ReturnValue->Val->Pointer = Pos;
}

void LibRindex(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char* Pos = (char*)Param[0]->Val->Pointer;
    if (Pos == NULL) ProgramFail(Parser, "rindex: string is NULL");
    int SearchChar = Param[1]->Val->Integer;

    ReturnValue->Val->Pointer = NULL;
    for (; *Pos != '\0'; Pos++)
    {
        if (*Pos == SearchChar)
            ReturnValue->Val->Pointer = Pos;
    }
}

void LibStrlen(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char* Pos = (char*)Param[0]->Val->Pointer;
    if (Pos == NULL) ProgramFail(Parser, "strlen: string is NULL");
    int Len;

    for (Len = 0; *Pos != '\0'; Pos++)
        Len++;

    ReturnValue->Val->Integer = Len;
}

void LibMemset(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    int Len = Param[2]->Val->Integer;
    if (Len < 0) ProgramFail(Parser, "memset: negative size is invalid");
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "memset: destination is NULL (undefined behavior in C)");
    /* Keep the standard return value consistent with the cstdlib wrapper. */
    ReturnValue->Val->Pointer = memset(Param[0]->Val->Pointer,
        Param[1]->Val->Integer, (size_t)Len);
}

void LibMemcpy(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    void *Dst = Param[0]->Val->Pointer;
    const void *Src = Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;
    if (Len < 0) ProgramFail(Parser, "memcpy: negative size is invalid");
    if (Dst == NULL || Src == NULL)
        ProgramFail(Parser, "memcpy: source or destination is NULL (undefined behavior in C)");
    if (Len > 0 && PicocMemoryRangesOverlap(Dst, Src, (size_t)Len))
        ProgramFail(Parser, DiagnosticMsgMemcpyOverlap);
    ReturnValue->Val->Pointer = memcpy(Dst, Src, (size_t)Len);
}

void LibMemcmp(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    unsigned char* Mem1 = (unsigned char*)Param[0]->Val->Pointer;
    unsigned char* Mem2 = (unsigned char*)Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;
    if (Len < 0) ProgramFail(Parser, "memcmp: negative size is invalid");
    if (Mem1 == NULL || Mem2 == NULL)
        ProgramFail(Parser, "memcmp: source is NULL (undefined behavior in C)");

    for (; Len > 0; Mem1++, Mem2++, Len--)
    {
        if (*Mem1 != *Mem2)
        {
            ReturnValue->Val->Integer = (int)*Mem1 - (int)*Mem2;
            return;
        }
    }

    ReturnValue->Val->Integer = 0;
}
#endif

/* list of all library functions and their prototypes */
struct LibraryFunction CLibrary[] =
{
    { LibPrintf,        "void printf(char *, ...);" },
    { LibSPrintf,       "char *sprintf(char *, char *, ...);" },
    { LibGets,          "char *gets(char *);" },
    { LibGetc,          "int getchar();" },
    { LibExit,          "void exit(int);" },
#ifdef PICOC_LIBRARY
    { LibSin,           "float sin(float);" },
    { LibCos,           "float cos(float);" },
    { LibTan,           "float tan(float);" },
    { LibAsin,          "float asin(float);" },
    { LibAcos,          "float acos(float);" },
    { LibAtan,          "float atan(float);" },
    { LibSinh,          "float sinh(float);" },
    { LibCosh,          "float cosh(float);" },
    { LibTanh,          "float tanh(float);" },
    { LibExp,           "float exp(float);" },
    { LibFabs,          "float fabs(float);" },
    { LibLog,           "float log(float);" },
    { LibLog10,         "float log10(float);" },
    { LibPow,           "float pow(float,float);" },
    { LibSqrt,          "float sqrt(float);" },
    { LibRound,         "float round(float);" },
    { LibCeil,          "float ceil(float);" },
    { LibFloor,         "float floor(float);" },
#endif
    { LibMalloc,        "void *malloc(int);" },
#ifndef NO_CALLOC
    { LibCalloc,        "void *calloc(int,int);" },
#endif
#ifndef NO_REALLOC
    { LibRealloc,       "void *realloc(void *,int);" },
#endif
    { LibFree,          "void free(void *);" },
#ifndef NO_STRING_FUNCTIONS
    { LibStrcpy,        "void strcpy(char *,char *);" },
    { LibStrncpy,       "void strncpy(char *,char *,int);" },
    { LibStrcmp,        "int strcmp(char *,char *);" },
    { LibStrncmp,       "int strncmp(char *,char *,int);" },
    { LibStrcat,        "void strcat(char *,char *);" },
    { LibIndex,         "char *index(char *,int);" },
    { LibRindex,        "char *rindex(char *,int);" },
    { LibStrlen,        "int strlen(char *);" },
    { LibMemset,        "void memset(void *,int,int);" },
    { LibMemcpy,        "void memcpy(void *,void *,int);" },
    { LibMemcmp,        "int memcmp(void *,void *,int);" },
#endif
    { NULL,             NULL }
};

#endif /* BUILTIN_MINI_STDLIB */

/* ------------------------------------------------------------------ */
/* PrintValue – works both with and without BUILTIN_MINI_STDLIB.
 * Placed after the MINI block so that, when MINI is enabled, the
 * Print* helpers are already defined.
 * When MINI is off, IOFILE is FILE* and we use stdio directly.
 * This matches the original design of the project. */
void PrintValue(struct Value *Val, IOFILE *Stream)
{
    if (Val == NULL || Val->Typ == NULL)
    {
#ifdef BUILTIN_MINI_STDLIB
        PrintStr("(null)", Stream);
#else
        fputs("(null)", Stream ? Stream : stdout);
#endif
        return;
    }

#ifdef BUILTIN_MINI_STDLIB
    switch (Val->Typ->Base)
    {
        case TypeVoid:          PrintStr("void", Stream); break;
        case TypeInt:           PrintSimpleInt(Val->Val->Integer, Stream); break;
        case TypeShort:         PrintSimpleInt(Val->Val->ShortInteger, Stream); break;
        case TypeChar:
            if (Val->Val->Character >= 32 && Val->Val->Character < 127) {
                PrintCh('\'', Stream); PrintCh(Val->Val->Character, Stream); PrintCh('\'', Stream);
            } else
                PrintSimpleInt(Val->Val->Character, Stream);
            break;
        case TypeLong:          PrintSimpleInt(Val->Val->LongInteger, Stream); break;
        case TypeUnsignedInt:   PrintUnsigned(Val->Val->UnsignedInteger, 10, 0, FALSE, FALSE, Stream); break;
        case TypeUnsignedShort: PrintUnsigned(Val->Val->UnsignedShortInteger, 10, 0, FALSE, FALSE, Stream); break;
        case TypeUnsignedLong:  PrintUnsigned(Val->Val->UnsignedLongInteger, 10, 0, FALSE, FALSE, Stream); break;
        case TypeUnsignedLongLong: PrintUnsignedLongLong(Val->Val->UnsignedLongLongInteger, 10, 0, FALSE, FALSE, Stream); break;
        case TypeUnsignedChar:  PrintUnsigned(Val->Val->UnsignedCharacter, 10, 0, FALSE, FALSE, Stream); break;
#ifndef NO_FP
        case TypeFP:            PrintFP(Val->Val->FP, Stream); break;
#endif
        case TypePointer:
            if (Val->Val->Pointer == NULL)
                PrintStr("NULL", Stream);
            else if (Val->Typ->FromType != NULL && Val->Typ->FromType->Base == TypeChar) {
                PrintCh('"', Stream); PrintStr((const char *)Val->Val->Pointer, Stream); PrintCh('"', Stream);
            } else {
                PrintStr("0x", Stream);
                PrintUnsigned((unsigned long)(PTR_TYPE_INT)Val->Val->Pointer, 16, 0, FALSE, FALSE, Stream);
            }
            break;
        case TypeArray:         PrintStr("(array)", Stream); break;
        case TypeStruct:
            PrintStr("(struct ", Stream);
            if (Val->Typ->Identifier) PrintStr(Val->Typ->Identifier, Stream);
            PrintCh(')', Stream);
            break;
        case TypeUnion:
            PrintStr("(union ", Stream);
            if (Val->Typ->Identifier) PrintStr(Val->Typ->Identifier, Stream);
            PrintCh(')', Stream);
            break;
        case TypeEnum:          PrintSimpleInt(Val->Val->Integer, Stream); break;
        case TypeFunction:      PrintStr("(function)", Stream); break;
        case TypeMacro:         PrintStr("(macro)", Stream); break;
        case Type_Type:         PrintType(Val->Val->Typ, Stream); break;
        default:                PrintStr("(unknown)", Stream); break;
    }
#else /* !BUILTIN_MINI_STDLIB – system FILE* */
    {
        FILE *out = Stream ? Stream : stdout;
        switch (Val->Typ->Base)
        {
            case TypeVoid:          fputs("void", out); break;
            case TypeInt:           fprintf(out, "%d", Val->Val->Integer); break;
            case TypeShort:         fprintf(out, "%d", (int)Val->Val->ShortInteger); break;
            case TypeChar:
                if (Val->Val->Character >= 32 && Val->Val->Character < 127)
                    fprintf(out, "'%c'", Val->Val->Character);
                else
                    fprintf(out, "%d", (int)Val->Val->Character);
                break;
            case TypeLong:          fprintf(out, "%ld", Val->Val->LongInteger); break;
            case TypeUnsignedInt:   fprintf(out, "%u", Val->Val->UnsignedInteger); break;
            case TypeUnsignedShort: fprintf(out, "%u", (unsigned)Val->Val->UnsignedShortInteger); break;
            case TypeUnsignedLong:  fprintf(out, "%lu", Val->Val->UnsignedLongInteger); break;
            case TypeUnsignedLongLong: fprintf(out, "%llu", (unsigned long long)Val->Val->UnsignedLongLongInteger); break;
            case TypeUnsignedChar:  fprintf(out, "%u", (unsigned)Val->Val->UnsignedCharacter); break;
#ifndef NO_FP
            case TypeFP:            fprintf(out, "%g", Val->Val->FP); break;
#endif
            case TypePointer:
                if (Val->Val->Pointer == NULL)
                    fputs("NULL", out);
                else if (Val->Typ->FromType != NULL && Val->Typ->FromType->Base == TypeChar)
                    fprintf(out, "\"%s\"", (const char *)Val->Val->Pointer);
                else
                    fprintf(out, "0x%llx", (unsigned long long)(PTR_TYPE_INT)Val->Val->Pointer);
                break;
            case TypeArray:         fputs("(array)", out); break;
            case TypeStruct:
                fputs("(struct ", out);
                if (Val->Typ->Identifier) fputs(Val->Typ->Identifier, out);
                fputc(')', out);
                break;
            case TypeUnion:
                fputs("(union ", out);
                if (Val->Typ->Identifier) fputs(Val->Typ->Identifier, out);
                fputc(')', out);
                break;
            case TypeEnum:          fprintf(out, "%d", Val->Val->Integer); break;
            case TypeFunction:      fputs("(function)", out); break;
            case TypeMacro:         fputs("(macro)", out); break;
            case Type_Type:         fputs("(type)", out); break;
            default:                fputs("(unknown)", out); break;
        }
    }
#endif /* BUILTIN_MINI_STDLIB */
}


/* ---- FormatPure magic-box cache (always compiled) ---- */
#define FMT_CACHE_SIZE   128
#define FMT_CACHE_MAXARG  4
#define FMT_CACHE_MAXSTR 128
#define FMT_CACHE_DECAY  4096

typedef struct FmtCacheEntry {
    const char *Fmt;
    int NArgs;
    long long Args[FMT_CACHE_MAXARG];
    char Out[FMT_CACHE_MAXSTR];
    int OutLen;
    unsigned Clock;
    int Occupied;
    int LruPrev, LruNext;
} FmtCacheEntry;

static FmtCacheEntry FmtCache[FMT_CACHE_SIZE];
static int FmtMissStreak;
static int FmtCacheCold;
static int FmtLruHead = -1, FmtLruTail = -1;
static unsigned FmtClock = 1;

static int FmtCacheFormatIsSafe(const char *Fmt)
{
    const char *p;
    if (Fmt == NULL) return 0;
    for (p = Fmt; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == '%') continue;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
        if (*p == 'l' || *p == 'h' || *p == 'z') p++;
        if (*p == 'd' || *p == 'i' || *p == 'u' || *p == 'x' || *p == 'X'
            || *p == 'c' || *p == 'b')
            continue;
        return 0;
    }
    return 1;
}

static void FmtLruUnlink(int i)
{
    FmtCacheEntry *E = &FmtCache[i];
    if (E->LruPrev >= 0) FmtCache[E->LruPrev].LruNext = E->LruNext;
    else FmtLruHead = E->LruNext;
    if (E->LruNext >= 0) FmtCache[E->LruNext].LruPrev = E->LruPrev;
    else FmtLruTail = E->LruPrev;
    E->LruPrev = E->LruNext = -1;
}

static void FmtLruTouch(int i)
{
    FmtCacheEntry *E = &FmtCache[i];
    if (FmtLruHead == i) { E->Clock = FmtClock; return; }
    if (E->LruPrev >= 0 || E->LruNext >= 0 || FmtLruTail == i)
        FmtLruUnlink(i);
    E->LruNext = FmtLruHead;
    if (FmtLruHead >= 0) FmtCache[FmtLruHead].LruPrev = i;
    else FmtLruTail = i;
    FmtLruHead = i;
    E->LruPrev = -1;
    E->Clock = FmtClock;
}

static unsigned FmtCacheHash(const char *Fmt, long long *Args, int N)
{
    unsigned h = 2166136261u;
    const unsigned char *p = (const unsigned char *)Fmt;
    int i;
    while (*p) { h ^= *p++; h *= 16777619u; }
    for (i = 0; i < N; i++) {
        unsigned long long a = (unsigned long long)Args[i];
        h ^= (unsigned)a; h *= 16777619u;
        h ^= (unsigned)(a >> 32); h *= 16777619u;
    }
    return h;
}

int FmtCacheEnabled(void) { return !FmtCacheCold; }

int FmtCacheLookup(const char *Fmt, long long *Args, int N, char *Dst, int DstCap)
{
    unsigned h;
    int probes, i;
    if (FmtCacheCold)
        return 0;
    if (Fmt == NULL || Dst == NULL || DstCap < 2 || N < 0 || N > FMT_CACHE_MAXARG)
        return 0;
    if (!FmtCacheFormatIsSafe(Fmt)) return 0;
    FmtClock++;
    h = FmtCacheHash(Fmt, Args, N) % FMT_CACHE_SIZE;
    for (probes = 0, i = (int)h; probes < 12; probes++, i = (i + 1) % FMT_CACHE_SIZE) {
        FmtCacheEntry *E = &FmtCache[i];
        if (!E->Occupied) return 0;
        if (E->NArgs == N
            && (E->Fmt == Fmt || (E->Fmt != NULL && Fmt != NULL && strcmp(E->Fmt, Fmt) == 0))
            && (N == 0 || memcmp(E->Args, Args, (size_t)N * sizeof(long long)) == 0)) {
            if (FmtClock - E->Clock > FMT_CACHE_DECAY) {
                FmtLruUnlink(i);
                E->Occupied = 0;
                return 0;
            }
            if (E->OutLen + 1 > DstCap) return 0;
            memcpy(Dst, E->Out, (size_t)E->OutLen + 1);
            FmtLruTouch(i);
            return 1;
        }
    }
    return 0;
}

void FmtCacheStore(const char *Fmt, long long *Args, int N, const char *Out)
{
    unsigned h;
    int probes, i, slot = -1, len;
    if (Fmt == NULL || Out == NULL || N < 0 || N > FMT_CACHE_MAXARG) return;
    if (!FmtCacheFormatIsSafe(Fmt)) return;
    len = (int)strlen(Out);
    if (len <= 0 || len >= FMT_CACHE_MAXSTR) return;
    h = FmtCacheHash(Fmt, Args, N) % FMT_CACHE_SIZE;
    for (probes = 0, i = (int)h; probes < 12; probes++, i = (i + 1) % FMT_CACHE_SIZE) {
        FmtCacheEntry *E = &FmtCache[i];
        if (!E->Occupied) { slot = i; break; }
        if (E->Fmt == Fmt && E->NArgs == N
            && memcmp(E->Args, Args, (size_t)N * sizeof(long long)) == 0) {
            memcpy(E->Out, Out, (size_t)len + 1);
            E->OutLen = len;
            FmtLruTouch(i);
            return;
        }
        if (slot < 0 && FmtClock - E->Clock > FMT_CACHE_DECAY) slot = i;
    }
    if (slot < 0) {
        slot = FmtLruTail;
        if (slot < 0) slot = (int)h;
        else FmtLruUnlink(slot);
    } else if (FmtCache[slot].Occupied)
        FmtLruUnlink(slot);
    {
        FmtCacheEntry *E = &FmtCache[slot];
        E->Fmt = Fmt;
        E->NArgs = N;
        if (N > 0) memcpy(E->Args, Args, (size_t)N * sizeof(long long));
        memcpy(E->Out, Out, (size_t)len + 1);
        E->OutLen = len;
        E->Occupied = 1;
        E->LruPrev = E->LruNext = -1;
        FmtLruTouch(slot);
    }
}

/* FirstDataArg: 2 for sprintf(buf,fmt,args...), 3 for snprintf(buf,n,fmt,args...) */
int FmtCacheCollectIntArgsEx(struct Value **Param, int NumArgs, int FirstDataArg,
                             long long *Out, int MaxOut)
{
    int i, n = 0;
    if (FirstDataArg < 0)
        return -1;
    for (i = FirstDataArg; i < NumArgs && n < MaxOut; i++) {
        struct Value *V = Param[i];
        if (V == NULL || V->Typ == NULL) return -1;
        switch (V->Typ->Base) {
            case TypeInt: case TypeShort: case TypeChar:
            case TypeLong: case TypeLongLong:
            case TypeUnsignedInt: case TypeUnsignedShort:
            case TypeUnsignedLong: case TypeUnsignedChar:
                Out[n++] = ExpressionCoerceInteger(V);
                break;
            default:
                return -1;
        }
    }
    return n;
}

int FmtCacheCollectIntArgs(struct Value **Param, int NumArgs, long long *Out, int MaxOut)
{
    return FmtCacheCollectIntArgsEx(Param, NumArgs, 2, Out, MaxOut);
}
