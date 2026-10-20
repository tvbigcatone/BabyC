#include <stdio.h>
struct S { int a[3]; int n; };
int main()
{
    struct S s;
    s.a[0] = 1;
    s.n = 3;
    printf("%d\n", s.a[2]);
    return 0;
}
