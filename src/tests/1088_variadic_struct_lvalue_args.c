#include <stdio.h>
#include <stdarg.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
union U { int i; char c[4]; };
struct P gp = {5, 6};
struct P garr[2] = {{1, 3}, {2, 4}};
static int sump(int n, ...) {
    va_list ap; int i, s = 0; va_start(ap, n);
    for (i = 0; i < n; i++) { struct P p = va_arg(ap, struct P); s = s * 100 + p.x * 10 + p.y; }
    va_end(ap); return s;
}
static int uni(int n, ...) { va_list ap; union U u; va_start(ap, n); u = va_arg(ap, union U); va_end(ap); return u.i + n; }
int main(void) {
    struct P b = {3, 4}, arr[2] = {{7, 8}, {2, 1}};
    struct W w, *pw = &w; struct P *pp = &b; union U u, *pu = &u; int i = 0;
    w.in.x = 4; w.in.y = 4; w.tag = 9; u.i = 77;
    printf("%d %d %d\n", sump(1, w.in), sump(1, *pp), sump(2, arr[0], arr[1]));
    printf("%d %d %d\n", sump(1, gp), sump(2, garr[1], pw->in), sump(1, pp[0]));
    printf("%d %d\n", uni(1, *pu), uni(2, u));
    { int r = sump(1, arr[i++]); printf("%d %d\n", r, i); }
    return 0;
}
