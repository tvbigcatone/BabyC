def it(cr,ci):
    zr=0.0;zi=0.0;n=0
    while n<50 and zr*zr+zi*zi<=4.0:
        t=zr*zr-zi*zi+cr; zi=2.0*zr*zi+ci; zr=t; n+=1
    return n
s=0
for y in range(60):
    for x in range(90): s+=it(x*3.0/90.0-2.0, y*2.4/60.0-1.2)
print(s)
