#include <stdio.h>
/* F5 residual: char[] Index store + load-after-store on local array. */
int main(void)
{
    char c[6];
    c[0] = 3;
    c[1] = 5;
    c[2] = 7;
    c[3] = c[0] + c[1] + c[2];
    c[4] = 1;
    c[5] = c[3] - c[4];
    printf("%d %d %d\n", c[3], c[5], c[2]);
    return 0;
}
