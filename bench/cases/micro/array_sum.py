N = 10000
a = list(range(N))
s = 0
for k in range(200):
    for i in range(N):
        s += a[i]
print(s)
