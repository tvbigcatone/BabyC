#include <stdio.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
union U { int i; short s[2]; };
struct P gp = {5, 6};
struct P garr[3] = {{1, 1}, {2, 2}, {3, 3}};
static struct P deref(struct P *p) { return *p; }
static struct P mem(struct W *w) { return w->in; }
static struct P idx(struct P *a, int i) { return a[i]; }
static struct P glob(void) { return gp; }
static struct P gidx(int i) { return garr[i]; }
static struct P loc(void) { struct W w; w.in.x = 6; w.in.y = 1; w.tag = 0; return w.in; }
static struct P locidx(int i) { struct P a[2]; a[0].x = 1; a[0].y = 2; a[1].x = 3; a[1].y = 4; return a[i]; }
static union U uderef(union U *u) { return *u; }
int main(void) {
    struct P arr[2] = {{7, 8}, {2, 1}}, q;
    struct W w, *pw = &w; struct P *pin = &w.in; union U u, v, *pu = &u;
    w.in.x = 4; w.in.y = 3; w.tag = 1; u.i = 99;
    q = deref(arr + 1); printf("%d %d\n", q.x, q.y);
    q = mem(pw); printf("%d %d\n", q.x, q.y);
    q = idx(arr, 0); printf("%d %d\n", q.x, q.y);
    q = glob(); printf("%d %d\n", q.x, q.y);
    q = gidx(2); printf("%d %d\n", q.x, q.y);
    q = loc(); printf("%d %d\n", q.x, q.y);
    q = locidx(1); printf("%d %d\n", q.x, q.y);
    { struct P t = deref(pin); printf("%d\n", t.y); }
    v = uderef(pu); printf("%d\n", v.i);
    return 0;
}
