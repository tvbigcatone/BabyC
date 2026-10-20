#include <stdio.h>
/* F5 residual: short[] Index store + load-after-store on local array. */
int main(void)
{
    short s[5];
    s[0] = 10;
    s[1] = 20;
    s[2] = 30;
    s[3] = s[0] + s[1];
    s[4] = s[3] - s[2];
    printf("%d %d\n", s[3], s[4]);
    return 0;
}
