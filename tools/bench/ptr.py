a = list(range(1000))
t = 0
for r in range(300):
    for i in range(1000):
        t += a[i]
print(t)
