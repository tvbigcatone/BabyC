#include <stdio.h>
#include <stdarg.h>
static long long collect(int count, ...) {
    va_list ap, cp;
    long long sum = 0;
    int i;
    va_start(ap, count);
    va_copy(cp, ap);
    for (i = 0; i < count; ++i) sum += va_arg(cp, int);
    va_end(cp);
    for (i = 0; i < count; ++i) sum += va_arg(ap, int);
    va_end(ap);
    return sum;
}
static double collect_fp(int count, ...) {
    va_list ap;
    double sum = 0.0;
    va_start(ap, count);
    while (count-- > 0) sum += va_arg(ap, double);
    va_end(ap);
    return sum;
}
int main(void) {
    char c = 3; short s = 4; float f = 1.5f;
    printf("%lld %.1f\n", collect(3, c, s, 5), collect_fp(2, f, 2.25));
    return 0;
}
