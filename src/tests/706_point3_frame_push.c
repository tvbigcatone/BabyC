/* Point3a: recursive calls must push/pop frames without leak/overflow. */
int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main()
{
    if (fib(0) != 0) return 1;
    if (fib(1) != 1) return 2;
    if (fib(10) != 55) return 3;
    if (fib(15) != 610) return 4;
    return 0;
}
