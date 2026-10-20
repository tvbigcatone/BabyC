/* struct N a, *p, *q; mixed value + pointers on one line */
#include <stdio.h>
struct N { int v; };
int main(void)
{
    struct N a, *p, *q;
    a.v = 7;
    p = &a;
    q = p;
    printf("%d\n", q->v);
    return 0;
}
