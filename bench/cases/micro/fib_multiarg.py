def fib2(a, b):
    if a < 2:
        return a + b
    return fib2(a - 1, b) + fib2(a - 2, b + 1)

if __name__ == "__main__":
    print(fib2(20, 0))
