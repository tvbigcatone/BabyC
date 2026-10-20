def gcd(a,b):
    while b:
        t=a%b;a=b;b=t
    return a
s=0
for i in range(1,251):
    for j in range(1,251): s+=gcd(i,j)
print(s)
