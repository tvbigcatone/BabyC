L=5000
buf=[0]*(L+1)
h=0
for r in range(20):
    for i in range(L): buf[i]=97+((i*7+r)%26)
    buf[L]=0
    vow=0;i=0
    while buf[i]!=0:
        c=buf[i]
        if c==97 or c==101 or c==105 or c==111 or c==117: vow+=1
        i+=1
    i=0;j=L-1
    while i<j:
        t=buf[i];buf[i]=buf[j];buf[j]=t;i+=1;j-=1
    for i in range(L): h=(h*33+buf[i])%1000003
    h=(h+vow)%1000003
print(h)
