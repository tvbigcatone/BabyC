N=70
a=[[0]*N for _ in range(N)];b=[[0]*N for _ in range(N)];c=[[0]*N for _ in range(N)]
for i in range(N):
    for j in range(N):
        a[i][j]=(i*7+j*3)%11; b[i][j]=(i*5+j*13)%17
for i in range(N):
    for j in range(N):
        s=0
        for k in range(N): s+=a[i][k]*b[k][j]
        c[i][j]=s
chk=0
for i in range(N):
    for j in range(N): chk=(chk*31+c[i][j])%1000003
print(chk)
