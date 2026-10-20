#include <stdio.h>
int g;
int step(int x)
{
    g = g * 10 + x;
    return g;
}
int wrapper(int x)
{
    return step(x) * 10 + 1;
}
int main(void)
{
    g = 0;
    printf("%d ", wrapper(2));
    printf("%d ", step(3));
    printf("%d\n", wrapper(4));
    return 0;
}
