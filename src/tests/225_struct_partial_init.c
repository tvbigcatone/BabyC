#include <stdio.h>
struct S { int a; int b; int c; };
int main()
{
    struct S x = { 1 };
    struct S y = { 2, 3 };
    printf("%d %d %d\n", x.a, x.b, x.c);
    printf("%d %d %d\n", y.a, y.b, y.c);
    return 0;
}
