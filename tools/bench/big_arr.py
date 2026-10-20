a = list(range(8000))
t = 0
for r in range(200):
    for i in range(8000):
        t += a[i]
print(t)
