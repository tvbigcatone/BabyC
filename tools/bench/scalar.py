t=0
for i in range(1000000):
    t += i & 7
print(t)
