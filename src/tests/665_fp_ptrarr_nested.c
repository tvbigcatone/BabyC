#include <stdio.h>
#include <stdlib.h>
/* Structural: T *a[N]; a[i][j] write/read — no name-bound path */
int main() {
    int i, s = 0;
    int *cells[4];
    for (i = 0; i < 4; i++)
        cells[i] = (int *)malloc(sizeof(int) * 8);
    for (i = 0; i < 4; i++) {
        cells[i][0] = i + 1;
        cells[i][7] = (i + 1) * 10;
    }
    for (i = 0; i < 4; i++)
        s += cells[i][0] + cells[i][7];
    for (i = 0; i < 4; i++) free(cells[i]);
    printf("%d\n", s);
    return 0;
}
