N=400
v=[0]*N;x=5
def gt(a,b): return a>b
for i in range(N): x=(x*75+74)%65537; v[i]=x
for i in range(N-1):
    for j in range(N-1-i):
        if gt(v[j],v[j+1]): v[j],v[j+1]=v[j+1],v[j]
chk=0
for i in range(N): chk=(chk*31+v[i])%1000003
print(v[0],v[N-1],chk)
