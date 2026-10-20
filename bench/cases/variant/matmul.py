N=60
a=[(i*7)%11 for i in range(N*N)]; b=[(i*13+5)%17 for i in range(N*N)]; c=[0]*(N*N)
for i in range(N):
    for k in range(N):
        for j in range(N):
            c[i*N+j]+=a[i*N+k]*b[k*N+j]
chk=0
for i in range(N*N): chk=(chk*31+c[i])%1000003
print(chk)
