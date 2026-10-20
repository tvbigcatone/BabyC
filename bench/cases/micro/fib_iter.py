def fib(n):
    a, b = 0, 1
    if n <= 0: return 0
    for i in range(1, n):
        a, b = b, a + b
    return b
s = 0
for i in range(5000):
    s += fib(30)
print(s)
