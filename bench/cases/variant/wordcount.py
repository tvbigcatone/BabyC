L=30000
t=[0]*(L+1);hist=[0]*32;x=3
for i in range(L):
    x=(x*1103+12345)%32768
    t[i]=32 if ((x>>4)%6)==0 else 97+(x%26)
t[L]=0
ln=0;words=0;longest=0
for i in range(L+1):
    if t[i]!=32 and t[i]!=0: ln+=1
    else:
        if ln>0:
            words+=1
            if ln>longest: longest=ln
            if ln>31: hist[31]+=1
            else: hist[ln]+=1
        ln=0
chk=0
for i in range(32): chk=(chk*31+hist[i])%1000003
print(words,longest,chk,L)
