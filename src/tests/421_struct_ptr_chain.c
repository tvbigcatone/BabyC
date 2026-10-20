#include <stdio.h>
struct Inner { int v; };
struct Outer { struct Inner *in; int *ip; };
int main() {
    struct Inner i;
    struct Outer o;
    int x;
    i.v = 5;
    x = 9;
    o.in = &i;
    o.ip = &x;
    o.in->v = o.in->v + 1;
    *o.ip = *o.ip + 1;
    printf("%d %d\n", i.v, x);
    return 0;
}
