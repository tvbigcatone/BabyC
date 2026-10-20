#include <stdio.h>

static int cube[2][3][4];

int main(void)
{
    int i = 1, j = 2, k = 3;
    int *slots[25] = {
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k],
        &cube[i][j][k]
    };
    int seen, seq;

    cube[1][2][3] = 73;
    cube[0][1][2] = 19;
    seen = (cube[0][1][2] = 41, cube[0][1][2]);
    seq = (i = 0, j = 1, k = 2, i + j + k);
    printf("%d %d %d %d %d\n", *slots[0], *slots[24],
           cube[0][1][2], seq, seen);
    return 0;
}
