#include <stdio.h>
struct S { int a; int b; };
int main()
{
    struct S s;
    s.a = 1;
    printf("%d\n", s.b);
    return 0;
}
