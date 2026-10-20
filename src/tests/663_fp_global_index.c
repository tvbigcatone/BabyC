#include <stdio.h>
#define N 6
int G[N][N];
int row[N];
/* Structural: global 2D/1D index in recursive explore — not name-bound */
void explore(int u) {
    int v;
    row[u] = 1;
    for (v = 0; v < N; v++)
        if (G[u][v] && !row[v]) explore(v);
}
int main() {
    int i, j, t = 0;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            G[i][j] = ((i + j) % 3 == 0) ? 1 : 0;
    for (i = 0; i < N; i++) row[i] = 0;
    explore(0);
    for (i = 0; i < N; i++) t += row[i];
    printf("%d\n", t);
    return 0;
}
