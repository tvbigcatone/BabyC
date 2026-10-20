#include <stdio.h>
int f(int x)
{
    return !x;
}
int main()
{
    printf("%d %d %d\n", f(0), f(3), !!5);
    return 0;
}
