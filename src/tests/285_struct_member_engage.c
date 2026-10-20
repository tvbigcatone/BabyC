#include <stdio.h>

/* Phase A positive case: a flat struct member access (ident.field, int
 * field, no pointer/nesting) must engage Irb, not just fall back to the
 * token / Loop Body IR path like 268 did before this change. */

struct counter {
    int m;
};

void main()
{
    struct counter c;
    int i;

    c.m = 0;

    for (i = 0; i < 1000; i++)
        c.m = c.m + 1;

    printf("m %d\n", c.m);
}
