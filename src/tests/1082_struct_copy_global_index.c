#include <stdio.h>
struct P { int x, y; };
struct P gp = {5, 6};
static int f(struct P p) { return p.x * 10 + p.y; }
int main(void) {
    struct P arr[2] = {{7, 8}, {2, 1}}, q;
    struct P *pp = &arr[1];
    q = gp; printf("%d %d\n", q.x, q.y);
    q = arr[1]; printf("%d %d\n", q.x, q.y);
    q = pp[0]; printf("%d %d %d\n", q.x, q.y, f(pp[0]));
    return 0;
}
