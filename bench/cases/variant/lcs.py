L=400
a=[0]*L;b=[0]*L;x=7
for i in range(L): x=(x*1103+12345)%32768; a[i]=97+((x>>6)%4)
for i in range(L): x=(x*1103+12345)%32768; b[i]=97+((x>>6)%4)
prev=[0]*(L+1);cur=[0]*(L+1)
for i in range(1,L+1):
    cur[0]=0
    for j in range(1,L+1):
        if a[i-1]==b[j-1]: cur[j]=prev[j-1]+1
        elif prev[j]>cur[j-1]: cur[j]=prev[j]
        else: cur[j]=cur[j-1]
    for j in range(L+1): prev[j]=cur[j]
print(prev[L])
