def gcd(a,b): return a if b==0 else gcd(b,a%b)
s=0
for i in range(1,201):
    for j in range(1,201): s+=gcd(i,j)
print(s)
