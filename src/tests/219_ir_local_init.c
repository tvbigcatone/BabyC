#include <stdio.h>
int g(void)
{
    int x = 10;
    int y = x + 5;
    return y;
}
int main()
{
    printf("%d\n", g());
    return 0;
}
