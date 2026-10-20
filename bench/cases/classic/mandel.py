inside=0
for py in range(80):
    for px in range(120):
        x0=px*3.5/120.0-2.5; y0=py*2.0/80.0-1.0
        x=0.0;y=0.0;it=0
        while it<60 and x*x+y*y<=4.0:
            xt=x*x-y*y+x0; y=2.0*x*y+y0; x=xt; it+=1
        if it==60: inside+=1
print(inside)
