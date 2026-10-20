#include <stdio.h>
#include <stdarg.h>
static void probe(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double d = va_arg(ap, double);
    int i = va_arg(ap, int);
    long long ll = va_arg(ap, long long);
    unsigned long u = va_arg(ap, unsigned long);
    int *p = va_arg(ap, int *);
    printf("%.1f %d %lld %lu %d\n", d, i, ll, u, *p);
    va_end(ap);
}
int main(void)
{
    int x = 73;
    probe(5, 2.5, 17, 1LL << 40, (unsigned long)1 << 34, &x);
    return 0;
}
