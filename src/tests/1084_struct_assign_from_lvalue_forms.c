#include <stdio.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
union U { int i; short s[2]; };
struct P gp = {5, 6};
struct P garr[3] = {{1, 1}, {2, 2}, {3, 3}};
struct W gw = {{7, 7}, 1};
int main(void) {
    struct P b = {3, 4}, q, arr[3] = {{7, 8}, {2, 1}, {9, 9}};
    struct W w; struct P *pp = &arr[0], *pq; struct W *pw = &w;
    union U u, v, *pu = &u; int i = 0;
    w.in.x = 4; w.in.y = 9; w.tag = 9; u.i = 4321;
    q = w.in; printf("%d %d\n", q.x, q.y);
    q = *pp; printf("%d %d\n", q.x, q.y);
    q = gp; printf("%d %d\n", q.x, q.y);
    q = arr[1]; printf("%d %d\n", q.x, q.y);
    q = pp[2]; printf("%d %d\n", q.x, q.y);
    q = pw->in; printf("%d %d\n", q.x, q.y);
    q = garr[i++]; printf("%d %d %d\n", q.x, q.y, i);
    q = arr[i++]; printf("%d %d %d\n", q.x, q.y, i);
    pq = pp; q = *pq++; printf("%d %d %d\n", q.x, q.y, (int)(pq - pp));
    q = gw.in; printf("%d %d\n", q.x, q.y);
    v = *pu; printf("%d\n", v.i);
    q = b; printf("%d %d\n", q.x, q.y);
    return 0;
}
