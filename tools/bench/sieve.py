s=[0]*8000
for r in range(20):
    for i in range(8000): s[i]=1
    for i in range(2,8000):
        if s[i]:
            for j in range(i+i,8000,i): s[j]=0
    c=0
    for i in range(2,8000): c+=s[i]
print(c)
