#include <stdio.h>
/* Intermediate row m[i] must not trip uninit when followed by [j] assignment. */
int main()
{
    int m[2][3];
    int i, j;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            m[i][j] = i * 10 + j;
    printf("%d %d\n", m[0][2], m[1][0]);
    return 0;
}
