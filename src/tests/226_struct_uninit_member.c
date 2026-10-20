#include <stdio.h>
struct S { int a; int b; };
int main()
{
    struct S s;
    printf("%d\n", s.a);
    return 0;
}
