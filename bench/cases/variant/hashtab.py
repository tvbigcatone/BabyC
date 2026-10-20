M=4096
keys=[0]*M;used=[0]*M
x=1;hits=0;ins=0
for i in range(2500):
    x=(x*1103+12345)%65536;k=x;h=(k*31)%M
    while used[h] and keys[h]!=k: h=(h+1)%M
    if not used[h]: used[h]=1;keys[h]=k;ins+=1
x=1
for i in range(20000):
    x=(x*1103+12345)%65536;k=x;h=(k*31)%M
    while used[h] and keys[h]!=k: h=(h+1)%M
    if used[h]: hits+=1
print(ins,hits)
