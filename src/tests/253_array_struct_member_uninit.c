#include <stdio.h>
struct S { int a; int b; };
int main()
{
    struct S v[3];
    v[1].a = 4;
    printf("%d\n", v[1].b);
    return 0;
}
