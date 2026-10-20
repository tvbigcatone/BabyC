#include <stdio.h>
struct S { int a[3]; int n; };
int main()
{
    struct S s;
    s.a[0] = 1;
    s.n = 3;
    printf("%d %d\n", s.a[0], s.n);
    return 0;
}
