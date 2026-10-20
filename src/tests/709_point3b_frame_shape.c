/* Point3b: compile-time NumLocals template — scalar locals + recursion. */
int add(int a, int b)
{
    int t;
    t = a + b;
    return t;
}

int sum_to(int n)
{
    int i, s;
    s = 0;
    for (i = 1; i <= n; i++)
        s = s + i;
    return s;
}

int main()
{
    if (add(3, 4) != 7) return 1;
    if (sum_to(10) != 55) return 2;
    if (sum_to(100) != 5050) return 3;
    return 0;
}
