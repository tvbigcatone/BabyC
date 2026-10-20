buf = ''.join(chr(ord('a') + (i % 26)) for i in range(255))
n = 0
for k in range(20000):
    for ch in buf:
        if ch == 'm' or ch == 'x':
            n += 1
print(n)
