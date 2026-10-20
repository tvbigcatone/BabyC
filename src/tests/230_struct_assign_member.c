#include <stdio.h>
struct S { int a; int b; };
int main()
{
    struct S s = { 1 };
    s.b = 9;
    printf("%d %d\n", s.a, s.b);
    return 0;
}
