#include <stdio.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
struct P gp = {5, 6};
static int f(struct P p) { return p.x * 10 + p.y; }
int main(void) {
    struct P a = {1, 2}, b = {3, 4}, arr[2] = {{7, 8}, {2, 1}};
    struct W w; struct P *pp = &b;
    w.in.x = 4; w.in.y = 4; w.tag = 9;
    printf("%d %d %d %d %d %d\n", f(a), f(w.in), f(arr[1]), f(*pp), f(gp), f(pp[0]));
    return 0;
}
