#include <stdio.h>
#include <stdarg.h>
static long long ints(int count, ...) {
    va_list ap; long long sum = 0; int i;
    va_start(ap, count);
    for (i = 0; i < count; ++i) sum += va_arg(ap, int);
    va_end(ap); return sum;
}
static double doubles(int count, ...) {
    va_list ap; double sum = 0.0; int i;
    va_start(ap, count);
    for (i = 0; i < count; ++i) sum += va_arg(ap, double);
    va_end(ap); return sum;
}
int main(void) {
    char c = -3; short s = 7; float f = 1.25f;
    printf("%lld %.2f\n", ints(2, c, s), doubles(2, f, 2.5));
    return 0;
}
