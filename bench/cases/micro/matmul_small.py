N = 40
A = [[(i + j) % 17 for j in range(N)] for i in range(N)]
B = [[(i * j + 1) % 13 for j in range(N)] for i in range(N)]
C = [[0]*N for _ in range(N)]
for i in range(N):
    for j in range(N):
        s = 0
        for k in range(N):
            s += A[i][k] * B[k][j]
        C[i][j] = s
total = sum(sum(row) for row in C)
print(total)
