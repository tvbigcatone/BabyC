#include <stdarg.h>
double g(int n, ...)
{
    va_list ap;
    double d;
    va_start(ap, n);
    d = va_arg(ap, double);
    va_end(ap);
    return d;
}
int main(void)
{
    return (int)g(1, 7);
}
