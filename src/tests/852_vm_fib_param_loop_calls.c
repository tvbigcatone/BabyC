#include <stdio.h>
/* BUG A consequence: repeated calls with a varying int parameter.
 * Expected 2080100000 (checked with gcc -O2). */
long long fib(int n, int k)
{
    long long a = 0, b = 1, t;
    int i;
    if (n <= 0)
        return 0;
    for (i = 1; i < n; i++) { t = (a + b) ^ (k & 1); a = b + k - k; b = t; }
    return b;
}
int main(void)
{
    int i;
    long long s = 0;
    for (i = 0; i < 5000; i++)
        s += fib(30, i);
    printf("%lld\n", s);
    return 0;
}
