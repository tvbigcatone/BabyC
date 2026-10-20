#include <stdarg.h>
long long g(int n, ...)
{
    va_list ap;
    long long x;
    va_start(ap, n);
    x = va_arg(ap, long long);
    va_end(ap);
    return x;
}
int main(void) { return (int)g(1, 7); }
