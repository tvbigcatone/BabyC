#include <stdio.h>
/* Anti-regression: 2D write with occ[i] index + occ[i]++ in for-loop */
int line[8][4];
int id[8][4];
int occ[8];
int has_multi(int idx)
{
    int i, j;
    for (i = 0; i < occ[idx]; i++)
        for (j = i + 1; j < occ[idx]; j++)
            if (id[idx][i] != id[idx][j]) return 1;
    return 0;
}
int main()
{
    int i;
    occ[0] = 0;
    for (i = 0; i < 3; i++) {
        line[0][occ[0]] = 10 + i;
        id[0][occ[0]] = (i == 2) ? 99 : 1;
        occ[0]++;
    }
    printf("occ=%d multi=%d id0=%d id2=%d\n", occ[0], has_multi(0), id[0][0], id[0][2]);
    return 0;
}
