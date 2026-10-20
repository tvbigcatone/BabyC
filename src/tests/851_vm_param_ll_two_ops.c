#include <stdio.h>
/* BUG A (VM): long long accumulator + int parameter in a two-operator expression.
 * Expected 10 / 319 / 55.  Wrong result seen under BABYC_EXEC=vm: 1 / 1 / 1. */
long long f_and(int n, int k)
{
    long long b = 1;
    int i;
    for (i = 1; i < n; i++)
        b = b + (k & 1);
    return b;
}
long long f_add3(int n, int k)
{
    long long a = 0, b = 1, t;
    int i;
    for (i = 1; i < n; i++) { t = a + b + k; a = b; b = t; }
    return b;
}
long long f_kk(int n, int k)
{
    long long a = 0, b = 1, t;
    int i;
    for (i = 1; i < n; i++) { t = a + b; a = b + k - k; b = t; }
    return b;
}
int main(void)
{
    printf("%lld\n", f_and(10, 7));
    printf("%lld\n", f_add3(10, 3));
    printf("%lld\n", f_kk(10, 7));
    return 0;
}
