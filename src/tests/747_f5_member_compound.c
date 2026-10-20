#include <stdio.h>
struct S { int a; int b; };
int main(void)
{
    struct S s;
    s.a = 5;
    s.b = 10;
    s.a += 3;
    s.b -= 2;
    s.a *= 2;
    printf("%d %d\n", s.a, s.b);
    return 0;
}
