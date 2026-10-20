#include <stdio.h>
#include <stdarg.h>
static long long mixed(int n, ...) {
    va_list ap;
    long long total;
    long lv;
    long long llv;
    double dv;
    int *pv;
    int cv;
    va_start(ap, n);
    lv = va_arg(ap, long);
    llv = va_arg(ap, long long);
    dv = va_arg(ap, double);
    pv = va_arg(ap, int *);
    cv = va_arg(ap, int);
    total = (long long)lv + llv + (long long)(dv * 10.0) + *pv + cv;
    va_end(ap);
    return total;
}
int main(void) {
    int pointed = 11;
    char promoted = -4;
    printf("%lld\n", mixed(5, 100000L, 20000000000LL, 2.5, &pointed, promoted));
    return 0;
}
