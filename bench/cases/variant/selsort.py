N=900
v=[0]*N;x=11
def minidx(p,frm,n):
    m=frm
    for i in range(frm+1,n):
        if p[i]<p[m]: m=i
    return m
for i in range(N): x=(x*75+74)%65537; v[i]=x
for i in range(N-1):
    m=minidx(v,i,N); v[i],v[m]=v[m],v[i]
chk=0
for i in range(N): chk=(chk*31+v[i])%1000003
print(v[0],v[N-1],chk)
