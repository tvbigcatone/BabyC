/* B.2: pointee size + type on LocalSlot — no TableGet needed for T* locals.
 * Multi-decl struct N *p,*q; member via p->n / q->n; array-of-ptr.
 * Run under BABYC_IR_SHADOW=0 when verifying pure frame path.
 */
#include <stdio.h>
struct N { int n; int m; };
int main(void)
{
    struct N a, b;
    struct N *p, *q;
    struct N *tab[2];
    a.n = 10; a.m = 20;
    b.n = 30; b.m = 40;
    p = &a; q = &b;
    tab[0] = &a; tab[1] = &b;
    printf("%d %d\n", p->n, q->m);
    printf("%d %d\n", tab[0]->m, tab[1]->n);
    p->n = 11;
    printf("%d\n", a.n);
    return 0;
}
