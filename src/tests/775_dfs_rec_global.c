#include <stdio.h>
#define N 6
int adj[N][N];
int vis[N];
int cnt;
void dfs(int u) {
    int v;
    vis[u] = 1;
    cnt++;
    for (v = 0; v < N; v++)
        if (adj[u][v] && !vis[v]) dfs(v);
}
int main() {
    int i, j, r;
    long long total = 0;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            adj[i][j] = ((i * 7 + j * 3) % 5 == 0) ? 1 : 0;
    for (r = 0; r < 100; r++) {
        for (i = 0; i < N; i++) vis[i] = 0;
        cnt = 0;
        dfs(0);
        total += cnt;
    }
    printf("%lld\n", total);
    return 0;
}
