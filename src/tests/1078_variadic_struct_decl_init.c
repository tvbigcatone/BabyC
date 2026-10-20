#include <stdio.h>
#include <stdarg.h>
struct P { int x, y; };
union U { int i; char c[4]; };
static int sump(int n, ...)
{
    va_list ap;
    int i, s = 0;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        struct P p = va_arg(ap, struct P);
        s = s * 100 + p.x * 10 + p.y;
    }
    va_end(ap);
    return s;
}
static int uni(int n, ...)
{
    va_list ap;
    union U u = (va_start(ap, n), va_arg(ap, union U));
    va_end(ap);
    return u.i + n;
}
static struct P mk(int a) { struct P r; r.x = a; r.y = a + 1; return r; }
int main(void)
{
    struct P a = {1, 2}, b = {3, 4};
    union U u;
    u.i = 77;
    printf("%d\n", sump(2, a, b));
    printf("%d\n", sump(2, mk(5), a.x ? b : a));
    printf("%d\n", sump(1, (b = a)));
    printf("%d\n", uni(1, u));
    return 0;
}
