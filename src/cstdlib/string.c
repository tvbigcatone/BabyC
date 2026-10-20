/*  */
#include <string.h>

#include "../interpreter.h"

static void StringRequirePointer(struct ParseState *Parser, const void *Ptr,
                                 const char *Function, const char *Argument)
{
    if (Ptr == NULL)
        ProgramFail(Parser, "%s: %s pointer is NULL (undefined behavior in C)",
                    Function, Argument);
}

static int StringRequireNonnegativeLength(struct ParseState *Parser, int Len,
                                          const char *Function)
{
    if (Len < 0)
        ProgramFail(Parser, "%s: negative size is invalid (undefined behavior in C)", Function);
    return Len;
}


static int String_ZeroValue = 0;

void StringStrcpy(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strcpy", "destination");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strcpy", "source");
    {
        size_t CopyBytes = strlen((const char *)Param[1]->Val->Pointer) + 1;
        if (PicocMemoryRangesOverlap(Param[0]->Val->Pointer,
                                     Param[1]->Val->Pointer, CopyBytes))
            ProgramFail(Parser, "strcpy source and destination overlap (undefined behavior in C)");
    }
    ReturnValue->Val->Pointer = strcpy(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrncpy(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strncpy", "destination");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strncpy", "source");
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "strncpy");
    if (Param[2]->Val->Integer > 0) {
        size_t CopyBytes = (size_t)Param[2]->Val->Integer;
        if (PicocMemoryRangesOverlap(Param[0]->Val->Pointer,
                                     Param[1]->Val->Pointer, CopyBytes))
            ProgramFail(Parser, "strncpy source and destination overlap (undefined behavior in C)");
    }
    ReturnValue->Val->Pointer = strncpy(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, (size_t)Param[2]->Val->Integer);
}

void StringStrcmp(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    /* Portable: return actual unsigned-char difference (e.g. 'o'-'O' == 32).
     * Host MSVC UCRT strcmp often yields only -1/0/1; glibc yields the delta.
     * Course samples (007_strcmp_test) expect the delta. ISO C only requires
     * sign of the result; returning the delta is also conforming. */
    const unsigned char *s1 = (const unsigned char *)Param[0]->Val->Pointer;
    const unsigned char *s2 = (const unsigned char *)Param[1]->Val->Pointer;
    StringRequirePointer(Parser, s1, "strcmp", "first argument");
    StringRequirePointer(Parser, s2, "strcmp", "second argument");
    while (*s1 != '\0' && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    ReturnValue->Val->Integer = (int)*s1 - (int)*s2;
}

void StringStrncmp(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    const unsigned char *s1 = (const unsigned char *)Param[0]->Val->Pointer;
    const unsigned char *s2 = (const unsigned char *)Param[1]->Val->Pointer;
    int n = Param[2]->Val->Integer;
    StringRequirePointer(Parser, s1, "strncmp", "first argument");
    StringRequirePointer(Parser, s2, "strncmp", "second argument");
    StringRequireNonnegativeLength(Parser, n, "strncmp");
    if (n == 0)
    {
        ReturnValue->Val->Integer = 0;
        return;
    }
    while (n > 0 && *s1 != '\0' && *s1 == *s2)
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        ReturnValue->Val->Integer = 0;
    else
        ReturnValue->Val->Integer = (int)*s1 - (int)*s2;
}

void StringStrcat(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strcat", "destination");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strcat", "source");
    {
        const char *Dst = (const char *)Param[0]->Val->Pointer;
        const char *Src = (const char *)Param[1]->Val->Pointer;
        size_t DstLen = strlen(Dst), SrcLen = strlen(Src);
        if (DstLen > (size_t)-1 - SrcLen - 1)
            ProgramFail(Parser, "strcat: resulting string size overflows size_t");
        if (PicocMemoryRangesOverlap(Dst, Src, DstLen + SrcLen + 1) ||
            PicocMemoryRangesOverlap(Src, Dst, SrcLen + 1))
            ProgramFail(Parser, "strcat source and destination overlap (undefined behavior in C)");
    }
    ReturnValue->Val->Pointer = strcat(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrncat(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strncat", "destination");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strncat", "source");
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "strncat");
    {
        const char *Dst = (const char *)Param[0]->Val->Pointer;
        const char *Src = (const char *)Param[1]->Val->Pointer;
        size_t DstLen = strlen(Dst), SrcLen = strlen(Src);
        size_t Take = SrcLen < (size_t)Param[2]->Val->Integer ? SrcLen : (size_t)Param[2]->Val->Integer;
        if (DstLen > (size_t)-1 - Take - 1)
            ProgramFail(Parser, "strncat: resulting string size overflows size_t");
        if (Take > 0 && (PicocMemoryRangesOverlap(Dst, Src, DstLen + Take + 1) ||
            PicocMemoryRangesOverlap(Src, Dst, Take + 1)))
            ProgramFail(Parser, "strncat source and destination overlap (undefined behavior in C)");
    }
    ReturnValue->Val->Pointer = strncat(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, (size_t)Param[2]->Val->Integer);
}

#ifndef WIN32
void StringIndex(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "index", "string");
    ReturnValue->Val->Pointer = index(Param[0]->Val->Pointer,
        Param[1]->Val->Integer);
}

void StringRindex(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "rindex", "string");
    ReturnValue->Val->Pointer = rindex(Param[0]->Val->Pointer,
        Param[1]->Val->Integer);
}
#endif

void StringStrlen(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strlen", "string");
    ReturnValue->Val->Integer = strlen(Param[0]->Val->Pointer);
}

void StringMemset(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "memset");
    if (Param[2]->Val->Integer > 0) StringRequirePointer(Parser, Param[0]->Val->Pointer, "memset", "destination");
    ReturnValue->Val->Pointer = memset(Param[0]->Val->Pointer,
        Param[1]->Val->Integer, (size_t)Param[2]->Val->Integer);
}

void StringMemcpy(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    void *Dst = Param[0]->Val->Pointer;
    const void *Src = Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;
    StringRequireNonnegativeLength(Parser, Len, "memcpy");
    if (Len > 0 && (Dst == NULL || Src == NULL))
        ProgramFail(Parser, "memcpy: source or destination is NULL (undefined behavior in C)");
    if (Len > 0 && PicocMemoryRangesOverlap(Dst, Src, (size_t)Len))
        ProgramFail(Parser, DiagnosticMsgMemcpyOverlap);
    ReturnValue->Val->Pointer = memcpy(Dst, Src, (size_t)Len);
}

void StringMemcmp(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "memcmp");
    if (Param[2]->Val->Integer > 0) {
        StringRequirePointer(Parser, Param[0]->Val->Pointer, "memcmp", "first argument");
        StringRequirePointer(Parser, Param[1]->Val->Pointer, "memcmp", "second argument");
    }
    ReturnValue->Val->Integer = memcmp(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, (size_t)Param[2]->Val->Integer);
}

void StringMemmove(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "memmove");
    if (Param[2]->Val->Integer > 0) {
        StringRequirePointer(Parser, Param[0]->Val->Pointer, "memmove", "destination");
        StringRequirePointer(Parser, Param[1]->Val->Pointer, "memmove", "source");
    }
    ReturnValue->Val->Pointer = memmove(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, (size_t)Param[2]->Val->Integer);
}

void StringMemchr(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "memchr");
    if (Param[2]->Val->Integer > 0) StringRequirePointer(Parser, Param[0]->Val->Pointer, "memchr", "memory");
    ReturnValue->Val->Pointer = memchr(Param[0]->Val->Pointer,
        Param[1]->Val->Integer, (size_t)Param[2]->Val->Integer);
}

void StringStrchr(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strchr", "string");
    ReturnValue->Val->Pointer = strchr(Param[0]->Val->Pointer,
        Param[1]->Val->Integer);
}

void StringStrrchr(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strrchr", "string");
    ReturnValue->Val->Pointer = strrchr(Param[0]->Val->Pointer,
        Param[1]->Val->Integer);
}

void StringStrcoll(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strcoll", "first argument");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strcoll", "second argument");
    ReturnValue->Val->Integer = strcoll(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrerror(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strerror(Param[0]->Val->Integer);
}

void StringStrspn(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strspn", "first argument");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strspn", "second argument");
    ReturnValue->Val->Integer = strspn(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrcspn(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strcspn", "first argument");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strcspn", "second argument");
    ReturnValue->Val->Integer = strcspn(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrpbrk(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strpbrk", "string");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strpbrk", "accept set");
    ReturnValue->Val->Pointer = strpbrk(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrstr(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strstr", "haystack");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strstr", "needle");
    ReturnValue->Val->Pointer = strstr(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrtok(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    /* strtok(NULL, delim) is valid continuation syntax. */
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strtok", "delimiters");
    ReturnValue->Val->Pointer = strtok(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrxfrm(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strxfrm", "destination");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strxfrm", "source");
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "strxfrm");
    ReturnValue->Val->Integer = (int)strxfrm(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, (size_t)Param[2]->Val->Integer);
}

#ifndef WIN32
void StringStrdup(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strdup", "string");
    ReturnValue->Val->Pointer = (void*)strdup(Param[0]->Val->Pointer);
    if (ReturnValue->Val->Pointer != NULL)
        BabycHeapTrackAllocation(ReturnValue->Val->Pointer,
            strlen((const char *)ReturnValue->Val->Pointer) + 1);
}

void StringStrtok_r(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    /* str may be NULL on continuation; delimiters and saveptr may not. */
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strtok_r", "delimiters");
    StringRequirePointer(Parser, Param[2]->Val->Pointer, "strtok_r", "save pointer");
    ReturnValue->Val->Pointer = (void*)strtok_r(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}
#endif

void StringStrcasecmp(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strcasecmp", "first argument");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strcasecmp", "second argument");
    ReturnValue->Val->Integer = strcasecmp(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer);
}

void StringStrncasecmp(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    StringRequirePointer(Parser, Param[0]->Val->Pointer, "strncasecmp", "first argument");
    StringRequirePointer(Parser, Param[1]->Val->Pointer, "strncasecmp", "second argument");
    StringRequireNonnegativeLength(Parser, Param[2]->Val->Integer, "strncasecmp");
    ReturnValue->Val->Integer = strncasecmp(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, (size_t)Param[2]->Val->Integer);
}

/* all string.h functions */
struct LibraryFunction StringFunctions[] =
{
#ifndef WIN32
	{StringIndex,   "char *index(char *,int);"},
    {StringRindex,  "char *rindex(char *,int);"},
#endif
    {StringMemcpy,  "void *memcpy(void *,void *,int);"},
    {StringMemmove, "void *memmove(void *,void *,int);"},
    {StringMemchr,  "void *memchr(char *,int,int);"},
    {StringMemcmp,  "int memcmp(void *,void *,int);"},
    {StringMemset,  "void *memset(void *,int,int);"},
    {StringStrcat,  "char *strcat(char *,char *);"},
    {StringStrncat, "char *strncat(char *,char *,int);"},
    {StringStrchr,  "char *strchr(char *,int);"},
    {StringStrrchr, "char *strrchr(char *,int);"},
    {StringStrcmp,  "int strcmp(char *,char *);"},
    {StringStrncmp, "int strncmp(char *,char *,int);"},
    {StringStrcoll, "int strcoll(char *,char *);"},
    {StringStrcpy,  "char *strcpy(char *,char *);"},
    {StringStrncpy, "char *strncpy(char *,char *,int);"},
    {StringStrerror,"char *strerror(int);"},
    {StringStrlen,  "int strlen(char *);"},
    {StringStrspn,  "int strspn(char *,char *);"},
    {StringStrcspn, "int strcspn(char *,char *);"},
    {StringStrpbrk, "char *strpbrk(char *,char *);"},
    {StringStrstr,  "char *strstr(char *,char *);"},
    {StringStrtok,  "char *strtok(char *,char *);"},
    {StringStrxfrm, "int strxfrm(char *,char *,int);"},
    {StringStrcasecmp,  "int strcasecmp(char *,char *);"},
    {StringStrncasecmp, "int strncasecmp(char *,char *,int);"},
#ifndef WIN32
	{StringStrdup,  "char *strdup(char *);"},
    {StringStrtok_r,"char *strtok_r(char *,char *,char **);"},
#endif
    {NULL,          NULL }
};

/* creates various system-dependent definitions */
void StringSetupFunc(Picoc *pc)
{
    /* define NULL */
    if (!VariableDefined(pc, TableStrRegister(pc, "NULL")))
        VariableDefinePlatformVar(pc, NULL, "NULL", &pc->IntType,
            (union AnyValue*)&String_ZeroValue, FALSE);
}

