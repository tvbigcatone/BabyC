/* Anti-regression: *o.ip / *o.in under vm_strict (Unary* of Member). */
#include <stdio.h>
struct Inner { int v; };
struct Outer { struct Inner *in; int *ip; };
int main(void) {
    struct Inner i;
    struct Outer o;
    int x = 10;
    i.v = 3;
    o.in = &i;
    o.ip = &x;
    printf("%d %d\n", *o.ip, o.in->v);
    *o.ip = *o.ip + 2;
    o.in->v = o.in->v + 4;
    printf("%d %d\n", x, i.v);
    return 0;
}
