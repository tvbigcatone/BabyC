#include <stdio.h>

/* Anti-regression: IrbCompilePrimary has no case for TokenArrow, so
 * p->field inside a loop body is never Irb-compilable and must fall
 * back to the token / Loop Body IR path with correct results. */

struct box {
    int x;
};

void main()
{
    struct box b;
    struct box *p;
    int i;

    b.x = 0;
    p = &b;

    for (i = 0; i < 5; i++)
        p->x = p->x + 1;

    printf("x %d\n", p->x);
}
