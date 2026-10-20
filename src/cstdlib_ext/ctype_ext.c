/* ctype.h package marker for WITH_CSTDLIB_EXT (desktop).
 * Core isalpha/isdigit/... live in cstdlib/ctype.c and are always
 * registered as "ctype.h" from IncludeInit on non-mini builds.
 * This unit exists so desktop links an explicit cstdlib_ext ctype
 * object; micro builds omit WITH_CSTDLIB_EXT and this file. */
#include "../interpreter.h"

/* No extra symbols — avoids clash with cstdlib/ctype.c isblank. */
struct LibraryFunction CtypeExtFunctions[] =
{
    {NULL, NULL}
};

const char CtypeExtDefs[] = "";

void CtypeExtSetupFunc(Picoc *pc)
{
    (void)pc;
}
