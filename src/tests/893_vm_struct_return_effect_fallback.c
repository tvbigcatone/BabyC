#include <stdio.h>
struct S { int a; int b; };
static int g;
struct S make_effect(int x) {
    struct S r;
    if (x > 0) g++;
    r.a = x;
    r.b = g;
    return r;
}
int main(void) {
    struct S v[1];
    v[0] = make_effect(7);
    printf("%d %d %d\n", v[0].a, v[0].b, g);
    return 0;
}
