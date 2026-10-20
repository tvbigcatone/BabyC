#include <stdio.h>
struct Obj { int a, b, c; };
int sum3(struct Obj *o) {
    return o->a + o->b + o->c;
}
struct Obj o;
int main() {
    int i, t;
    t = 0;
    o.a = 2; o.b = 3; o.c = 5;
    for (i = 0; i < 100; i = i + 1)
        t = t + sum3(&o);
    printf("%d\n", t);
    return 0;
}
