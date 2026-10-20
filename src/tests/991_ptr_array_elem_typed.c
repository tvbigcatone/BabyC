#include <stdio.h>
#include <stdlib.h>
/* T *a[N]: a[i][j] must use the declared pointee T. The tree assumed char
 * (1-byte stride) for any pointer element, so ptrs[1][2] = 55 wrote one
 * byte at offset 2; ptrs[i] + 9 was scaled by 1. Linear refused a[i][j]. */
int *gp[3];
int main(void)
{
    int *ip[4];
    short *sp[2];
    double *dp[2];
    unsigned char *up[2];
    int i, j, s = 0;
    for (i = 0; i < 4; i++)
        ip[i] = (int *)calloc(10, sizeof(int));
    for (i = 0; i < 4; i++)
    {
        ip[i][0] = i + 1;
        ip[i][9] = (i + 1) * 100;
    }
    for (i = 0; i < 4; i++)
        s += ip[i][0] + ip[i][9] + *(ip[i] + 9);
    printf("%d %d %d\n", s, *(ip[2] + 9), (int)((ip[3] + 9) - ip[3]));
    for (i = 0; i < 2; i++)
    {
        sp[i] = (short *)calloc(4, sizeof(short));
        dp[i] = (double *)calloc(4, sizeof(double));
        up[i] = (unsigned char *)calloc(4, 1);
        for (j = 0; j < 4; j++)
        {
            sp[i][j] = (short)(-(i * 10 + j));
            dp[i][j] = i + j * 0.25;
            up[i][j] = (unsigned char)(250 + i + j);
        }
    }
    printf("%d %d %.2f %.2f %d %d\n", sp[1][3], sp[0][2], dp[1][3], dp[0][1],
           up[1][3], up[0][0]);
    for (i = 0; i < 3; i++)
    {
        gp[i] = (int *)malloc(3 * sizeof(int));
        gp[i][2] = i * 7;
    }
    printf("%d %d\n", gp[2][2], *(gp[1] + 2));
    for (i = 0; i < 4; i++) free(ip[i]);
    for (i = 0; i < 2; i++) { free(sp[i]); free(dp[i]); free(up[i]); }
    for (i = 0; i < 3; i++) free(gp[i]);
    return 0;
}
