N = 800
s = 0
for pass_ in range(20):
    a = [(i * 17 + pass_ * 3) % 997 for i in range(N)]
    for i in range(1, N):
        key = a[i]
        j = i - 1
        while j >= 0 and a[j] > key:
            a[j + 1] = a[j]
            j -= 1
        a[j + 1] = key
    s += a[0] + a[N//2] + a[N-1]
print(s)
