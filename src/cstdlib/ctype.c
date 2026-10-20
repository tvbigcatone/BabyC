/*  */
#include <ctype.h>
#include <limits.h>

#include "../interpreter.h"

/* ISO C ctype functions accept only EOF or values representable as unsigned char.
 * Validate before reaching the host CRT: passing a negative signed char is a
 * common student bug and can otherwise become host-dependent undefined behavior. */
static int StdCtypeRequireArgument(struct ParseState *Parser, int Ch, const char *Name)
{
    if (Ch != EOF && (Ch < 0 || (unsigned int)Ch > (unsigned int)UCHAR_MAX))
        ProgramFail(Parser, "%s: argument must be EOF or an unsigned-char value (0..UCHAR_MAX)",
            Name);
    return Ch;
}


void StdIsalnum(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isalnum");
    ReturnValue->Val->Integer = isalnum(Param[0]->Val->Integer);
}

void StdIsalpha(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isalpha");
    ReturnValue->Val->Integer = isalpha(Param[0]->Val->Integer);
}

void StdIsblank(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isblank");
    int ch = Param[0]->Val->Integer;
    ReturnValue->Val->Integer = (ch == ' ') | (ch == '\t');
}

void StdIscntrl(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "iscntrl");
    ReturnValue->Val->Integer = iscntrl(Param[0]->Val->Integer);
}

void StdIsdigit(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isdigit");
    ReturnValue->Val->Integer = isdigit(Param[0]->Val->Integer);
}

void StdIsgraph(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isgraph");
    ReturnValue->Val->Integer = isgraph(Param[0]->Val->Integer);
}

void StdIslower(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "islower");
    ReturnValue->Val->Integer = islower(Param[0]->Val->Integer);
}

void StdIsprint(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isprint");
    ReturnValue->Val->Integer = isprint(Param[0]->Val->Integer);
}

void StdIspunct(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "ispunct");
    ReturnValue->Val->Integer = ispunct(Param[0]->Val->Integer);
}

void StdIsspace(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isspace");
    ReturnValue->Val->Integer = isspace(Param[0]->Val->Integer);
}

void StdIsupper(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isupper");
    ReturnValue->Val->Integer = isupper(Param[0]->Val->Integer);
}

void StdIsxdigit(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "isxdigit");
    ReturnValue->Val->Integer = isxdigit(Param[0]->Val->Integer);
}

void StdTolower(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "tolower");
    ReturnValue->Val->Integer = tolower(Param[0]->Val->Integer);
}

void StdToupper(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    Param[0]->Val->Integer = StdCtypeRequireArgument(Parser, Param[0]->Val->Integer, "toupper");
    ReturnValue->Val->Integer = toupper(Param[0]->Val->Integer);
}

void StdIsascii(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = isascii(Param[0]->Val->Integer);
}

void StdToascii(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = toascii(Param[0]->Val->Integer);
}

/* all string.h functions */
struct LibraryFunction StdCtypeFunctions[] =
{
    {StdIsalnum, "int isalnum(int);"},
    {StdIsalpha, "int isalpha(int);"},
    {StdIsblank, "int isblank(int);"},
    {StdIscntrl, "int iscntrl(int);"},
    {StdIsdigit, "int isdigit(int);"},
    {StdIsgraph, "int isgraph(int);"},
    {StdIslower, "int islower(int);"},
    {StdIsprint, "int isprint(int);"},
    {StdIspunct, "int ispunct(int);"},
    {StdIsspace, "int isspace(int);"},
    {StdIsupper, "int isupper(int);"},
    {StdIsxdigit, "int isxdigit(int);"},
    {StdTolower, "int tolower(int);"},
    {StdToupper, "int toupper(int);"},
    {StdIsascii, "int isascii(int);"},
    {StdToascii, "int toascii(int);"},
    {NULL, NULL }
};

