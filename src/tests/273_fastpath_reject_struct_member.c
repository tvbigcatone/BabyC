#include <stdio.h>

/* Anti-regression: IrbCompilePrimary only handles bare identifiers and
 * ident[expr]; a '.' after an identifier is left unconsumed, so struct
 * member access inside a loop body is never Irb-compilable and must
 * fall back to the token / Loop Body IR path with correct results. */

struct counter {
    int m;
    int pad;
};

void main()
{
    struct counter c;
    int i;

    c.m = 0;
    c.pad = 99;

    for (i = 0; i < 45; i++)
        c.m = c.m + 1;

    printf("m %d pad %d\n", c.m, c.pad);
}
