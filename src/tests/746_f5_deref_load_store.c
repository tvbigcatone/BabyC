#include <stdio.h>
int main(void)
{
    int x, y;
    int *p;
    x = 0;
    y = 0;
    p = &x;
    *p = 42;
    y = *p;
    *p = y + 1;
    printf("%d %d\n", x, y);
    return 0;
}
