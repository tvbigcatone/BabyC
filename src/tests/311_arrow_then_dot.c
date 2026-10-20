#include <stdio.h>
struct inner { int v; };
struct outer { struct inner in; };
void main()
{
    struct outer o;
    struct outer *p;
    int i;
    o.in.v = 0;
    p = &o;
    for (i = 0; i < 7; i++)
        p->in.v = p->in.v + 3;
    printf("v %d\n", p->in.v);
}
