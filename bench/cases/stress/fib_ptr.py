calls = 0
def fib(n):
    global calls
    calls += 1
    if n < 2: return n
    return fib(n-1) + fib(n-2)
r = fib(30)
print(r, calls)
