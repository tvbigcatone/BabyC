#include <stdio.h>
/* Row decay to pointer (& or pass) is alias → permissive for that row's bytes. */
int main()
{
    int m[2][3];
    int *p = m[0];
    p[2] = 7;
    printf("%d\n", m[0][2]);
    return 0;
}
