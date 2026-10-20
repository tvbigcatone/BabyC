#include <stdio.h>
/* F5 residual: struct member load on local Ident (safe offset). */
struct S { int a; int b; char c; };
int main(void)
{
    struct S s;
    s.a = 10;
    s.b = 20;
    s.c = 3;
    printf("%d %d %d\n", s.a, s.b, s.c);
    printf("%d\n", s.a + s.b + s.c);
    return 0;
}
