#include <stdio.h>
int main()
{
    int m[2][3];
    int i, j;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            m[i][j] = i * 3 + j;
    m[1][2] += 10;
    printf("%d %d %d\n", m[0][0], m[1][1], m[1][2]);
    return 0;
}
