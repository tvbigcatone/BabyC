#include <stdio.h>
struct S { int a; int b; };
int main(void)
{
    struct S s;
    s.a = 7;
    s.b = 11;
    s.a = s.a + 3;
    printf("%d %d\n", s.a, s.b);
    return 0;
}
