#include <stdio.h>
#define N 5
int tab[N];
int mat[N][N];
/* Structural: global 1D/2D stores, varied names — no name-bound fastpath */
void fill(int n) {
    int i, j;
    for (i = 0; i < n; i++)
        tab[i] = i + 1;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            mat[i][j] = i * 10 + j;
}
int main() {
    int i, j, s = 0;
    fill(N);
    for (i = 0; i < N; i++) s += tab[i];
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            s += mat[i][j];
    printf("%d\n", s);
    return 0;
}
