#include <stdio.h>

int neg(int x) { return -x; }
int dbl(int x) { return x + x; }

int main()
{
    int (*fp)(int);
    fp = &neg;
    printf("a %d\n", (*fp)(3));
    fp = &dbl;
    printf("b %d\n", (*fp)(3));
    printf("ok\n");
    return 0;
}
