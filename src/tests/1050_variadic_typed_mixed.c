#include <stdio.h>
#include <stdarg.h>
static long long mixed(int n, ...) {
    va_list ap;
    double d;
    int i;
    long long ll;
    int *p;
    va_start(ap, n);
    d = va_arg(ap, double);
    i = va_arg(ap, int);
    ll = va_arg(ap, long long);
    p = va_arg(ap, int *);
    va_end(ap);
    return (long long)d + i + ll + *p;
}
int main(void) {
    int x = 7;
    printf("%lld\n", mixed(4, 2.5, 11, 5000000000LL, &x));
    return 0;
}
