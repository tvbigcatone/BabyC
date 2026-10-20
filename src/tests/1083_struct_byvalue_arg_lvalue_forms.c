#include <stdio.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
struct WW { int pad; struct W w; };
union U { int i; short s[2]; };
struct P gp = {5, 6};
struct P garr[3] = {{1, 1}, {2, 2}, {3, 3}};
struct W gw = {{7, 7}, 1};
static int f(struct P p) { return p.x * 10 + p.y; }
static int g(int k, struct P p, int m) { return k * 1000 + p.x * 10 + p.y + m * 100000; }
static int fu(union U u) { return u.i; }
int main(void) {
    struct P a = {1, 2}, b = {3, 4}, arr[3] = {{7, 8}, {2, 1}, {9, 9}};
    struct W w, warr[2]; struct WW ww; struct P *pp = &arr[0]; struct W *pw = &w;
    union U u, *pu = &u; int (*fp)(struct P) = f; int i = 0;
    w.in.x = 4; w.in.y = 5; w.tag = 9; warr[1].in = b; warr[0].in = a; ww.w = w; ww.w.in.x = 6;
    u.i = 1234;
    printf("%d %d %d %d\n", f(w.in), f(*pp), f(arr[1]), f(pp[2]));
    printf("%d %d %d %d\n", f(gp), f(garr[2]), f(gw.in), f(pw->in));
    printf("%d %d %d\n", f(*(pp + 1)), f(ww.w.in), f(warr[1].in));
    printf("%d %d\n", fu(*pu), fu(u));
    printf("%d %d\n", fp(w.in), fp(*pp));
    { int r = f(arr[i++]); printf("%d %d\n", r, i); }
    printf("%d %d\n", g(1, w.in, 2), g(3, garr[1], 4));
    printf("%d\n", f(*&b));
    return 0;
}
