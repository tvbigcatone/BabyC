#include <stdio.h>
#include <stdarg.h>

static int forward(const char *fmt, ...)
{
    va_list ap, copy;
    int i;
    unsigned u;
    long l;
    long long ll;
    double d;
    const char *s;
    void *p;
    char out[160];
    va_start(ap, fmt);
    va_copy(copy, ap);
    vsnprintf(out, sizeof(out), fmt, copy);
    va_end(copy);
    i = va_arg(ap, int);
    u = va_arg(ap, unsigned);
    l = va_arg(ap, long);
    ll = va_arg(ap, long long);
    d = va_arg(ap, double);
    s = va_arg(ap, const char *);
    p = va_arg(ap, void *);
    va_end(ap);
    printf("%s\n", out);
    printf("%d|%u|%ld|%lld|%.2f|%s|%s\n", i, u, l, ll, d, s, (const char *)p);
    return 0;
}

int main(void)
{
    int x = 7;
    return forward("%d|%u|%ld|%lld|%.2f|%s|%s", -3, 4000000000u, 123456789L,
        9000000000LL, (float)2.25, "ok", (void *)"ptr");
}
