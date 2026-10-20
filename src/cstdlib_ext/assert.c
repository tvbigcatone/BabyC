/* assert.h — runtime assertion for BabyC / PicoC
 *
 * assert(condition) is a library function (condition is an int expression).
 * If NDEBUG is #define'd before #include <assert.h>, assert becomes a no-op.
 */
#include "../interpreter.h"

static void AssertCheck(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    (void)ReturnValue;
    (void)NumArgs;
    if (Param == NULL || Param[0] == NULL || !Param[0]->Val->Integer)
        ProgramFail(Parser, "assertion failed");
}

static void AssertNoop(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    (void)Parser;
    (void)ReturnValue;
    (void)Param;
    (void)NumArgs;
}

struct LibraryFunction AssertFunctions[] =
{
    { AssertCheck, "void assert(int);" },
    { NULL, NULL }
};

const char AssertDefs[] = "";

void AssertSetupFunc(Picoc *pc)
{
    /* Keys in GlobalTable are interned — must use TableStrRegister for lookup. */
    char *Key = TableStrRegister(pc, "NDEBUG");
    if (VariableDefined(pc, Key))
        AssertFunctions[0].Func = AssertNoop;
    else
        AssertFunctions[0].Func = AssertCheck;
}
