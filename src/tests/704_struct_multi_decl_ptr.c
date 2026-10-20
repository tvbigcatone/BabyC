/* struct N *p,*q; second declarator must keep pointee type for q->v */
#include <stdio.h>
struct N { int v; struct N *nx; };
int main(void)
{
    struct N a;
    struct N *p, *q;
    a.v = 5;
    p = &a;
    q = p;
    printf("%d\n", q->v);
    return 0;
}
