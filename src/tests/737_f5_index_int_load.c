#include <stdio.h>
/* F5 residual: int[] Index load lowers under BABYC_LOWER; both soft-match. */
int main(void)
{
    int a[8];
    int i, s;
    for (i = 0; i < 8; i++)
        a[i] = i * 3 + 1;
    s = a[0] + a[3] + a[7];
    printf("%d\n", s);
    printf("%d\n", a[2] * a[4]);
    return 0;
}
