def f(a):
    return a + (a + 1)
t = 0
v = [[0,0] for _ in range(100)]
for r in range(200):
    for i in range(100):
        v[i][0] = i
        v[i][1] = r
    for i in range(100):
        t += v[i][0] + v[i][1]
for i in range(20000):
    t += f(i)
print(t)
