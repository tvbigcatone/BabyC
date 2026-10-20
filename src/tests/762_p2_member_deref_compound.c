/* P2 shadow: compound and ++/-- on member and *p (BABYC_EXEC=both). */
#include <stdio.h>
struct S { int a; int b; };
int main(void)
{
    struct S s;
    int x;
    int *p;
    s.a = 10;
    s.b = 3;
    s.a += 5;
    s.b *= 2;
    s.a++;
    ++s.b;
    x = 7;
    p = &x;
    *p += 4;
    (*p)++;
    printf("%d %d %d\n", s.a, s.b, x);
    return 0;
}
