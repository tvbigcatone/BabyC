N=400001
flag=[0]*(N+2)
def count(n):
    c=0
    for i in range(2,n+1):
        if flag[i]==0: c+=1
    return c
i=2
while i<=N:
    if flag[i]==0:
        j=i+i
        while j<=N:
            flag[j]=1; j+=i
    i+=1
print(count(N))
