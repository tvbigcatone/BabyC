/* P2 shadow: p->m compound and ++ (pointer-to-struct local). */
#include <stdio.h>
struct S { int a; int b; };
int main(void)
{
    struct S s;
    struct S *p;
    s.a = 10;
    s.b = 3;
    p = &s;
    p->a += 5;
    p->b *= 2;
    p->a++;
    ++p->b;
    printf("%d %d\n", s.a, s.b);
    return 0;
}
