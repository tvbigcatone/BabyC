t = 0
for i in range(200000):
    buf = "%d:%d:%d" % (i, i + 1, i * 2)
    t += ord(buf[0]) + ord(buf[1])
print(t)
