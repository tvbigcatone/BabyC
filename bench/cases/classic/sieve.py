N=500000
s=bytearray(N+1)
for i in range(2,N+1): s[i]=1
i=2
while i*i<=N:
    if s[i]:
        for j in range(i*i,N+1,i): s[j]=0
    i+=1
c=0
for i in range(2,N+1):
    if s[i]: c+=1
print(c)
