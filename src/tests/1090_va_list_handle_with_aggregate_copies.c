#include <stdio.h>
#include <stdarg.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
static int vsum(int n, va_list ap) {
    int s = 0;
    while (n-- > 0) s += va_arg(ap, int);
    return s;
}
static int both(int n, ...) {
    va_list ap, cp; int a, b;
    va_start(ap, n);
    va_copy(cp, ap);
    a = vsum(n, ap);
    b = vsum(n, cp);
    va_end(cp);
    va_end(ap);
    return a * 1000 + b;
}
static int mixed(struct W *pw, int n, ...) {
    va_list ap; struct W w; struct P q; int i, s = 0;
    w = *pw; q = w.in;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        struct P p = va_arg(ap, struct P);
        s = s * 100 + p.x * 10 + p.y;
    }
    va_end(ap);
    return s + q.x * 10000 + (int)w.tag;
}
int main(void) {
    struct W w; struct P arr[2] = {{1, 2}, {3, 4}};
    w.in.x = 5; w.in.y = 6; w.tag = 7;
    printf("%d\n", both(3, 1, 2, 3));
    printf("%d\n", mixed(&w, 2, arr[0], w.in));
    return 0;
}
