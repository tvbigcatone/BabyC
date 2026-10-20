#include <stdio.h>
/* Anti-regression: a[i]++ / o[0]++ must not hang (LValueFrom cycle) */
int o[4];
int a[3];
int main()
{
    int i;
    o[0] = 0;
    o[0]++;
    o[0]++;
    a[1] = 10;
    a[1]++;
    for (i = 0; i < 3; i++)
        o[0]++;
    printf("%d %d\n", o[0], a[1]);
    return 0;
}
