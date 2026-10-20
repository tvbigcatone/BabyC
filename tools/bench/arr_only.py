a = [0]*1000
for i in range(1000): a[i] = i
t = 0
for r in range(300):
    for i in range(1000):
        t += a[i]
print(t)
