#include <stdio.h>
/* F5: int[] Index store on local array. */
int main(void)
{
    int a[6];
    int i;
    for (i = 0; i < 6; i++)
        a[i] = i;
    a[1] = 40;
    a[3] = a[1] + 5;
    a[5] = a[3] - a[0];
    printf("%d %d %d %d\n", a[1], a[3], a[5], a[2]);
    return 0;
}
