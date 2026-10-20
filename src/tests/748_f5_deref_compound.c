#include <stdio.h>
int main(void)
{
    int x;
    int *p;
    x = 10;
    p = &x;
    *p += 5;
    *p *= 2;
    *p -= 4;
    printf("%d\n", x);
    return 0;
}
