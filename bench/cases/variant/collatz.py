def step(x):
    if x&1: return 3*x+1
    return x>>1
total=0
for n in range(2,5000):
    x=n
    while x!=1:
        x=step(x); total+=1
print(total)
