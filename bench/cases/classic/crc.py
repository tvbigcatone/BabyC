tab=[0]*256
for i in range(256):
    c=i
    for k in range(8):
        if c&1: c=0xEDB88320^(c>>1)
        else: c=c>>1
    tab[i]=c
data=[0]*20000;x=1
for i in range(20000): x=(x*131+17)%65521; data[i]=x&255
crc=0xFFFFFFFF
for i in range(20000): crc=tab[(crc^data[i])&255]^(crc>>8)
crc^=0xFFFFFFFF
print(crc)
