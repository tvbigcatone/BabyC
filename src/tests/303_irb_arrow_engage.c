#include <stdio.h>
/* Phase B: p->field (int field, one level) must engage Irb */
struct counter { int m; };
void main()
{
    struct counter c;
    struct counter *p;
    int i;
    c.m = 0;
    p = &c;
    for (i = 0; i < 1000; i++)
        p->m = p->m + 1;
    printf("m %d\n", p->m);
}
