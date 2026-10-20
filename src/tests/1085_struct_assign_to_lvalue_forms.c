#include <stdio.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
struct P gp = {5, 6};
struct P garr[3];
struct W gw;
int main(void) {
    struct P q = {1, 2}, arr[3] = {{0, 0}, {0, 0}, {0, 0}};
    struct W w; struct P *pp = &arr[0]; struct W *pw = &w;
    w.tag = 3;
    w.in = q; printf("%d %d\n", w.in.x, w.in.y);
    *pp = w.in; printf("%d %d\n", arr[0].x, arr[0].y);
    gp = q; printf("%d %d\n", gp.x, gp.y);
    arr[1] = gp; printf("%d %d\n", arr[1].x, arr[1].y);
    garr[2] = *pp; printf("%d %d\n", garr[2].x, garr[2].y);
    gw.in = arr[1]; printf("%d %d\n", gw.in.x, gw.in.y);
    q.x = 9; pw->in = q; printf("%d %d %ld\n", w.in.x, w.in.y, w.tag);
    pp[2] = pw->in; printf("%d %d\n", arr[2].x, arr[2].y);
    return 0;
}
