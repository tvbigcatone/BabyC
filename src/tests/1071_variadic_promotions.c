#include <stdio.h>
#include <stdarg.h>
static void probe(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double d = va_arg(ap, double);
    int c = va_arg(ap, int);
    printf("%.2f %d\n", d, c);
    va_end(ap);
}
int main(void)
{
    float f = 1.25f;
    char c = 65;
    probe(2, f, c);
    return 0;
}
