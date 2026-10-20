#include <stdio.h>
#include <stdarg.h>

static int inner(const char *fmt, ...)
{
    va_list a, b, c;
    int a1, b1, a2, c1, b2;
    va_start(a, fmt);
    va_start(b, fmt);
    va_copy(c, a);
    a1 = va_arg(a, int);
    b1 = va_arg(b, int);
    a2 = va_arg(a, int);
    c1 = va_arg(c, int);
    b2 = va_arg(b, int);
    printf("%d %d %d %d %d\n", a1, b1, a2, c1, b2);
    va_end(a);
    va_end(b);
    va_end(c);
    return a1 + b1 + a2 + c1 + b2;
}

static int outer(const char *fmt, ...)
{
    va_list ap;
    int first, last, nested;
    va_start(ap, fmt);
    first = va_arg(ap, int);
    nested = inner("%d", first + 1, first + 2, first + 3);
    last = va_arg(ap, int);
    printf("outer %d %d %d\n", first, last, nested);
    va_end(ap);
    return 0;
}

int main(void)
{
    return outer("%d", 10, 20);
}
