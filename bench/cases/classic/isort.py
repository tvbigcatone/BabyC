N=1200
v=[0]*N
x=12345
for i in range(N):
    x=(x*75)%65537; v[i]=x
for i in range(1,N):
    t=v[i]; j=i-1
    while j>=0 and v[j]>t:
        v[j+1]=v[j]; j-=1
    v[j+1]=t
chk=0
for i in range(N): chk=(chk*31+v[i])%1000003
print(v[0],v[N-1],chk)
