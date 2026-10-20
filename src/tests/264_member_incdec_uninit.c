#include <stdio.h>
struct S { int n; int m; };
int main()
{
    struct S s;
    s.n = 1;
    s.n++;
    printf("%d\n", s.n);
    s.m++;
    return 0;
}
