s = [1]*1000
c = 0
for r in range(1000):
    for i in range(1000):
        c += s[i]
print(c)
