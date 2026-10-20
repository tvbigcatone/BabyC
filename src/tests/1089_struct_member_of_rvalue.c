#include <stdio.h>
struct P { int x, y; };
union U { int i; unsigned char b[4]; };
struct P G = {9, 10};
static struct P mk(int a) { struct P r; r.x = a; r.y = a * 2; return r; }
static struct P deref(struct P *p) { return *p; }
static struct P pick(int c, struct P *a, struct P *b) { return c ? *a : *b; }
static struct P viaret(struct P *p) { return mk(p->x + 1); }
static union U un(int v) { union U u; u.i = v; return u; }
int main(void) {
    struct P a = {1, 2}, b = {3, 4}, c, *pb = &b;
    printf("%d %d\n", mk(3).y, deref(pb).y);
    printf("%d %d\n", mk(5).x + mk(6).y, un(258).i);
    c = pick(0, &a, &b); printf("%d %d\n", c.x, c.y);
    c = pick(1, &a, &b); printf("%d %d\n", c.x, c.y);
    c = viaret(&b); printf("%d %d\n", c.x, c.y);
    c = G; G.x = 0; printf("%d %d\n", c.x, G.x);
    return 0;
}
