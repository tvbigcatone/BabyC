/* Caller local must not leak into callee */
#include <stdio.h>
int inner(int n)
{
    int t;
    t = n * 2;
    return t;
}
int outer(void)
{
    int t;
    t = 5;
    return inner(3) + t;
}
int main()
{
    printf("%d\n", outer());
    return 0;
}
