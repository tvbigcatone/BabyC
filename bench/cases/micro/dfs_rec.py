N = 12
adj = [[1 if (i * 7 + j * 3) % 5 == 0 else 0 for j in range(N)] for i in range(N)]
def dfs(u, vis, cnt):
    vis[u] = 1
    cnt[0] += 1
    for v in range(N):
        if adj[u][v] and not vis[v]:
            dfs(v, vis, cnt)
total = 0
for r in range(5000):
    vis = [0]*N
    cnt = [0]
    dfs(0, vis, cnt)
    total += cnt[0]
print(total)
