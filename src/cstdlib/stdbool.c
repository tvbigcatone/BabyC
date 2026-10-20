/*  */
#include <stdbool.h>

#include "../interpreter.h"


static int trueValue = 1;
static int falseValue = 0;


/* structure definitions */
const char StdboolDefs[] = "typedef int bool;";

/* creates various system-dependent definitions */
void StdboolSetupFunc(Picoc *pc)
{
    /* defines */
    VariableDefinePlatformVar(pc, NULL, "TRUE", &pc->IntType,
    	(union AnyValue*)&trueValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "FALSE", &pc->IntType,
    	(union AnyValue*)&falseValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "__bool_true_false_are_defined",
    	&pc->IntType, (union AnyValue*)&trueValue, FALSE);
}
