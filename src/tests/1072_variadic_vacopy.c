#include <stdio.h>
#include <stdarg.h>
static void probe(int n, ...)
{
    va_list a, b;
    va_start(a, n);
    va_copy(b, a);
    printf("%.3f %.3f\n", va_arg(a, double), va_arg(b, double));
    va_end(b);
    va_end(a);
}
int main(void) { probe(1, 4.125); return 0; }
