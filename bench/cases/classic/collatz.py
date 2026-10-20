total=0;best=0
for n in range(1,8000):
    x=n;steps=0
    while x!=1:
        if x%2==0: x=x//2
        else: x=3*x+1
        steps+=1
    total+=steps
    if steps>best: best=steps
print(total,best)
