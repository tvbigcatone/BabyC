#include <stdio.h>
#include <stdarg.h>
static double sum_mixed(int n, ...)
{
    va_list ap;
    double sum = 0.0;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; ++i)
    {
        if ((i & 1) == 0) sum += (double)va_arg(ap, int);
        else sum += va_arg(ap, double);
    }
    va_end(ap);
    return sum;
}
int main(void)
{
    printf("%.2f\n", sum_mixed(14,
        1, 2.5, 3, 4.25, 5, 6.5, 7, 8.25,
        9, 10.5, 11, 12.25, 13, 14.5));
    return 0;
}
