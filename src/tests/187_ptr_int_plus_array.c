/* 3 + a is valid C (int + array decays) */
#include <stdio.h>
int main()
{
    int a[5];
    int *p;
    int i;
    for (i = 0; i < 5; i = i + 1)
        a[i] = i;
    p = 3 + a;
    printf("%d\n", *p);
    printf("%d\n", (int)(p - a));
    return 0;
}
