m = [[0]*40 for _ in range(40)]
t = 0
for k in range(10):
    for i in range(40):
        for j in range(40):
            m[i][j] = i + j + k
    for i in range(40):
        for j in range(40):
            t += m[i][j]
print(t)
