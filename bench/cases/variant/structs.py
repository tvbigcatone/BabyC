N=400
class P: __slots__=('x','y','v')
ps=[]
for i in range(N):
    p=P();p.x=i;p.y=N-i;p.v=1;ps.append(p)
def upd(p):
    p.x=(p.x+p.v)%1000
    p.y=(p.y+p.x)%1000
    p.v=(p.v+p.y)%7+1
for r in range(150):
    for i in range(N): upd(ps[i])
chk=0
for i in range(N):
    p=ps[i]; chk=(chk*31+p.x+p.y*3+p.v)%1000003
print(chk)
